#include "GameForger/Editor/TextMesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <numeric>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "stb_truetype.h"

namespace gameforger::editor
{
	namespace
	{
		struct Contour
		{
			std::vector<glm::vec2> points;
			bool isHole = false;
			std::size_t parent = 0;
		};

		std::vector<unsigned int> decodeUtf8(const std::string& text)
		{
			std::vector<unsigned int> codepoints;
			codepoints.reserve(text.size());
			std::size_t index = 0;
			while (index < text.size())
			{
				const unsigned char first = static_cast<unsigned char>(text[index]);
				unsigned int codepoint = 0;
				std::size_t extraBytes = 0;
				if ((first & 0x80U) == 0x00U)
				{
					codepoint = first;
					extraBytes = 0;
				}
				else if ((first & 0xE0U) == 0xC0U)
				{
					codepoint = first & 0x1FU;
					extraBytes = 1;
				}
				else if ((first & 0xF0U) == 0xE0U)
				{
					codepoint = first & 0x0FU;
					extraBytes = 2;
				}
				else if ((first & 0xF8U) == 0xF0U)
				{
					codepoint = first & 0x07U;
					extraBytes = 3;
				}
				else
				{
					// Invalid leading byte - skip it.
					++index;
					continue;
				}

				++index;
				bool valid = true;
				for (std::size_t byteIndex = 0; byteIndex < extraBytes; ++byteIndex)
				{
					if (index >= text.size() || (static_cast<unsigned char>(text[index]) & 0xC0U) != 0x80U)
					{
						valid = false;
						break;
					}
					codepoint = (codepoint << 6U) | (static_cast<unsigned char>(text[index]) & 0x3FU);
					++index;
				}
				if (valid)
				{
					codepoints.push_back(codepoint);
				}
			}
			return codepoints;
		}

		float signedArea(const std::vector<glm::vec2>& points)
		{
			float area = 0.0F;
			for (std::size_t i = 0; i < points.size(); ++i)
			{
				const glm::vec2& a = points[i];
				const glm::vec2& b = points[(i + 1) % points.size()];
				area += a.x * b.y - b.x * a.y;
			}
			return area * 0.5F;
		}

		bool pointInPolygon(const glm::vec2& point, const std::vector<glm::vec2>& polygon)
		{
			bool inside = false;
			for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
			{
				const glm::vec2& pi = polygon[i];
				const glm::vec2& pj = polygon[j];
				if (((pi.y > point.y) != (pj.y > point.y)) &&
					(point.x < (pj.x - pi.x) * (point.y - pi.y) / (pj.y - pi.y) + pi.x))
				{
					inside = !inside;
				}
			}
			return inside;
		}

		float triangleSign(const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3)
		{
			return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
		}

		bool pointInTriangle(const glm::vec2& pt, const glm::vec2& v1, const glm::vec2& v2, const glm::vec2& v3)
		{
			const float d1 = triangleSign(pt, v1, v2);
			const float d2 = triangleSign(pt, v2, v3);
			const float d3 = triangleSign(pt, v3, v1);
			const bool hasNeg = (d1 < 0.0F) || (d2 < 0.0F) || (d3 < 0.0F);
			const bool hasPos = (d1 > 0.0F) || (d2 > 0.0F) || (d3 > 0.0F);
			return !(hasNeg && hasPos);
		}

		// Classifies each contour as solid or hole via point-in-polygon nesting
		// parity (a point nested inside an odd number of other contours is a
		// hole - this works regardless of the font's own winding convention),
		// finds each hole's immediate enclosing solid contour, then normalizes
		// winding so every solid contour is CCW and every hole is CW. Bridging
		// (mergeHoleIntoPolygon) depends on this normalization to produce a
		// consistently-wound simple polygon.
		void classifyAndNormalizeContours(std::vector<Contour>& contours)
		{
			std::vector<int> containment(contours.size(), 0);
			for (std::size_t i = 0; i < contours.size(); ++i)
			{
				if (contours[i].points.empty())
				{
					continue;
				}
				const glm::vec2 testPoint = contours[i].points.front();
				for (std::size_t j = 0; j < contours.size(); ++j)
				{
					if (i == j || contours[j].points.size() < 3)
					{
						continue;
					}
					if (pointInPolygon(testPoint, contours[j].points))
					{
						++containment[i];
					}
				}
			}
			for (std::size_t i = 0; i < contours.size(); ++i)
			{
				contours[i].isHole = (containment[i] % 2) == 1;
			}
			for (std::size_t i = 0; i < contours.size(); ++i)
			{
				if (!contours[i].isHole || contours[i].points.empty())
				{
					continue;
				}
				const glm::vec2 testPoint = contours[i].points.front();
				std::size_t bestParent = i;
				float bestArea = std::numeric_limits<float>::max();
				bool found = false;
				for (std::size_t j = 0; j < contours.size(); ++j)
				{
					if (i == j || contours[j].isHole || contours[j].points.size() < 3)
					{
						continue;
					}
					if (pointInPolygon(testPoint, contours[j].points))
					{
						const float area = std::abs(signedArea(contours[j].points));
						if (area < bestArea)
						{
							bestArea = area;
							bestParent = j;
							found = true;
						}
					}
				}
				contours[i].parent = found ? bestParent : i;
			}
			for (Contour& contour : contours)
			{
				const float area = signedArea(contour.points);
				if (contour.isHole && area > 0.0F)
				{
					std::reverse(contour.points.begin(), contour.points.end());
				}
				else if (!contour.isHole && area < 0.0F)
				{
					std::reverse(contour.points.begin(), contour.points.end());
				}
			}
		}

		// Finds the polygon vertex to bridge a hole into, following the
		// standard "hole elimination" technique: cast a ray from the hole's
		// rightmost point in +X, find the nearest edge it crosses, and pick
		// whichever of that edge's endpoints (or a vertex that blocks the
		// view of it) is safely visible from the hole point.
		std::size_t findBridgeIndex(const std::vector<glm::vec2>& polygon, const glm::vec2& holePoint)
		{
			float bestX = std::numeric_limits<float>::lowest();
			std::size_t bestEdgeStart = 0;
			bool found = false;
			for (std::size_t i = 0; i < polygon.size(); ++i)
			{
				const glm::vec2& a = polygon[i];
				const glm::vec2& b = polygon[(i + 1) % polygon.size()];
				if ((a.y > holePoint.y) != (b.y > holePoint.y))
				{
					const float t = (holePoint.y - a.y) / (b.y - a.y);
					const float x = a.x + t * (b.x - a.x);
					if (x >= holePoint.x && x > bestX)
					{
						bestX = x;
						bestEdgeStart = i;
						found = true;
					}
				}
			}
			if (!found)
			{
				return 0;
			}

			const std::size_t edgeEnd = (bestEdgeStart + 1) % polygon.size();
			std::size_t candidate = polygon[bestEdgeStart].x > polygon[edgeEnd].x ? bestEdgeStart : edgeEnd;
			const glm::vec2 intersection(bestX, holePoint.y);

			for (std::size_t i = 0; i < polygon.size(); ++i)
			{
				if (i == candidate)
				{
					continue;
				}
				if (pointInTriangle(polygon[i], holePoint, intersection, polygon[candidate]))
				{
					const glm::vec2 toCandidate = polygon[candidate] - holePoint;
					const glm::vec2 toOther = polygon[i] - holePoint;
					const float candidateAngle = std::abs(std::atan2(toCandidate.y, toCandidate.x));
					const float otherAngle = std::abs(std::atan2(toOther.y, toOther.x));
					if (otherAngle < candidateAngle)
					{
						candidate = i;
					}
				}
			}
			return candidate;
		}

		void mergeHoleIntoPolygon(std::vector<glm::vec2>& polygon, const std::vector<glm::vec2>& hole)
		{
			if (hole.size() < 3)
			{
				return;
			}

			std::size_t holeStart = 0;
			for (std::size_t i = 1; i < hole.size(); ++i)
			{
				if (hole[i].x > hole[holeStart].x)
				{
					holeStart = i;
				}
			}
			const glm::vec2 holePoint = hole[holeStart];
			const std::size_t bridgeIndex = findBridgeIndex(polygon, holePoint);
			const glm::vec2 bridgePoint = polygon[bridgeIndex];

			std::vector<glm::vec2> merged;
			merged.reserve(polygon.size() + hole.size() + 2);
			for (std::size_t i = 0; i <= bridgeIndex; ++i)
			{
				merged.push_back(polygon[i]);
			}
			for (std::size_t offset = 0; offset <= hole.size(); ++offset)
			{
				merged.push_back(hole[(holeStart + offset) % hole.size()]);
			}
			merged.push_back(bridgePoint);
			for (std::size_t i = bridgeIndex + 1; i < polygon.size(); ++i)
			{
				merged.push_back(polygon[i]);
			}
			polygon = std::move(merged);
		}

		// Simple O(n^3)-worst-case ear clipping - more than fine for glyph
		// outlines, which rarely exceed a few dozen points even after curve
		// flattening. Bails out gracefully (returns whatever was already
		// clipped) rather than looping forever on degenerate input.
		std::vector<std::array<glm::vec2, 3>> earClipTriangulate(std::vector<glm::vec2> polygon)
		{
			std::vector<std::array<glm::vec2, 3>> triangles;
			if (signedArea(polygon) < 0.0F)
			{
				std::reverse(polygon.begin(), polygon.end());
			}
			if (polygon.size() < 3)
			{
				return triangles;
			}

			std::vector<std::size_t> indices(polygon.size());
			std::iota(indices.begin(), indices.end(), 0U);

			const std::size_t maxIterations = polygon.size() * polygon.size() + 16;
			std::size_t guard = 0;
			while (indices.size() > 3 && guard++ < maxIterations)
			{
				bool earFound = false;
				for (std::size_t i = 0; i < indices.size(); ++i)
				{
					const std::size_t prev = (i + indices.size() - 1) % indices.size();
					const std::size_t next = (i + 1) % indices.size();
					const glm::vec2& a = polygon[indices[prev]];
					const glm::vec2& b = polygon[indices[i]];
					const glm::vec2& c = polygon[indices[next]];

					const float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
					if (cross <= 0.0F)
					{
						continue;
					}

					bool anyInside = false;
					for (std::size_t k = 0; k < indices.size(); ++k)
					{
						if (k == prev || k == i || k == next)
						{
							continue;
						}
						if (pointInTriangle(polygon[indices[k]], a, b, c))
						{
							anyInside = true;
							break;
						}
					}
					if (anyInside)
					{
						continue;
					}

					triangles.push_back({a, b, c});
					indices.erase(indices.begin() + static_cast<std::ptrdiff_t>(i));
					earFound = true;
					break;
				}
				if (!earFound)
				{
					break;
				}
			}
			if (indices.size() == 3)
			{
				triangles.push_back({polygon[indices[0]], polygon[indices[1]], polygon[indices[2]]});
			}
			return triangles;
		}

		std::vector<Contour> extractGlyphContours(const stbtt_fontinfo& fontInfo, const int glyphIndex, const float scale)
		{
			std::vector<Contour> contours;
			stbtt_vertex* vertices = nullptr;
			const int vertexCount = stbtt_GetGlyphShape(&fontInfo, glyphIndex, &vertices);
			if (vertexCount <= 0 || vertices == nullptr)
			{
				if (vertices != nullptr)
				{
					stbtt_FreeShape(&fontInfo, vertices);
				}
				return contours;
			}

			Contour current;
			glm::vec2 penPosition(0.0F);
			constexpr int curveSteps = 8;

			for (int i = 0; i < vertexCount; ++i)
			{
				const stbtt_vertex& vertex = vertices[i];
				const glm::vec2 point(static_cast<float>(vertex.x) * scale, static_cast<float>(vertex.y) * scale);
				switch (vertex.type)
				{
					case STBTT_vmove:
						if (!current.points.empty())
						{
							contours.push_back(std::move(current));
							current = Contour{};
						}
						current.points.push_back(point);
						penPosition = point;
						break;
					case STBTT_vline:
						current.points.push_back(point);
						penPosition = point;
						break;
					case STBTT_vcurve:
					{
						const glm::vec2 control(
							static_cast<float>(vertex.cx) * scale, static_cast<float>(vertex.cy) * scale);
						for (int step = 1; step <= curveSteps; ++step)
						{
							const float t = static_cast<float>(step) / static_cast<float>(curveSteps);
							const float u = 1.0F - t;
							current.points.push_back(
								u * u * penPosition + 2.0F * u * t * control + t * t * point);
						}
						penPosition = point;
						break;
					}
					case STBTT_vcubic:
					{
						const glm::vec2 control1(
							static_cast<float>(vertex.cx) * scale, static_cast<float>(vertex.cy) * scale);
						const glm::vec2 control2(
							static_cast<float>(vertex.cx1) * scale, static_cast<float>(vertex.cy1) * scale);
						for (int step = 1; step <= curveSteps; ++step)
						{
							const float t = static_cast<float>(step) / static_cast<float>(curveSteps);
							const float u = 1.0F - t;
							current.points.push_back(
								u * u * u * penPosition + 3.0F * u * u * t * control1 +
								3.0F * u * t * t * control2 + t * t * t * point);
						}
						penPosition = point;
						break;
					}
					default:
						break;
				}
			}
			if (!current.points.empty())
			{
				contours.push_back(std::move(current));
			}

			stbtt_FreeShape(&fontInfo, vertices);
			return contours;
		}

		void appendTriangle(
			std::vector<float>& out, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& normal)
		{
			const std::array<glm::vec3, 3> positions{a, b, c};
			for (const glm::vec3& position : positions)
			{
				out.insert(out.end(), {position.x, position.y, position.z, normal.x, normal.y, normal.z});
			}
		}

		void appendGlyphMesh(
			std::vector<float>& out,
			const std::vector<Contour>& contours,
			const std::vector<std::array<glm::vec2, 3>>& frontTriangles,
			const float depth,
			const glm::vec2& offset)
		{
			const float halfDepth = depth * 0.5F;

			for (const std::array<glm::vec2, 3>& triangle : frontTriangles)
			{
				appendTriangle(
					out,
					glm::vec3(triangle[0] + offset, halfDepth),
					glm::vec3(triangle[1] + offset, halfDepth),
					glm::vec3(triangle[2] + offset, halfDepth),
					glm::vec3(0.0F, 0.0F, 1.0F));
			}
			for (const std::array<glm::vec2, 3>& triangle : frontTriangles)
			{
				appendTriangle(
					out,
					glm::vec3(triangle[0] + offset, -halfDepth),
					glm::vec3(triangle[2] + offset, -halfDepth),
					glm::vec3(triangle[1] + offset, -halfDepth),
					glm::vec3(0.0F, 0.0F, -1.0F));
			}

			for (const Contour& contour : contours)
			{
				const std::size_t count = contour.points.size();
				if (count < 2)
				{
					continue;
				}
				for (std::size_t i = 0; i < count; ++i)
				{
					const glm::vec2 p1 = contour.points[i] + offset;
					const glm::vec2 p2 = contour.points[(i + 1) % count] + offset;
					const glm::vec3 front1(p1, halfDepth);
					const glm::vec3 front2(p2, halfDepth);
					const glm::vec3 back1(p1, -halfDepth);
					const glm::vec3 back2(p2, -halfDepth);

					const glm::vec2 edgeVector = p2 - p1;
					if (glm::length(edgeVector) < 1e-6F)
					{
						continue;
					}
					const glm::vec2 edgeDirection = glm::normalize(edgeVector);
					// classifyAndNormalizeContours() walks contours CCW for
					// outer (material) and CW for holes (cutouts in the
					// material). The wall normal must point into the
					// outside-air side - the right-perpendicular of the
					// edge direction for an outer CCW contour, the
					// left-perpendicular for a hole CW contour. The previous
					// code used the right-perpendicular for both, which made
					// the side walls of every glyph hole (A, O, 8, P, R, B,
					// ...) point into the glyph material instead of into the
					// hole - they lit from the wrong side under any non-flat
					// lighting.
					const glm::vec3 normal(
						contour.isHole ? -edgeDirection.y : edgeDirection.y,
						contour.isHole ? edgeDirection.x : -edgeDirection.x,
						0.0F);

					appendTriangle(out, front1, front2, back2, normal);
					appendTriangle(out, front1, back2, back1, normal);
				}
			}
		}
	}

	TextMeshBuildResult buildTextMesh(
		const std::filesystem::path& fontFilePath, const std::string& utf8Text, const float worldSize, const float depth)
	{
		TextMeshBuildResult result;

		std::ifstream file(fontFilePath, std::ios::binary);
		if (!file)
		{
			result.message = "Could not open font file: " + fontFilePath.string();
			return result;
		}
		const std::vector<unsigned char> fontBuffer(
			(std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if (fontBuffer.empty())
		{
			result.message = "Font file is empty: " + fontFilePath.string();
			return result;
		}

		stbtt_fontinfo fontInfo;
		const int offset = stbtt_GetFontOffsetForIndex(fontBuffer.data(), 0);
		if (offset < 0 || stbtt_InitFont(&fontInfo, fontBuffer.data(), offset) == 0)
		{
			result.message = "Not a valid TrueType/OpenType font: " + fontFilePath.string();
			return result;
		}

		const float scale = stbtt_ScaleForMappingEmToPixels(&fontInfo, worldSize);
		const std::vector<unsigned int> codepoints = decodeUtf8(utf8Text);

		int ascent = 0;
		int descent = 0;
		int lineGap = 0;
		stbtt_GetFontVMetrics(&fontInfo, &ascent, &descent, &lineGap);
		const float lineHeight = static_cast<float>(ascent - descent + lineGap) * scale;

		std::vector<float> vertices;
		float penX = 0.0F;
		float penY = 0.0F;

		for (std::size_t index = 0; index < codepoints.size(); ++index)
		{
			const unsigned int codepoint = codepoints[index];
			if (codepoint == static_cast<unsigned int>('\n'))
			{
				penX = 0.0F;
				penY -= lineHeight;
				continue;
			}

			const int glyphIndex = stbtt_FindGlyphIndex(&fontInfo, static_cast<int>(codepoint));
			if (glyphIndex != 0)
			{
				std::vector<Contour> contours = extractGlyphContours(fontInfo, glyphIndex, scale);
				if (!contours.empty())
				{
					classifyAndNormalizeContours(contours);

					std::vector<std::array<glm::vec2, 3>> triangles;
					for (std::size_t c = 0; c < contours.size(); ++c)
					{
						if (contours[c].isHole)
						{
							continue;
						}
						std::vector<glm::vec2> combined = contours[c].points;
						for (std::size_t h = 0; h < contours.size(); ++h)
						{
							if (contours[h].isHole && contours[h].parent == c)
							{
								mergeHoleIntoPolygon(combined, contours[h].points);
							}
						}
						const std::vector<std::array<glm::vec2, 3>> glyphTriangles = earClipTriangulate(combined);
						triangles.insert(triangles.end(), glyphTriangles.begin(), glyphTriangles.end());
					}

					appendGlyphMesh(vertices, contours, triangles, depth, glm::vec2(penX, penY));
				}
			}

			int advanceWidth = 0;
			int leftSideBearing = 0;
			stbtt_GetGlyphHMetrics(&fontInfo, glyphIndex, &advanceWidth, &leftSideBearing);
			penX += static_cast<float>(advanceWidth) * scale;

			if (index + 1 < codepoints.size())
			{
				const int nextGlyph = stbtt_FindGlyphIndex(&fontInfo, static_cast<int>(codepoints[index + 1]));
				penX += static_cast<float>(stbtt_GetGlyphKernAdvance(&fontInfo, glyphIndex, nextGlyph)) * scale;
			}
		}

		result.success = true;
		result.vertices = std::move(vertices);
		if (result.vertices.empty())
		{
			result.message = "Text produced no visible geometry (empty string, or the font is missing every "
							  "requested glyph).";
		}
		return result;
	}
}
