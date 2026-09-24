#pragma once

#include <filesystem>
#include <vector>

namespace gameforger::editor
{
	struct LoadedTexture
	{
		bool success = false;
		int width = 0;
		int height = 0;
		std::vector<unsigned char> rgba; // width*height*4 bytes, row-major top-to-bottom
	};

	// Loads any stb_image-supported image file as RGBA8 - the shared CPU-side
	// loader behind every terrain splat layer's diffuse/normal/height texture
	// (see TerrainLayerData, EditorScene.hpp). GPU upload happens separately
	// in the renderer, same "pure CPU builder, renderer owns the GL object"
	// split as ModelImport.hpp/TextMesh.hpp.
	[[nodiscard]] LoadedTexture loadTextureImage(const std::filesystem::path& filePath);

	// Same, but rows in top-to-bottom order (undoing the process-wide
	// stbi_set_flip_vertically_on_load) - for 2D UI images drawn with a
	// top-left texture origin (ImGui::Image, the runtime HUD): icons, logos.
	[[nodiscard]] LoadedTexture loadTextureImageTopDown(const std::filesystem::path& filePath);

	// Derives a normal map from a color image's own luminance (bright =
	// high, dark = low) via a central-difference pass, encoded the standard
	// way (RGB = normal.xyz * 0.5 + 0.5) - the fallback used when a
	// TerrainLayerData's normalPath is left empty, mirroring Unity's
	// TerrainLayer "generate from albedo" behavior so a layer works
	// immediately with just a diffuse texture and can be upgraded later by
	// filling in a real authored map.
	[[nodiscard]] LoadedTexture generateNormalMapFromDiffuse(const LoadedTexture& diffuse, float strength = 2.0F);

	// Same fallback idea for the height slot: the diffuse image's own
	// luminance, replicated across RGB, used directly as a grayscale height
	// map when heightPath is left empty.
	[[nodiscard]] LoadedTexture generateHeightMapFromDiffuse(const LoadedTexture& diffuse);
}
