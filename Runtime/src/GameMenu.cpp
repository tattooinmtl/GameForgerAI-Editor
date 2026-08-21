#include "GameMenu.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#include <GLFW/glfw3.h>

#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>

namespace gameforger::editor
{
	namespace
	{
		constexpr const char* vertexShaderSource = R"glsl(
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

		// Draws either a flat-tinted rect (useTexture=0, ignores fragUv) or
		// a glyph sampled from the baked font atlas modulated by tintColor
		// (useTexture=1) - one shader for both, same trick SplashScreen.cpp
		// uses a dedicated one for since it only ever draws textured quads;
		// this menu draws far more solid rects (backgrounds/sliders/
		// buttons) than glyphs, so unifying avoids a second program.
		constexpr const char* fragmentShaderSource = R"glsl(
#version 460 core
in vec2 fragUv;
out vec4 fragColor;
uniform sampler2D atlas;
uniform vec4 tintColor;
uniform int useTexture;

void main()
{
	float alpha = useTexture != 0 ? texture(atlas, fragUv).r : 1.0;
	fragColor = vec4(tintColor.rgb, tintColor.a * alpha);
}
)glsl";

		GLuint compileShader(const GLenum type, const char* source, const char* label)
		{
			const GLuint shader = glCreateShader(type);
			glShaderSource(shader, 1, &source, nullptr);
			glCompileShader(shader);
			GLint success = GL_FALSE;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
			if (success == GL_FALSE)
			{
				std::array<char, 1024> log{};
				glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
				std::fprintf(stderr, "GameMenu %s shader compilation failed: %s\n", label, log.data());
			}
			return shader;
		}
	}

	GameMenu::~GameMenu()
	{
		shutdown();
	}

	bool GameMenu::initialize(const std::filesystem::path& fontPath)
	{
		const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource, "vertex");
		const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource, "fragment");
		shaderProgram_ = glCreateProgram();
		glAttachShader(shaderProgram_, vertexShader);
		glAttachShader(shaderProgram_, fragmentShader);
		glLinkProgram(shaderProgram_);
		glDeleteShader(vertexShader);
		glDeleteShader(fragmentShader);

		GLint linkSuccess = GL_FALSE;
		glGetProgramiv(shaderProgram_, GL_LINK_STATUS, &linkSuccess);
		if (linkSuccess == GL_FALSE)
		{
			std::fprintf(stderr, "GameMenu shader program failed to link.\n");
			return false;
		}
		// Cache uniform locations once; drawCenteredBanner() needs them too
		// and would otherwise re-fetch the same handles every frame.
		tintColorLocation_ = glGetUniformLocation(shaderProgram_, "tintColor");
		useTextureLocation_ = glGetUniformLocation(shaderProgram_, "useTexture");
		atlasLocation_ = glGetUniformLocation(shaderProgram_, "atlas");
		projectionLocation_ = glGetUniformLocation(shaderProgram_, "projection");

		glGenVertexArrays(1, &vertexArray_);
		glGenBuffers(1, &vertexBuffer_);
		glBindVertexArray(vertexArray_);
		glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
		// 6 vertices/quad, position(2)+uv(2) each - re-uploaded via
		// glBufferSubData before every single quad draw call (this menu is
		// only ever a handful of quads drawn while paused, not a
		// performance-sensitive path).
		glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<const void*>(2 * sizeof(float)));
		glEnableVertexAttribArray(1);
		glBindVertexArray(0);

		// Font atlas - graceful degradation if this fails (unsupported OTF
		// flavor, missing file, etc.): fontLoaded_ stays false, drawText()
		// then just silently draws nothing rather than the whole menu
		// failing to initialize - buttons/sliders still work via their
		// colored rects with no labels. See the class doc comment.
		std::ifstream file(fontPath, std::ios::binary);
		if (!file)
		{
			std::fprintf(stderr, "GameMenu: could not open font file: %s\n", fontPath.string().c_str());
		}
		else
		{
			const std::vector<unsigned char> fontBuffer(
				(std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			std::vector<unsigned char> atlasPixels(static_cast<std::size_t>(kAtlasWidth) * kAtlasHeight);
			const int bakeResult = stbtt_BakeFontBitmap(
				fontBuffer.data(), 0, 28.0F, atlasPixels.data(), kAtlasWidth, kAtlasHeight, kFirstChar, kNumChars,
				bakedChars_.data());
			if (bakeResult <= 0)
			{
				std::fprintf(
					stderr, "GameMenu: font baking failed for %s (stb_truetype only supports TrueType-flavored "
					"outlines, not CFF/PostScript OTF) - menu text will be blank.\n",
					fontPath.string().c_str());
			}
			else
			{
				glGenTextures(1, &fontTexture_);
				glBindTexture(GL_TEXTURE_2D, fontTexture_);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
				glTexImage2D(
					GL_TEXTURE_2D, 0, GL_R8, kAtlasWidth, kAtlasHeight, 0, GL_RED, GL_UNSIGNED_BYTE,
					atlasPixels.data());
				glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
				fontLoaded_ = true;
			}
		}

		return true;
	}

	void GameMenu::shutdown() noexcept
	{
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
		if (fontTexture_ != 0)
		{
			glDeleteTextures(1, &fontTexture_);
			fontTexture_ = 0;
		}
		if (shaderProgram_ != 0)
		{
			glDeleteProgram(shaderProgram_);
			shaderProgram_ = 0;
		}
		fontLoaded_ = false;
	}

	namespace
	{
		void uploadQuad(
			const GLuint vertexArray, const GLuint vertexBuffer, const float x, const float y, const float w,
			const float h, const float u0, const float v0, const float u1, const float v1)
		{
			const std::array<float, 24> vertices{
				x,     y,     u0, v0,
				x + w, y,     u1, v0,
				x + w, y + h, u1, v1,
				x,     y,     u0, v0,
				x + w, y + h, u1, v1,
				x,     y + h, u0, v1,
			};
			glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
			glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data());
			glBindVertexArray(vertexArray);
			glDrawArrays(GL_TRIANGLES, 0, 6);
		}
	}

	bool GameMenu::render(
		GLFWwindow* window, const int windowWidth, const int windowHeight, bool& open, float& mouseSensitivity,
		int& targetFps, bool& quitRequested, bool& saveRequested, bool& loadRequested)
	{
		if (!open || windowWidth <= 0 || windowHeight <= 0 || shaderProgram_ == 0)
		{
			activeSliderId_ = -1;
			// Reset click-edge state on every early-out too. The static
			// `mouseWasDown` below is function-scoped (function-static in
			// C++), so without this reset the value from the last
			// menu-open frame would persist across a long pause, then
			// fire a phantom "click" the next time the user opened the
			// menu while still holding LMB from elsewhere on screen.
			mouseWasDown_ = false;
			return false;
		}

		double mouseX = 0.0;
		double mouseY = 0.0;
		glfwGetCursorPos(window, &mouseX, &mouseY);
		const bool mouseDownNow = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
		// Own edge-tracking (this class's whole reason to exist is that
		// nothing else in Runtime tracks 2D mouse-button-click edges -
		// GlfwInputSource only covers keyboard). Promoted from a function-
		// static to a member field so the early-out above can reset it.
		const bool mouseClicked = mouseDownNow && !mouseWasDown_;
		const bool mouseJustReleased = !mouseDownNow && mouseWasDown_;
		mouseWasDown_ = mouseDownNow;

		const GLint projectionLocation = projectionLocation_;
		const GLint tintColorLocation = tintColorLocation_;
		const GLint useTextureLocation = useTextureLocation_;
		const GLint atlasLocation = atlasLocation_;

		const auto drawSolidRect =
			[&](const float x, const float y, const float w, const float h, const float r, const float g,
				const float b, const float a)
		{
			glUseProgram(shaderProgram_);
			glUniform4f(tintColorLocation, r, g, b, a);
			glUniform1i(useTextureLocation, 0);
			uploadQuad(vertexArray_, vertexBuffer_, x, y, w, h, 0.0F, 0.0F, 1.0F, 1.0F);
		};

		const auto drawText = [&](const std::string& text, float x, const float y, const float r, const float g,
								   const float b, const float a)
		{
			if (!fontLoaded_)
			{
				return;
			}
			glUseProgram(shaderProgram_);
			glUniform4f(tintColorLocation, r, g, b, a);
			glUniform1i(useTextureLocation, 1);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, fontTexture_);
			glUniform1i(atlasLocation, 0);
			float cursorX = x;
			float cursorY = y;
			for (const char character : text)
			{
				if (character < kFirstChar || character >= kFirstChar + kNumChars)
				{
					continue;
				}
				stbtt_aligned_quad quad{};
				stbtt_GetBakedQuad(
					bakedChars_.data(), kAtlasWidth, kAtlasHeight, character - kFirstChar, &cursorX, &cursorY, &quad,
					1);
				uploadQuad(
					vertexArray_, vertexBuffer_, quad.x0, quad.y0, quad.x1 - quad.x0, quad.y1 - quad.y0, quad.s0,
					quad.t0, quad.s1, quad.t1);
			}
		};

		const auto button = [&](const std::string& label, const float x, const float y, const float w,
								 const float h) -> bool
		{
			const bool hovered = mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h;
			drawSolidRect(
				x, y, w, h, hovered ? 0.35F : 0.20F, hovered ? 0.38F : 0.22F, hovered ? 0.45F : 0.28F, 0.95F);
			drawText(label, x + 16.0F, y + h * 0.5F - 9.0F, 1.0F, 1.0F, 1.0F, 1.0F);
			return hovered && mouseClicked;
		};

		// Stateless/immediate-mode: recomputes value directly from mouse-X
		// every frame the mouse is held down over the track (or was
		// already dragging this exact slider, even if the cursor has since
		// drifted outside the track's vertical bounds) - same idea ImGui's
		// own SliderFloat uses. `id` disambiguates which slider currently
		// owns the drag, so two sliders don't fight over activeSliderId_.
		const auto slider = [&](const int id, const char* label, const float x, const float y, const float w,
								 const float h, const float minValue, const float maxValue, float& value) -> bool
		{
			const bool hoveredTrack =
				mouseX >= x - 8.0 && mouseX <= x + w + 8.0 && mouseY >= y - 10.0 && mouseY <= y + h + 10.0;
			if (mouseDownNow && activeSliderId_ == -1 && hoveredTrack)
			{
				activeSliderId_ = id;
			}
			if (activeSliderId_ == id && mouseDownNow)
			{
				const float t = glm::clamp(static_cast<float>((mouseX - x) / static_cast<double>(w)), 0.0F, 1.0F);
				value = minValue + t * (maxValue - minValue);
			}
			const bool committed = activeSliderId_ == id && mouseJustReleased;
			if (committed)
			{
				activeSliderId_ = -1;
			}

			drawSolidRect(x, y, w, h, 0.16F, 0.16F, 0.20F, 0.9F);
			const float t = glm::clamp((value - minValue) / (maxValue - minValue), 0.0F, 1.0F);
			constexpr float handleWidth = 10.0F;
			drawSolidRect(x + t * w - handleWidth * 0.5F, y - 4.0F, handleWidth, h + 8.0F, 0.85F, 0.65F, 0.20F, 1.0F);
			drawText(label, x, y - 22.0F, 0.85F, 0.9F, 1.0F, 1.0F);

			return committed;
		};

		glDisable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glUseProgram(shaderProgram_);
		const glm::mat4 projection =
			glm::ortho(0.0F, static_cast<float>(windowWidth), static_cast<float>(windowHeight), 0.0F, -1.0F, 1.0F);
		glUniformMatrix4fv(projectionLocation, 1, GL_FALSE, glm::value_ptr(projection));

		// Dim overlay behind the panel so the paused game scene stays
		// visible-but-secondary underneath, the standard pause-menu look.
		drawSolidRect(0.0F, 0.0F, static_cast<float>(windowWidth), static_cast<float>(windowHeight), 0.0F, 0.0F, 0.0F, 0.55F);

		constexpr float panelWidth = 420.0F;
		const float panelX = (static_cast<float>(windowWidth) - panelWidth) * 0.5F;
		float cursorY = static_cast<float>(windowHeight) * 0.5F - 190.0F;

		drawText("Paused", panelX, cursorY, 1.0F, 1.0F, 1.0F, 1.0F);
		cursorY += 60.0F;

		bool committedAny = slider(0, "Mouse Sensitivity", panelX, cursorY, panelWidth, 10.0F, 0.02F, 0.50F, mouseSensitivity);
		cursorY += 70.0F;

		float targetFpsFloat = static_cast<float>(targetFps);
		committedAny = slider(1, "Target FPS (30-90)", panelX, cursorY, panelWidth, 10.0F, 30.0F, 90.0F, targetFpsFloat) || committedAny;
		targetFps = static_cast<int>(targetFpsFloat + 0.5F);
		cursorY += 80.0F;

		constexpr float buttonWidth = 190.0F;
		constexpr float buttonHeight = 46.0F;
		constexpr float buttonRowSpacing = 16.0F;

		// Save/Load only signal intent here - this class has no access to
		// the scene/script runtime to actually perform them, see the
		// header's own doc comment.
		if (button("Save", panelX, cursorY, buttonWidth, buttonHeight))
		{
			saveRequested = true;
		}
		if (button("Load", panelX + panelWidth - buttonWidth, cursorY, buttonWidth, buttonHeight))
		{
			loadRequested = true;
		}
		cursorY += buttonHeight + buttonRowSpacing;

		if (button("Resume", panelX, cursorY, buttonWidth, buttonHeight))
		{
			open = false;
		}
		if (button("Quit", panelX + panelWidth - buttonWidth, cursorY, buttonWidth, buttonHeight))
		{
			quitRequested = true;
		}

		glBindVertexArray(0);
		glUseProgram(0);
		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);

		return committedAny;
	}

	void GameMenu::drawCenteredBanner(const int windowWidth, const int windowHeight, const std::string& text)
	{
		if (shaderProgram_ == 0 || windowWidth <= 0 || windowHeight <= 0 || text.empty())
		{
			return;
		}

		// Same shader setup the pause menu uses (orthographic full-window
		// projection, no depth, alpha-blended quads). Disabling depth lets
		// the banner sit on top of the rendered game scene without z-fight;
		// alpha-blending is required because the atlas is a sampled texture.
		glDisable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glUseProgram(shaderProgram_);
		const glm::mat4 projection = glm::ortho(
			0.0F, static_cast<float>(windowWidth),
			static_cast<float>(windowHeight), 0.0F,
			-1.0F, 1.0F);
		glUniformMatrix4fv(projectionLocation_, 1, GL_FALSE, glm::value_ptr(projection));

		// Measure the text width in pixels so we can center both the text
		// and its background plate.
		const float textScale = 3.0F;
		float textWidth = 0.0F;
		if (fontLoaded_)
		{
			float measureX = 0.0F;
			float measureY = 0.0F;
			for (const char character : text)
			{
				if (character < kFirstChar || character >= kFirstChar + kNumChars)
				{
					continue;
				}
				stbtt_aligned_quad measureQuad{};
				stbtt_GetBakedQuad(
					bakedChars_.data(), kAtlasWidth, kAtlasHeight, character - kFirstChar, &measureX, &measureY,
					&measureQuad, textScale);
			}
			textWidth = measureX;
		}

		// Background plate: a translucent dark band behind the text so it
		// stays readable over any scene background.
		constexpr float platePaddingX = 32.0F;
		constexpr float platePaddingY = 16.0F;
		constexpr float charHeight = 18.0F;
		const float textHeight = charHeight * textScale;
		const float plateWidth = textWidth + platePaddingX * 2.0F;
		const float plateHeight = textHeight + platePaddingY * 2.0F;
		const float plateX = (static_cast<float>(windowWidth) - plateWidth) * 0.5F;
		const float plateY = (static_cast<float>(windowHeight) - plateHeight) * 0.5F;
		glUniform4f(tintColorLocation_, 0.05F, 0.05F, 0.05F, 0.80F);
		glUniform1i(useTextureLocation_, 0);
		uploadQuad(vertexArray_, vertexBuffer_, plateX, plateY, plateWidth, plateHeight, 0.0F, 0.0F, 1.0F, 1.0F);

		// Foreground text - same shape as drawText() inside render() but
		// uses the cached uniform locations instead of re-fetching them.
		if (fontLoaded_)
		{
			glUniform4f(tintColorLocation_, 1.0F, 0.92F, 0.55F, 1.0F);
			glUniform1i(useTextureLocation_, 1);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, fontTexture_);
			glUniform1i(atlasLocation_, 0);
			const float cursorXStart = (static_cast<float>(windowWidth) - textWidth) * 0.5F;
			float cursorX = cursorXStart;
			float cursorY = (static_cast<float>(windowHeight) - textHeight) * 0.5F;
			for (const char character : text)
			{
				if (character < kFirstChar || character >= kFirstChar + kNumChars)
				{
					continue;
				}
				stbtt_aligned_quad quad{};
				stbtt_GetBakedQuad(
					bakedChars_.data(), kAtlasWidth, kAtlasHeight, character - kFirstChar, &cursorX, &cursorY,
					&quad, textScale);
				uploadQuad(
					vertexArray_, vertexBuffer_, quad.x0, quad.y0, quad.x1 - quad.x0, quad.y1 - quad.y0,
					quad.s0, quad.t0, quad.s1, quad.t1);
			}
		}

		glBindVertexArray(0);
		glUseProgram(0);
		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);
	}
}
