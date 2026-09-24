#include "RuntimeHud.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "GameForger/Core/ProjectPaths.hpp"
#include "GameForger/Editor/TerrainTexture.hpp"

namespace gameforger::editor
{
	namespace
	{
		constexpr const char* kVertexShader = R"glsl(
#version 460 core
layout (location = 0) in vec2 position;
layout (location = 1) in vec2 uv;
out vec2 fragUv;
uniform mat4 projection;
void main()
{
	fragUv = uv;
	gl_Position = projection * vec4(position, 0.0, 1.0);
}
)glsl";

		// mode 0: flat color, 1: font atlas (alpha = red channel), 2: RGBA image.
		constexpr const char* kFragmentShader = R"glsl(
#version 460 core
in vec2 fragUv;
out vec4 fragColor;
uniform sampler2D sampler;
uniform vec4 color;
uniform int mode;
void main()
{
	if (mode == 1)
	{
		fragColor = vec4(color.rgb, color.a * texture(sampler, fragUv).r);
	}
	else if (mode == 2)
	{
		fragColor = texture(sampler, fragUv) * color;
	}
	else
	{
		fragColor = color;
	}
}
)glsl";

		GLuint compile(const GLenum type, const char* source)
		{
			const GLuint shader = glCreateShader(type);
			glShaderSource(shader, 1, &source, nullptr);
			glCompileShader(shader);
			GLint ok = GL_FALSE;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
			if (ok == GL_FALSE)
			{
				std::array<char, 1024> log{};
				glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
				std::fprintf(stderr, "RuntimeHud shader compile failed: %s\n", log.data());
			}
			return shader;
		}

		void pushQuad(std::vector<float>& out, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c,
			const glm::vec2& d, const glm::vec2& uvA = {0.0F, 0.0F}, const glm::vec2& uvB = {1.0F, 0.0F},
			const glm::vec2& uvC = {1.0F, 1.0F}, const glm::vec2& uvD = {0.0F, 1.0F})
		{
			const std::array<float, 24> quad{a.x, a.y, uvA.x, uvA.y, b.x, b.y, uvB.x, uvB.y, c.x, c.y, uvC.x, uvC.y,
				a.x, a.y, uvA.x, uvA.y, c.x, c.y, uvC.x, uvC.y, d.x, d.y, uvD.x, uvD.y};
			out.insert(out.end(), quad.begin(), quad.end());
		}
	}

	std::vector<std::filesystem::path> runtimeFontCandidates(const std::filesystem::path& projectRoot)
	{
		std::vector<std::filesystem::path> candidates;
		std::error_code error;
		const std::filesystem::path fontsDirectory = projectRoot / "Game" / "Fonts";
		if (std::filesystem::exists(fontsDirectory, error))
		{
			for (const auto& entry : std::filesystem::directory_iterator(fontsDirectory, error))
			{
				std::string extension = entry.path().extension().string();
				std::transform(extension.begin(), extension.end(), extension.begin(),
					[](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (extension == ".ttf")
				{
					candidates.push_back(entry.path());
				}
			}
		}
		for (const char* systemFont : {"C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/segoeui.ttf",
				 "C:/Windows/Fonts/arialbd.ttf", "C:/Windows/Fonts/arial.ttf"})
		{
			if (std::filesystem::exists(systemFont, error))
			{
				candidates.emplace_back(systemFont);
			}
		}
		return candidates;
	}

	RuntimeHud::~RuntimeHud()
	{
		shutdown();
	}

	bool RuntimeHud::initialize(const std::filesystem::path& projectRoot)
	{
		projectRoot_ = projectRoot;
		const GLuint vertex = compile(GL_VERTEX_SHADER, kVertexShader);
		const GLuint fragment = compile(GL_FRAGMENT_SHADER, kFragmentShader);
		program_ = glCreateProgram();
		glAttachShader(program_, vertex);
		glAttachShader(program_, fragment);
		glLinkProgram(program_);
		glDeleteShader(vertex);
		glDeleteShader(fragment);
		GLint linked = GL_FALSE;
		glGetProgramiv(program_, GL_LINK_STATUS, &linked);
		if (linked == GL_FALSE)
		{
			std::fprintf(stderr, "RuntimeHud shader failed to link.\n");
			return false;
		}
		projectionLocation_ = glGetUniformLocation(program_, "projection");
		colorLocation_ = glGetUniformLocation(program_, "color");
		modeLocation_ = glGetUniformLocation(program_, "mode");
		samplerLocation_ = glGetUniformLocation(program_, "sampler");

		glGenVertexArrays(1, &vertexArray_);
		glGenBuffers(1, &vertexBuffer_);
		glBindVertexArray(vertexArray_);
		glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
		glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<const void*>(2 * sizeof(float)));
		glEnableVertexAttribArray(1);
		glBindVertexArray(0);

		// First font that actually bakes visible glyphs wins.
		for (const std::filesystem::path& fontPath : runtimeFontCandidates(projectRoot))
		{
			std::ifstream file(fontPath, std::ios::binary);
			const std::vector<unsigned char> fontData(
				(std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			stbtt_fontinfo info{};
			if (fontData.empty() || stbtt_InitFont(&info, fontData.data(), stbtt_GetFontOffsetForIndex(fontData.data(), 0)) == 0)
			{
				continue;
			}
			std::vector<unsigned char> atlas(static_cast<std::size_t>(kAtlasSize) * kAtlasSize);
			if (stbtt_BakeFontBitmap(fontData.data(), 0, kBakePixels, atlas.data(), kAtlasSize, kAtlasSize, kFirstChar,
					kNumChars, bakedChars_.data()) <= 0 ||
				std::none_of(atlas.begin(), atlas.end(), [](const unsigned char value) { return value > 0; }))
			{
				continue;
			}
			int ascent = 0;
			int descent = 0;
			int lineGap = 0;
			stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
			fontAscent_ = static_cast<float>(ascent) * stbtt_ScaleForPixelHeight(&info, kBakePixels);
			glGenTextures(1, &fontTexture_);
			glBindTexture(GL_TEXTURE_2D, fontTexture_);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kAtlasSize, kAtlasSize, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
			glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
			std::fprintf(stderr, "HUD font: %s\n", fontPath.string().c_str());
			break;
		}
		if (fontTexture_ == 0)
		{
			std::fprintf(stderr, "RuntimeHud: no usable TrueType font found - HUD text will be blank.\n");
		}
		return true;
	}

	void RuntimeHud::shutdown() noexcept
	{
		for (auto& [path, texture] : images_)
		{
			if (texture != 0)
			{
				glDeleteTextures(1, &texture);
			}
		}
		images_.clear();
		if (fontTexture_ != 0)
		{
			glDeleteTextures(1, &fontTexture_);
			fontTexture_ = 0;
		}
		if (vertexBuffer_ != 0)
		{
			glDeleteBuffers(1, &vertexBuffer_);
			vertexBuffer_ = 0;
		}
		if (vertexArray_ != 0)
		{
			glDeleteVertexArrays(1, &vertexArray_);
			vertexArray_ = 0;
		}
		if (program_ != 0)
		{
			glDeleteProgram(program_);
			program_ = 0;
		}
	}

	void RuntimeHud::begin(const int windowWidth, const int windowHeight)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, windowWidth, windowHeight);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glUseProgram(program_);
		const glm::mat4 projection = glm::ortho(0.0F, static_cast<float>(windowWidth), static_cast<float>(windowHeight), 0.0F);
		glUniformMatrix4fv(projectionLocation_, 1, GL_FALSE, glm::value_ptr(projection));
		glUniform1i(samplerLocation_, 0);
	}

	void RuntimeHud::end()
	{
		glBindVertexArray(0);
		glUseProgram(0);
	}

	void RuntimeHud::draw(const std::vector<float>& vertices, const Mode mode, const HudColor& color, const GLuint texture)
	{
		if (vertices.empty() || program_ == 0)
		{
			return;
		}
		glUseProgram(program_);
		glUniform4f(colorLocation_, color.r, color.g, color.b, color.a);
		glUniform1i(modeLocation_, static_cast<int>(mode));
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texture);
		glBindVertexArray(vertexArray_);
		glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
		glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(),
			GL_STREAM_DRAW);
		glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 4));
	}

	void RuntimeHud::line(const glm::vec2& from, const glm::vec2& to, const HudColor& color, const float thickness)
	{
		const glm::vec2 delta = to - from;
		const float length = glm::length(delta);
		if (length < 0.001F)
		{
			return;
		}
		const glm::vec2 normal = glm::vec2(-delta.y, delta.x) / length * (thickness * 0.5F);
		scratch_.clear();
		pushQuad(scratch_, from + normal, to + normal, to - normal, from - normal);
		draw(scratch_, Mode::Solid, color, 0);
	}

	void RuntimeHud::rectFilled(const glm::vec2& min, const glm::vec2& max, const HudColor& color, float /*rounding*/)
	{
		scratch_.clear();
		pushQuad(scratch_, min, {max.x, min.y}, max, {min.x, max.y});
		draw(scratch_, Mode::Solid, color, 0);
	}

	void RuntimeHud::rect(
		const glm::vec2& min, const glm::vec2& max, const HudColor& color, float /*rounding*/, const float thickness)
	{
		rectFilled(min, {max.x, min.y + thickness}, color, 0.0F);
		rectFilled({min.x, max.y - thickness}, max, color, 0.0F);
		rectFilled(min, {min.x + thickness, max.y}, color, 0.0F);
		rectFilled({max.x - thickness, min.y}, max, color, 0.0F);
	}

	void RuntimeHud::circleFilled(const glm::vec2& center, const float radius, const HudColor& color)
	{
		constexpr int kSegments = 20;
		scratch_.clear();
		for (int index = 0; index < kSegments; ++index)
		{
			const float a0 = static_cast<float>(index) / kSegments * 6.2831853F;
			const float a1 = static_cast<float>(index + 1) / kSegments * 6.2831853F;
			const std::array<float, 12> triangle{center.x, center.y, 0.0F, 0.0F,
				center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius, 0.0F, 0.0F,
				center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius, 0.0F, 0.0F};
			scratch_.insert(scratch_.end(), triangle.begin(), triangle.end());
		}
		draw(scratch_, Mode::Solid, color, 0);
	}

	void RuntimeHud::text(const glm::vec2& position, const HudColor& color, const std::string& text, const float scale)
	{
		if (fontTexture_ == 0 || text.empty())
		{
			return;
		}
		const float factor = kUiPixels * scale / kBakePixels;
		float cursorX = 0.0F;
		float cursorY = 0.0F;
		scratch_.clear();
		for (const char character : text)
		{
			if (character < kFirstChar || character >= kFirstChar + kNumChars)
			{
				continue;
			}
			stbtt_aligned_quad quad{};
			stbtt_GetBakedQuad(bakedChars_.data(), kAtlasSize, kAtlasSize, character - kFirstChar, &cursorX, &cursorY, &quad, 1);
			const glm::vec2 origin = position + glm::vec2(0.0F, fontAscent_ * factor);
			const glm::vec2 a = origin + glm::vec2(quad.x0, quad.y0) * factor;
			const glm::vec2 c = origin + glm::vec2(quad.x1, quad.y1) * factor;
			pushQuad(scratch_, a, {c.x, a.y}, c, {a.x, c.y}, {quad.s0, quad.t0}, {quad.s1, quad.t0}, {quad.s1, quad.t1},
				{quad.s0, quad.t1});
		}
		draw(scratch_, Mode::Font, color, fontTexture_);
	}

	glm::vec2 RuntimeHud::textSize(const std::string& text, const float scale)
	{
		const float factor = kUiPixels * scale / kBakePixels;
		float width = 0.0F;
		for (const char character : text)
		{
			if (character >= kFirstChar && character < kFirstChar + kNumChars)
			{
				width += bakedChars_[static_cast<std::size_t>(character - kFirstChar)].xadvance;
			}
		}
		return {width * factor, kUiPixels * scale * 1.15F};
	}

	GLuint RuntimeHud::imageTexture(const std::string& projectRelativePath)
	{
		const auto found = images_.find(projectRelativePath);
		if (found != images_.end())
		{
			return found->second;
		}
		GLuint texture = 0;
		// Same confinement as every other project asset path.
		if (const std::optional<std::filesystem::path> path = core::resolveProjectFile(
				projectRoot_, projectRelativePath, "Game", {".png", ".jpg", ".jpeg", ".bmp", ".tga"}))
		{
			const LoadedTexture image = loadTextureImageTopDown(*path);
			if (image.success)
			{
				glGenTextures(1, &texture);
				glBindTexture(GL_TEXTURE_2D, texture);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
					image.rgba.data());
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			}
		}
		images_[projectRelativePath] = texture;
		return texture;
	}

	void RuntimeHud::image(const std::string& projectRelativePath, const glm::vec2& min, const glm::vec2& max)
	{
		const GLuint texture = imageTexture(projectRelativePath);
		if (texture == 0)
		{
			return;
		}
		scratch_.clear();
		pushQuad(scratch_, min, {max.x, min.y}, max, {min.x, max.y});
		draw(scratch_, Mode::Image, {1.0F, 1.0F, 1.0F, 1.0F}, texture);
	}
}
