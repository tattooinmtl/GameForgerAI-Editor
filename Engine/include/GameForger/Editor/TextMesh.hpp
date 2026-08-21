#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace gameforger::editor
{
	struct TextMeshBuildResult
	{
		bool success = false;
		std::string message;
		std::vector<float> vertices; // position(3) + normal(3) per vertex, matches PrimitiveMeshData
	};

	// Builds an extruded 3D mesh for `utf8Text` using the TrueType/OpenType
	// font at `fontFilePath`. `worldSize` is the font's em height in world
	// units; `depth` is the extrusion thickness in world units. The first
	// line's baseline runs along local +X starting at the local origin, with
	// the glyphs rising along +Y - matches how text is normally authored, so
	// a TextMesh's pivot sits at the start of the first line's baseline.
	// '\n' starts a new line below the previous one, using the font's own
	// line-height metrics.
	//
	// Each glyph's outline (from stb_truetype) is triangulated with proper
	// hole support (e.g. the counters of 'A', 'O', '8') via hole-bridging +
	// ear clipping, then extruded into a front face, back face, and side
	// walls. This is real per-glyph geometry, not billboarded quads - no
	// rotation-aware constraints, it's a genuine 3D mesh like any primitive.
	[[nodiscard]] TextMeshBuildResult buildTextMesh(
		const std::filesystem::path& fontFilePath,
		const std::string& utf8Text,
		float worldSize,
		float depth);
}
