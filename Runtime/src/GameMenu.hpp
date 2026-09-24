#pragma once

#include <array>
#include <filesystem>

#include <glad/gl.h>

#include "stb_truetype.h"

struct GLFWwindow;

namespace gameforger::editor
{
	// Runtime's own Esc-to-pause menu - genuinely exclusive to
	// GameForgerRuntime (the Editor has ImGui for this, GameForgerRuntime
	// has no UI toolkit at all otherwise), so this is a small, self-
	// contained 2D screen-space UI layer built directly on stb_truetype's
	// simpler baked-bitmap-font API (distinct from the glyph-triangulation
	// API Engine/Editor's TextMesh.cpp uses for in-world 3D text) plus one
	// flat/textured-quad GL shader, in the same "own GL setup, no external
	// UI dependency" spirit as SplashScreen.cpp.
	class GameMenu final
	{
	public:
		GameMenu() = default;
		~GameMenu();

		GameMenu(const GameMenu&) = delete;
		GameMenu& operator=(const GameMenu&) = delete;

		// Bakes `fontPath` into a texture atlas and sets up the shader/quad
		// GPU objects. Returns false only on a GL/shader failure (a bad or
		// unsupported font file still returns true - see the .cpp comment
		// on graceful degradation: buttons/sliders stay usable via colored
		// rects even with no text labels).
		[[nodiscard]] bool initialize(const std::filesystem::path& fontPath);
		void shutdown() noexcept;
		// False if the font given to initialize() couldn't be baked (menu text
		// would be blank) - the caller can shutdown() and retry another font.
		[[nodiscard]] bool hasFont() const noexcept { return fontLoaded_; }

		// Draws the whole pause menu (dim overlay, title, two sliders,
		// Save/Load/Resume/Quit buttons) into whichever framebuffer is
		// currently bound, and applies any interaction directly to
		// mouseSensitivity/targetFps/quitRequested/saveRequested/
		// loadRequested - a complete no-op (no draw calls, no input read)
		// while `open` is false. `open` is in/out: the caller (Esc) opens
		// it, but the Resume button can also clear it directly. This class
		// only signals intent for Save/Load/Quit (it has no access to the
		// scene/script runtime to actually perform them) - the caller reads
		// those out-params after each call and does the real work. Returns
		// true the exact frame a slider's value was committed (mouse
		// released after dragging), so the caller knows when to persist
		// settings to disk instead of writing every frame while dragging.
		bool render(
			GLFWwindow* window,
			int windowWidth,
			int windowHeight,
			bool& open,
			float& mouseSensitivity,
			int& targetFps,
			bool& quitRequested,
			bool& saveRequested,
			bool& loadRequested);

		// Draws `text` as a large centered banner with a dim background plate,
		// into whichever framebuffer is currently bound. Independent of the
		// pause-menu state - safe to call every frame regardless of whether
		// the menu is open. Caller gates on its own condition (e.g. only when
		// gameplay.gameOverMessage is non-empty).
		void drawCenteredBanner(int windowWidth, int windowHeight, const std::string& text);

	private:
		GLuint shaderProgram_ = 0;
		GLuint vertexArray_ = 0;
		GLuint vertexBuffer_ = 0;
		GLint tintColorLocation_ = -1;
		GLint useTextureLocation_ = -1;
		GLint atlasLocation_ = -1;
		GLint projectionLocation_ = -1;

		GLuint fontTexture_ = 0;
		bool fontLoaded_ = false;
		static constexpr int kAtlasWidth = 512;
		static constexpr int kAtlasHeight = 512;
		static constexpr int kFirstChar = 32;
		static constexpr int kNumChars = 96;
		std::array<stbtt_bakedchar, kNumChars> bakedChars_{};

		// Slider drag state - which slider (if any) the mouse button went
		// down on top of, so a drag that moves outside the track's Y range
		// still keeps tracking that slider until release (matches how
		// every real slider widget behaves, not just "must stay directly
		// over the track pixel-for-pixel").
		int activeSliderId_ = -1;
		// Mouse-button edge-tracking state, reset to false in early-out
		// paths so a long pause with LMB held doesn't fire a phantom
		// click on the next menu-open frame.
		bool mouseWasDown_ = false;
	};
}
