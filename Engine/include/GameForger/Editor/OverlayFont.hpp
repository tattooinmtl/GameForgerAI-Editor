#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace gameforger::editor
{
	// A baked bitmap font atlas for screen-space text, built with
	// stb_truetype - the same approach Runtime's GameMenu and Engine's
	// TextMesh already use, extracted so the HUD overlay in ViewportRenderer
	// does not need a third copy of it.
	//
	// CPU-side only: this produces the pixels and the glyph metrics; the GPU
	// texture is the renderer's to own, matching this project's established
	// "pure CPU builder, renderer owns the GL object" split.
	//
	// Degrades quietly on failure. A missing file or a CFF/PostScript-flavoured
	// OTF (which stb_truetype cannot rasterise) leaves `success` false, and the
	// caller draws no text rather than failing to start.
	struct OverlayGlyph
	{
		// Atlas pixel rect.
		float u0 = 0.0F, v0 = 0.0F, u1 = 0.0F, v1 = 0.0F;
		// Offsets from the pen position, and how far to advance after.
		float xOffset = 0.0F, yOffset = 0.0F, xAdvance = 0.0F;
		float width = 0.0F, height = 0.0F;
	};

	struct OverlayFont
	{
		static constexpr int kAtlasWidth = 512;
		static constexpr int kAtlasHeight = 512;
		static constexpr int kFirstChar = 32;
		static constexpr int kCharCount = 96;
		// The size the atlas is rasterised at. Text drawn at other sizes is
		// scaled from this, so very large text softens - acceptable for a HUD,
		// and far cheaper than an atlas per size.
		static constexpr float kBakedPixelHeight = 48.0F;

		bool success = false;
		std::vector<unsigned char> alphaPixels; // kAtlasWidth * kAtlasHeight, single channel
		std::array<OverlayGlyph, kCharCount> glyphs{};

		// Width in pixels that `text` would occupy at `pixelHeight`.
		[[nodiscard]] float measure(const std::string& text, float pixelHeight) const;
	};

	// Bakes `fontPath`. Returns a font with success=false if it cannot.
	[[nodiscard]] OverlayFont bakeOverlayFont(const std::filesystem::path& fontPath);
}
