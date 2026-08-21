#pragma once

#include <vector>

#include "GameForger/Editor/AICommand.hpp"

namespace gameforger::editor
{
	// Interleaved position(3) + normal(3) triangle list, non-indexed.
	// Every primitive is generated so it fits inside the [-1, 1] cube,
	// which lets picking use a single local-space AABB test for all shapes.
	struct PrimitiveMeshData
	{
		std::vector<float> vertices;
	};

	[[nodiscard]] PrimitiveMeshData generatePrimitiveMesh(PrimitiveType type);
}
