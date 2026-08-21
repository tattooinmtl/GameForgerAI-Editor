#include "GameForger/Editor/Terrain.hpp"

#include <algorithm>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

namespace gameforger::editor
{
	namespace
	{
		void pushVertex(
			std::vector<float>& out, const glm::vec3& position, const glm::vec3& normal, const glm::vec3& splat)
		{
			out.push_back(position.x);
			out.push_back(position.y);
			out.push_back(position.z);
			out.push_back(normal.x);
			out.push_back(normal.y);
			out.push_back(normal.z);
			out.push_back(splat.x);
			out.push_back(splat.y);
			out.push_back(splat.z);
		}
	}

	std::vector<float> buildTerrainMesh(
		const int resolution,
		const float worldSize,
		const float heightScale,
		const std::vector<float>& heights,
		const std::vector<float>& splatWeights)
	{
		std::vector<float> vertices;
		if (resolution < 2 || static_cast<int>(heights.size()) != resolution * resolution)
		{
			return vertices;
		}
		const bool hasSplatWeights = static_cast<int>(splatWeights.size()) == resolution * resolution * 3;

		const float cellSpacing = worldSize / static_cast<float>(resolution - 1);
		const float half = worldSize * 0.5F;

		const auto heightAt = [&](const int row, const int col) -> float
		{
			const int clampedRow = std::clamp(row, 0, resolution - 1);
			const int clampedCol = std::clamp(col, 0, resolution - 1);
			return heights[static_cast<std::size_t>(clampedRow) * static_cast<std::size_t>(resolution) +
				static_cast<std::size_t>(clampedCol)];
		};
		const auto splatAt = [&](const int row, const int col) -> glm::vec3
		{
			if (!hasSplatWeights)
			{
				return glm::vec3(1.0F, 0.0F, 0.0F);
			}
			const int clampedRow = std::clamp(row, 0, resolution - 1);
			const int clampedCol = std::clamp(col, 0, resolution - 1);
			const std::size_t base = (static_cast<std::size_t>(clampedRow) * static_cast<std::size_t>(resolution) +
				static_cast<std::size_t>(clampedCol)) *
				3;
			return glm::vec3(splatWeights[base], splatWeights[base + 1], splatWeights[base + 2]);
		};
		// Row increases toward -Z (matches PrimitiveMeshes' generatePlane
		// winding: going a->b along +X then a->d along -Z produces an
		// upward-facing CCW quad).
		// heights[] is 0..1 with 0.5 as the flat/sea-level baseline, so a
		// freshly created (all-0.5) terrain sits exactly at local y=0 and has
		// equal headroom to sculpt upward (toward 1.0) or downward into
		// canyons/riverbeds/lake bottoms (toward 0.0).
		const auto positionAt = [&](const int row, const int col) -> glm::vec3
		{
			return glm::vec3(
				static_cast<float>(col) * cellSpacing - half,
				(heightAt(row, col) - 0.5F) * heightScale,
				half - static_cast<float>(row) * cellSpacing);
		};
		const auto normalAt = [&](const int row, const int col) -> glm::vec3
		{
			const float left = heightAt(row, col - 1) * heightScale;
			const float right = heightAt(row, col + 1) * heightScale;
			const float down = heightAt(row + 1, col) * heightScale;
			const float up = heightAt(row - 1, col) * heightScale;
			return glm::normalize(glm::vec3(left - right, 2.0F * cellSpacing, down - up));
		};

		vertices.reserve(
			static_cast<std::size_t>(resolution - 1) * static_cast<std::size_t>(resolution - 1) * 6 * 9);
		for (int row = 0; row < resolution - 1; ++row)
		{
			for (int col = 0; col < resolution - 1; ++col)
			{
				const glm::vec3 a = positionAt(row, col);
				const glm::vec3 b = positionAt(row, col + 1);
				const glm::vec3 c = positionAt(row + 1, col + 1);
				const glm::vec3 d = positionAt(row + 1, col);
				const glm::vec3 na = normalAt(row, col);
				const glm::vec3 nb = normalAt(row, col + 1);
				const glm::vec3 nc = normalAt(row + 1, col + 1);
				const glm::vec3 nd = normalAt(row + 1, col);
				const glm::vec3 sa = splatAt(row, col);
				const glm::vec3 sb = splatAt(row, col + 1);
				const glm::vec3 sc = splatAt(row + 1, col + 1);
				const glm::vec3 sd = splatAt(row + 1, col);

				pushVertex(vertices, a, na, sa);
				pushVertex(vertices, b, nb, sb);
				pushVertex(vertices, c, nc, sc);

				pushVertex(vertices, a, na, sa);
				pushVertex(vertices, c, nc, sc);
				pushVertex(vertices, d, nd, sd);
			}
		}

		return vertices;
	}

	float sampleTerrainHeight(
		const int resolution,
		const float worldSize,
		const float heightScale,
		const std::vector<float>& heights,
		const float localX,
		const float localZ)
	{
		if (resolution < 2 || static_cast<int>(heights.size()) != resolution * resolution)
		{
			return 0.0F;
		}
		const float half = worldSize * 0.5F;
		const float u = std::clamp((localX + half) / worldSize, 0.0F, 1.0F);
		// Rows increase toward -Z (see buildTerrainMesh's positionAt), so
		// v=0 is the +Z edge.
		const float v = std::clamp((half - localZ) / worldSize, 0.0F, 1.0F);
		const float colF = u * static_cast<float>(resolution - 1);
		const float rowF = v * static_cast<float>(resolution - 1);
		const int col0 = static_cast<int>(colF);
		const int row0 = static_cast<int>(rowF);
		const int col1 = std::min(col0 + 1, resolution - 1);
		const int row1 = std::min(row0 + 1, resolution - 1);
		const float fc = colF - static_cast<float>(col0);
		const float fr = rowF - static_cast<float>(row0);
		const auto heightAt = [&](const int row, const int col)
		{
			return heights[static_cast<std::size_t>(row) * static_cast<std::size_t>(resolution) +
				static_cast<std::size_t>(col)];
		};
		const float top = heightAt(row0, col0) + (heightAt(row0, col1) - heightAt(row0, col0)) * fc;
		const float bottom = heightAt(row1, col0) + (heightAt(row1, col1) - heightAt(row1, col0)) * fc;
		return ((top + (bottom - top) * fr) - 0.5F) * heightScale;
	}
}
