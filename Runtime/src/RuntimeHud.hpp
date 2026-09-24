#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <glad/gl.h>

#include "stb_truetype.h"

#include "GameForger/Runtime/GameplayHud.hpp"

namespace gameforger::editor
{
	// Fonts to try for runtime UI text, best first: any .ttf in the
	// project's Game/Fonts/, then common Windows system fonts. (stb_truetype
	// can't bake CFF-flavored .otf files such as the bundled display font,
	// which is why a TrueType fallback matters.)
	[[nodiscard]] std::vector<std::filesystem::path> runtimeFontCandidates(const std::filesystem::path& projectRoot);

	// GameForgerRuntime's implementation of the shared gameplay HUD canvas
	// (GameplayHud.hpp): immediate-mode 2D quads/lines/circles/text/images
	// in window pixels, drawn straight into the default framebuffer after
	// the 3D view is blitted. One tiny shader handles flat color, font-atlas
	// glyphs and RGBA images.
	class RuntimeHud final : public HudCanvas
	{
	public:
		RuntimeHud() = default;
		~RuntimeHud() override;
		RuntimeHud(const RuntimeHud&) = delete;
		RuntimeHud& operator=(const RuntimeHud&) = delete;

		[[nodiscard]] bool initialize(const std::filesystem::path& projectRoot);
		void shutdown() noexcept;
		[[nodiscard]] bool hasFont() const noexcept { return fontTexture_ != 0; }

		// Sets up 2D drawing over the whole window; call end() when done.
		void begin(int windowWidth, int windowHeight);
		void end();

		void line(const glm::vec2& from, const glm::vec2& to, const HudColor& color, float thickness) override;
		void rectFilled(const glm::vec2& min, const glm::vec2& max, const HudColor& color, float rounding) override;
		void rect(const glm::vec2& min, const glm::vec2& max, const HudColor& color, float rounding,
			float thickness) override;
		void circleFilled(const glm::vec2& center, float radius, const HudColor& color) override;
		void text(const glm::vec2& position, const HudColor& color, const std::string& text, float scale) override;
		[[nodiscard]] glm::vec2 textSize(const std::string& text, float scale) override;
		void image(const std::string& projectRelativePath, const glm::vec2& min, const glm::vec2& max) override;

	private:
		enum class Mode { Solid = 0, Font = 1, Image = 2 };
		// Vertices are x, y, u, v.
		void draw(const std::vector<float>& vertices, Mode mode, const HudColor& color, GLuint texture);
		[[nodiscard]] GLuint imageTexture(const std::string& projectRelativePath);

		std::filesystem::path projectRoot_;
		GLuint program_ = 0;
		GLuint vertexArray_ = 0;
		GLuint vertexBuffer_ = 0;
		GLint projectionLocation_ = -1;
		GLint colorLocation_ = -1;
		GLint modeLocation_ = -1;
		GLint samplerLocation_ = -1;

		static constexpr float kBakePixels = 32.0F;  // atlas glyph size
		static constexpr float kUiPixels = 17.0F;    // text() scale 1
		static constexpr int kAtlasSize = 1024;
		static constexpr int kFirstChar = 32;
		static constexpr int kNumChars = 96;
		GLuint fontTexture_ = 0;
		float fontAscent_ = 0.0F; // pixels at kBakePixels
		std::array<stbtt_bakedchar, kNumChars> bakedChars_{};

		std::unordered_map<std::string, GLuint> images_;
		std::vector<float> scratch_;
	};
}
