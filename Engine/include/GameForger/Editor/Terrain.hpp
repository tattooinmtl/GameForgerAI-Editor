#pragma once

#include <vector>

namespace gameforger::editor
{
	// Triangulates a `resolution` x `resolution` heightmap grid into a
	// position(3) + normal(3) + splatWeight(3) per-vertex mesh (9 floats/
	// vertex - the splat weight rides along as a third interleaved
	// attribute rather than a separately-sampled texture, since it's
	// already on the exact same resolution x resolution grid as the
	// heights it's painted alongside). The grid spans
	// [-worldSize/2, worldSize/2] in local X/Z (so the entity's pivot sits
	// at the terrain's horizontal center, like every other primitive).
	// heights[] is 0..1 with 0.5 as the flat baseline (local Y=0);
	// world-space height = (heights[row * resolution + col] - 0.5) *
	// heightScale, so a value of 1.0 reaches +heightScale/2 and 0.0 reaches
	// -heightScale/2. Per-vertex normals are estimated from neighboring
	// heights (central differences), so lighting responds to slope instead
	// of every triangle being flat-shaded.
	//
	// `splatWeights` is resolution*resolution*3 values (R,G,B per texel,
	// see TerrainData::splatWeights) - if empty or the wrong size, every
	// vertex defaults to full weight on layer 0 (a fresh/legacy terrain
	// with no real paint data yet renders as pure layer 0, not garbage).
	[[nodiscard]] std::vector<float> buildTerrainMesh(
		int resolution,
		float worldSize,
		float heightScale,
		const std::vector<float>& heights,
		const std::vector<float>& splatWeights = {});

	// Bilinearly samples the same heightmap at local-space (localX, localZ),
	// returning local-space Y (see buildTerrainMesh's doc comment for the
	// heights[]-to-world-Y mapping) - the single source of truth for "what
	// height is the terrain surface at this XZ", shared by the sculpt
	// brush's raycast (main.cpp) and terrain-aware physics grounding
	// (Collision.cpp) so both agree with what actually got rendered.
	[[nodiscard]] float sampleTerrainHeight(
		int resolution, float worldSize, float heightScale, const std::vector<float>& heights, float localX,
		float localZ);
}
