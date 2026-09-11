#include "GameForger/Editor/OverlayFont.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>

#include "stb_truetype.h"

namespace gameforger::editor
{
	float OverlayFont::measure(const std::string& text, const float pixelHeight) const
	{
		if (!success || pixelHeight <= 0.0F)
		{
			return 0.0F;
		}
		const float scale = pixelHeight / kBakedPixelHeight;
		float width = 0.0F;
		for (const char character : text)
		{
			const int index = static_cast<int>(static_cast<unsigned char>(character)) - kFirstChar;
			if (index < 0 || index >= kCharCount)
			{
				continue;
			}
			width += glyphs[static_cast<std::size_t>(index)].xAdvance * scale;
		}
		return width;
	}

	OverlayFont bakeOverlayFont(const std::filesystem::path& fontPath)
	{
		OverlayFont font;

		std::ifstream file(fontPath, std::ios::binary);
		if (!file)
		{
			// Not an error worth failing startup over - the caller draws no
			// text and everything else still works.
			return font;
		}
		const std::vector<unsigned char> fontBuffer(
			(std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if (fontBuffer.empty())
		{
			return font;
		}

		font.alphaPixels.assign(
			static_cast<std::size_t>(OverlayFont::kAtlasWidth) * OverlayFont::kAtlasHeight, 0);
		std::array<stbtt_bakedchar, OverlayFont::kCharCount> baked{};
		const int bakeResult = stbtt_BakeFontBitmap(
			fontBuffer.data(), 0, OverlayFont::kBakedPixelHeight, font.alphaPixels.data(),
			OverlayFont::kAtlasWidth, OverlayFont::kAtlasHeight, OverlayFont::kFirstChar,
			OverlayFont::kCharCount, baked.data());
		if (bakeResult <= 0)
		{
			// stb_truetype only handles TrueType-flavoured outlines, not
			// CFF/PostScript OTF. Say which, because "my font did nothing" is
			// otherwise impossible to diagnose.
			std::fprintf(
				stderr,
				"OverlayFont: could not bake %s - stb_truetype supports TrueType-flavoured outlines only, "
				"not CFF/PostScript OTF. HUD text will not draw.\n",
				fontPath.string().c_str());
			font.alphaPixels.clear();
			return font;
		}

		// Convert stb's integer atlas rects into the normalised UVs and pixel
		// offsets the renderer wants, once here rather than per draw call.
		constexpr float kInvWidth = 1.0F / static_cast<float>(OverlayFont::kAtlasWidth);
		constexpr float kInvHeight = 1.0F / static_cast<float>(OverlayFont::kAtlasHeight);
		for (std::size_t index = 0; index < baked.size(); ++index)
		{
			const stbtt_bakedchar& source = baked[index];
			OverlayGlyph& glyph = font.glyphs[index];
			glyph.u0 = static_cast<float>(source.x0) * kInvWidth;
			glyph.v0 = static_cast<float>(source.y0) * kInvHeight;
			glyph.u1 = static_cast<float>(source.x1) * kInvWidth;
			glyph.v1 = static_cast<float>(source.y1) * kInvHeight;
			glyph.width = static_cast<float>(source.x1 - source.x0);
			glyph.height = static_cast<float>(source.y1 - source.y0);
			glyph.xOffset = source.xoff;
			glyph.yOffset = source.yoff;
			glyph.xAdvance = source.xadvance;
		}
		font.success = true;
		return font;
	}
}
