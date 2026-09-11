#include "GameForger/Editor/Collision.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "GameForger/Editor/ModelImport.hpp"
#include "GameForger/Editor/PrimitiveMeshes.hpp"
#include "GameForger/Editor/Terrain.hpp"
#include "GameForger/Editor/Transform.hpp"

namespace gameforger::editor
{
	namespace
	{
		bool aabbsOverlap(const glm::vec3& aMin, const glm::vec3& aMax, const glm::vec3& bMin, const glm::vec3& bMax)
		{
			return aMin.x <= bMax.x && aMax.x >= bMin.x && aMin.y <= bMax.y && aMax.y >= bMin.y &&
				aMin.z <= bMax.z && aMax.z >= bMin.z;
		}

		glm::vec3 transformPoint(const glm::mat4& model, const glm::vec3& point)
		{
			const glm::vec4 world = model * glm::vec4(point, 1.0F);
			return glm::vec3(world);
		}

		ColliderAabb aabbFromLocalBounds(const SceneEntity& entity, const glm::vec3& localMin, const glm::vec3& localMax)
		{
			const glm::mat4 model = composeEntityTransform(entity);
			ColliderAabb bounds;
			bounds.min = glm::vec3(std::numeric_limits<float>::max());
			bounds.max = glm::vec3(std::numeric_limits<float>::lowest());
			for (int i = 0; i < 8; ++i)
			{
				const glm::vec3 local(
					(i & 1) != 0 ? localMax.x : localMin.x,
					(i & 2) != 0 ? localMax.y : localMin.y,
					(i & 4) != 0 ? localMax.z : localMin.z);
				const glm::vec3 world = transformPoint(model, local);
				bounds.min = glm::min(bounds.min, world);
				bounds.max = glm::max(bounds.max, world);
			}
			return bounds;
		}

		bool aabbOverlapsTriangle(
			const glm::vec3& amin, const glm::vec3& amax, glm::vec3 v0, glm::vec3 v1, glm::vec3 v2)
		{
			const glm::vec3 center = (amin + amax) * 0.5F;
			const glm::vec3 extent = (amax - amin) * 0.5F;
			v0 -= center;
			v1 -= center;
			v2 -= center;

			const glm::vec3 f0 = v1 - v0;
			const glm::vec3 f1 = v2 - v1;
			const glm::vec3 f2 = v0 - v2;

			const auto axisTest = [&](const glm::vec3& axis) -> bool
			{
				const float p0 = glm::dot(v0, axis);
				const float p1 = glm::dot(v1, axis);
				const float p2 = glm::dot(v2, axis);
				const float radius =
					extent.x * std::abs(axis.x) + extent.y * std::abs(axis.y) + extent.z * std::abs(axis.z);
				return std::max({p0, p1, p2}) >= -radius && std::min({p0, p1, p2}) <= radius;
			};

			if (std::max({v0.x, v1.x, v2.x}) < -extent.x || std::min({v0.x, v1.x, v2.x}) > extent.x)
			{
				return false;
			}
			if (std::max({v0.y, v1.y, v2.y}) < -extent.y || std::min({v0.y, v1.y, v2.y}) > extent.y)
			{
				return false;
			}
			if (std::max({v0.z, v1.z, v2.z}) < -extent.z || std::min({v0.z, v1.z, v2.z}) > extent.z)
			{
				return false;
			}

			const glm::vec3 normal = glm::cross(f0, f1);
			if (glm::dot(normal, normal) > 1e-12F && !axisTest(normal))
			{
				return false;
			}

			const glm::vec3 axes[9] = {
				{0.0F, -f0.z, f0.y},
				{0.0F, -f1.z, f1.y},
				{0.0F, -f2.z, f2.y},
				{f0.z, 0.0F, -f0.x},
				{f1.z, 0.0F, -f1.x},
				{f2.z, 0.0F, -f2.x},
				{-f0.y, f0.x, 0.0F},
				{-f1.y, f1.x, 0.0F},
				{-f2.y, f2.x, 0.0F},
			};
			for (const glm::vec3& axis : axes)
			{
				if (glm::dot(axis, axis) < 1e-12F)
				{
					continue;
				}
				if (!axisTest(axis))
				{
					return false;
				}
			}
			return true;
		}

		bool entityOrAncestorBlocks(const EditorScene& scene, const SceneEntity& entity, const int selfEntityId)
		{
			if (entity.hasCollider)
			{
				return true;
			}
			std::string parentName = entity.parentName;
			for (int depth = 0; depth < 64 && !parentName.empty(); ++depth)
			{
				const SceneEntity* parent = scene.findEntity(parentName);
				if (parent == nullptr)
				{
					return false;
				}
				if (parent->id == selfEntityId)
				{
					return false;
				}
				if (parent->hasCollider)
				{
					return true;
				}
				parentName = parent->parentName;
			}
			return false;
		}

		ColliderType effectiveColliderType(
			const EditorScene& scene, const SceneEntity& entity, const int selfEntityId)
		{
			if (entity.hasCollider)
			{
				return entity.colliderType;
			}
			std::string parentName = entity.parentName;
			for (int depth = 0; depth < 64 && !parentName.empty(); ++depth)
			{
				const SceneEntity* parent = scene.findEntity(parentName);
				if (parent == nullptr || parent->id == selfEntityId)
				{
					break;
				}
				if (parent->hasCollider)
				{
					return parent->colliderType;
				}
				parentName = parent->parentName;
			}
			return ColliderType::Box;
		}

		bool entityHasChildren(const EditorScene& scene, const SceneEntity& entity)
		{
			for (const SceneEntity& candidate : scene.entities())
			{
				if (candidate.parentName == entity.name)
				{
					return true;
				}
			}
			return false;
		}

		void resolveSolidAabb(
			BoxCollisionResult& result,
			const glm::vec3& otherMin,
			const glm::vec3& otherMax,
			const glm::vec3& otherCenter,
			const float halfWidth,
			const float height,
			bool& resolvedAny)
		{
			const glm::vec3 selfMin(
				result.position.x - halfWidth, result.position.y, result.position.z - halfWidth);
			const glm::vec3 selfMax(
				result.position.x + halfWidth, result.position.y + height, result.position.z + halfWidth);

			const float overlapX = std::min(selfMax.x, otherMax.x) - std::max(selfMin.x, otherMin.x);
			const float overlapY = std::min(selfMax.y, otherMax.y) - std::max(selfMin.y, otherMin.y);
			const float overlapZ = std::min(selfMax.z, otherMax.z) - std::max(selfMin.z, otherMin.z);
			if (overlapX <= 0.0F || overlapY <= 0.0F || overlapZ <= 0.0F)
			{
				return;
			}

			resolvedAny = true;
			const float selfCenterY = result.position.y + height * 0.5F;
			const bool fullyContainedHorizontally =
				overlapX >= (2.0F * halfWidth - 0.001F) && overlapZ >= (2.0F * halfWidth - 0.001F);
			if (fullyContainedHorizontally)
			{
				// Inside a solid volume (Box/AABB): pop to the top so a
				// character is not shoved under the floor at equal centers.
				result.position.y = otherMax.y;
				result.grounded = true;
			}
			else if (overlapY <= overlapX && overlapY <= overlapZ)
			{
				if (selfCenterY > otherCenter.y)
				{
					result.position.y = otherMax.y;
					result.grounded = true;
				}
				else
				{
					result.position.y = otherMin.y - height;
				}
			}
			else if (overlapX <= overlapZ)
			{
				result.position.x =
					result.position.x > otherCenter.x ? otherMax.x + halfWidth : otherMin.x - halfWidth;
			}
			else
			{
				result.position.z =
					result.position.z > otherCenter.z ? otherMax.z + halfWidth : otherMin.z - halfWidth;
			}
		}

		MeshCollisionGeometry aabbBoxTriangles(const glm::vec3& minBounds, const glm::vec3& maxBounds)
		{
			MeshCollisionGeometry geometry;
			const glm::vec3 c[8] = {
				{minBounds.x, minBounds.y, minBounds.z},
				{maxBounds.x, minBounds.y, minBounds.z},
				{maxBounds.x, maxBounds.y, minBounds.z},
				{minBounds.x, maxBounds.y, minBounds.z},
				{minBounds.x, minBounds.y, maxBounds.z},
				{maxBounds.x, minBounds.y, maxBounds.z},
				{maxBounds.x, maxBounds.y, maxBounds.z},
				{minBounds.x, maxBounds.y, maxBounds.z},
			};
			const int faces[12][3] = {
				{0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6}, {0, 4, 5}, {0, 5, 1},
				{3, 2, 6}, {3, 6, 7}, {0, 3, 7}, {0, 7, 4}, {1, 5, 6}, {1, 6, 2},
			};
			geometry.triangleVertices.reserve(36);
			for (const auto& face : faces)
			{
				geometry.triangleVertices.push_back(c[face[0]]);
				geometry.triangleVertices.push_back(c[face[1]]);
				geometry.triangleVertices.push_back(c[face[2]]);
			}
			geometry.localMin = minBounds;
			geometry.localMax = maxBounds;
			geometry.valid = true;
			return geometry;
		}

		MeshCollisionGeometry buildConvexHull(const std::vector<glm::vec3>& input)
		{
			std::vector<glm::vec3> points;
			points.reserve(input.size());
			constexpr float kWeld = 1.0e-5F;
			for (const glm::vec3& point : input)
			{
				bool duplicate = false;
				for (const glm::vec3& existing : points)
				{
					const glm::vec3 delta = point - existing;
					if (glm::dot(delta, delta) < kWeld * kWeld)
					{
						duplicate = true;
						break;
					}
				}
				if (!duplicate)
				{
					points.push_back(point);
				}
			}

			if (points.empty())
			{
				return {};
			}

			glm::vec3 minBounds(std::numeric_limits<float>::max());
			glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
			for (const glm::vec3& point : points)
			{
				minBounds = glm::min(minBounds, point);
				maxBounds = glm::max(maxBounds, point);
			}

			if (points.size() < 4)
			{
				return aabbBoxTriangles(minBounds, maxBounds);
			}

			const int count = static_cast<int>(points.size());
			int i0 = 0;
			int i1 = 1;
			float best = -1.0F;
			for (int i = 1; i < count; ++i)
			{
				const glm::vec3 delta = points[static_cast<std::size_t>(i)] - points[0];
				const float distance = glm::dot(delta, delta);
				if (distance > best)
				{
					best = distance;
					i1 = i;
				}
			}
			int i2 = 0;
			best = -1.0F;
			const glm::vec3 edge = points[static_cast<std::size_t>(i1)] - points[static_cast<std::size_t>(i0)];
			for (int i = 0; i < count; ++i)
			{
				const glm::vec3 area = glm::cross(edge, points[static_cast<std::size_t>(i)] - points[static_cast<std::size_t>(i0)]);
				const float distance = glm::dot(area, area);
				if (distance > best)
				{
					best = distance;
					i2 = i;
				}
			}
			if (best < 1.0e-12F)
			{
				return aabbBoxTriangles(minBounds, maxBounds);
			}
			const glm::vec3 planeNormal = glm::cross(
				points[static_cast<std::size_t>(i1)] - points[static_cast<std::size_t>(i0)],
				points[static_cast<std::size_t>(i2)] - points[static_cast<std::size_t>(i0)]);
			int i3 = 0;
			best = -1.0F;
			for (int i = 0; i < count; ++i)
			{
				const float distance = std::abs(glm::dot(
					planeNormal, points[static_cast<std::size_t>(i)] - points[static_cast<std::size_t>(i0)]));
				if (distance > best)
				{
					best = distance;
					i3 = i;
				}
			}
			if (best < 1.0e-8F)
			{
				return aabbBoxTriangles(minBounds, maxBounds);
			}

			struct Face
			{
				int a = 0;
				int b = 0;
				int c = 0;
			};
			std::vector<Face> faces;
			const glm::vec3 inside = (points[static_cast<std::size_t>(i0)] + points[static_cast<std::size_t>(i1)] +
									  points[static_cast<std::size_t>(i2)] + points[static_cast<std::size_t>(i3)]) *
				0.25F;
			const auto addOriented = [&](const int a, const int b, const int c)
			{
				int bb = b;
				int cc = c;
				const glm::vec3 normal = glm::cross(
					points[static_cast<std::size_t>(bb)] - points[static_cast<std::size_t>(a)],
					points[static_cast<std::size_t>(cc)] - points[static_cast<std::size_t>(a)]);
				if (glm::dot(normal, inside - points[static_cast<std::size_t>(a)]) > 0.0F)
				{
					std::swap(bb, cc);
				}
				faces.push_back({a, bb, cc});
			};
			addOriented(i0, i1, i2);
			addOriented(i0, i2, i3);
			addOriented(i0, i3, i1);
			addOriented(i1, i3, i2);

			std::vector<char> used(static_cast<std::size_t>(count), 0);
			used[static_cast<std::size_t>(i0)] = 1;
			used[static_cast<std::size_t>(i1)] = 1;
			used[static_cast<std::size_t>(i2)] = 1;
			used[static_cast<std::size_t>(i3)] = 1;

			constexpr float kVisible = 1.0e-5F;
			for (int pointIndex = 0; pointIndex < count; ++pointIndex)
			{
				if (used[static_cast<std::size_t>(pointIndex)] != 0)
				{
					continue;
				}
				const glm::vec3& point = points[static_cast<std::size_t>(pointIndex)];
				std::vector<int> visible;
				for (int faceIndex = 0; faceIndex < static_cast<int>(faces.size()); ++faceIndex)
				{
					const Face& face = faces[static_cast<std::size_t>(faceIndex)];
					const glm::vec3 normal = glm::cross(
						points[static_cast<std::size_t>(face.b)] - points[static_cast<std::size_t>(face.a)],
						points[static_cast<std::size_t>(face.c)] - points[static_cast<std::size_t>(face.a)]);
					if (glm::dot(normal, point - points[static_cast<std::size_t>(face.a)]) > kVisible)
					{
						visible.push_back(faceIndex);
					}
				}
				if (visible.empty())
				{
					continue;
				}

				std::map<std::pair<int, int>, int> edgeCount;
				std::map<std::pair<int, int>, std::pair<int, int>> directed;
				const auto addEdge = [&](const int a, const int b)
				{
					const std::pair<int, int> key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
					edgeCount[key] += 1;
					directed[key] = {a, b};
				};
				for (const int faceIndex : visible)
				{
					const Face& face = faces[static_cast<std::size_t>(faceIndex)];
					addEdge(face.a, face.b);
					addEdge(face.b, face.c);
					addEdge(face.c, face.a);
				}
				std::sort(visible.begin(), visible.end(), [](const int lhs, const int rhs) { return lhs > rhs; });
				for (const int faceIndex : visible)
				{
					faces.erase(faces.begin() + faceIndex);
				}
				for (const auto& [key, countUsed] : edgeCount)
				{
					if (countUsed != 1)
					{
						continue;
					}
					const auto [a, b] = directed[key];
					addOriented(a, b, pointIndex);
				}
				used[static_cast<std::size_t>(pointIndex)] = 1;
			}

			MeshCollisionGeometry geometry;
			geometry.localMin = minBounds;
			geometry.localMax = maxBounds;
			geometry.triangleVertices.reserve(faces.size() * 3);
			for (const Face& face : faces)
			{
				geometry.triangleVertices.push_back(points[static_cast<std::size_t>(face.a)]);
				geometry.triangleVertices.push_back(points[static_cast<std::size_t>(face.b)]);
				geometry.triangleVertices.push_back(points[static_cast<std::size_t>(face.c)]);
			}
			geometry.valid = geometry.triangleVertices.size() >= 3;
			return geometry;
		}

		const MeshCollisionGeometry* primitiveMeshCollision(const PrimitiveType type)
		{
			static std::unordered_map<int, MeshCollisionGeometry> cache;
			const int key = static_cast<int>(type);
			if (const auto found = cache.find(key); found != cache.end())
			{
				return &found->second;
			}
			const PrimitiveMeshData built = generatePrimitiveMesh(type);
			MeshCollisionGeometry geometry = meshCollisionFromVertices(built.vertices, 6);
			return &cache.emplace(key, std::move(geometry)).first->second;
		}

		const MeshCollisionGeometry* meshGeometryFor(
			const EditorScene& scene, const SceneEntity& entity, const ImportedMeshProvider& importedMesh)
		{
			if (entity.isImportedMesh)
			{
				if (importedMesh)
				{
					return importedMesh(entity);
				}
				return importedMeshCollision(scene, entity);
			}
			if (entity.isTerrain || entity.isTextMesh || entity.isCineCamera)
			{
				return nullptr;
			}
			return primitiveMeshCollision(entity.primitive);
		}

		MeshCollisionGeometry convexFrom(const MeshCollisionGeometry& mesh)
		{
			if (!mesh.valid)
			{
				return {};
			}
			return buildConvexHull(mesh.triangleVertices);
		}

		void resolveConvexSolid(
			BoxCollisionResult& result,
			const SceneEntity& other,
			const MeshCollisionGeometry& hull,
			const float halfWidth,
			const float height,
			bool& resolvedAny)
		{
			if (!hull.valid || hull.triangleVertices.size() < 3)
			{
				return;
			}

			const glm::mat4 model = composeEntityTransform(other);
			const ColliderAabb worldMesh = aabbFromLocalBounds(other, hull.localMin, hull.localMax);
			const glm::vec3 selfMin(
				result.position.x - halfWidth, result.position.y, result.position.z - halfWidth);
			const glm::vec3 selfMax(
				result.position.x + halfWidth, result.position.y + height, result.position.z + halfWidth);
			if (!aabbsOverlap(selfMin, selfMax, worldMesh.min, worldMesh.max))
			{
				return;
			}

			std::vector<glm::vec3> worldVertices;
			worldVertices.reserve(hull.triangleVertices.size());
			for (const glm::vec3& local : hull.triangleVertices)
			{
				worldVertices.push_back(transformPoint(model, local));
			}

			const glm::vec3 selfCenter = (selfMin + selfMax) * 0.5F;
			const glm::vec3 selfHalf = (selfMax - selfMin) * 0.5F;
			float bestPen = std::numeric_limits<float>::max();
			glm::vec3 bestPush(0.0F);
			bool separated = false;

			const auto considerAxis = [&](glm::vec3 axis)
			{
				const float axisLength = glm::length(axis);
				if (axisLength < 1.0e-8F || separated)
				{
					return;
				}
				axis /= axisLength;
				const float radius =
					std::abs(axis.x) * selfHalf.x + std::abs(axis.y) * selfHalf.y + std::abs(axis.z) * selfHalf.z;
				const float selfMinP = glm::dot(selfCenter, axis) - radius;
				const float selfMaxP = glm::dot(selfCenter, axis) + radius;
				float hullMinP = std::numeric_limits<float>::max();
				float hullMaxP = std::numeric_limits<float>::lowest();
				for (const glm::vec3& vertex : worldVertices)
				{
					const float projected = glm::dot(vertex, axis);
					hullMinP = std::min(hullMinP, projected);
					hullMaxP = std::max(hullMaxP, projected);
				}
				if (selfMaxP < hullMinP || selfMinP > hullMaxP)
				{
					separated = true;
					return;
				}
				const float penPositive = selfMaxP - hullMinP;
				const float penNegative = hullMaxP - selfMinP;
				if (penPositive < penNegative)
				{
					if (penPositive < bestPen)
					{
						bestPen = penPositive;
						bestPush = -axis * penPositive;
					}
				}
				else if (penNegative < bestPen)
				{
					bestPen = penNegative;
					bestPush = axis * penNegative;
				}
			};

			considerAxis({1.0F, 0.0F, 0.0F});
			considerAxis({0.0F, 1.0F, 0.0F});
			considerAxis({0.0F, 0.0F, 1.0F});
			for (std::size_t i = 0; i + 2 < worldVertices.size() && !separated; i += 3)
			{
				considerAxis(glm::cross(worldVertices[i + 1] - worldVertices[i], worldVertices[i + 2] - worldVertices[i]));
			}
			if (separated || bestPen == std::numeric_limits<float>::max())
			{
				return;
			}

			resolvedAny = true;
			result.position += bestPush;
			if (bestPush.y > 0.001F)
			{
				result.grounded = true;
			}
		}

		void resolveImportedTriangles(
			BoxCollisionResult& result,
			const SceneEntity& other,
			const MeshCollisionGeometry& mesh,
			const float halfWidth,
			const float height,
			bool& resolvedAny)
		{
			if (!mesh.valid || mesh.triangleVertices.size() < 3)
			{
				return;
			}

			const glm::mat4 model = composeEntityTransform(other);
			const ColliderAabb worldMesh = aabbFromLocalBounds(other, mesh.localMin, mesh.localMax);
			const glm::vec3 selfMin(
				result.position.x - halfWidth, result.position.y, result.position.z - halfWidth);
			const glm::vec3 selfMax(
				result.position.x + halfWidth, result.position.y + height, result.position.z + halfWidth);
			if (!aabbsOverlap(selfMin, selfMax, worldMesh.min, worldMesh.max))
			{
				return;
			}

			for (std::size_t i = 0; i + 2 < mesh.triangleVertices.size(); i += 3)
			{
				glm::vec3 selfMinNow(
					result.position.x - halfWidth, result.position.y, result.position.z - halfWidth);
				glm::vec3 selfMaxNow(
					result.position.x + halfWidth, result.position.y + height, result.position.z + halfWidth);
				const glm::vec3 a = transformPoint(model, mesh.triangleVertices[i]);
				const glm::vec3 b = transformPoint(model, mesh.triangleVertices[i + 1]);
				const glm::vec3 c = transformPoint(model, mesh.triangleVertices[i + 2]);
				if (!aabbOverlapsTriangle(selfMinNow, selfMaxNow, a, b, c))
				{
					continue;
				}

				glm::vec3 normal = glm::cross(b - a, c - a);
				const float normalLength = glm::length(normal);
				if (normalLength < 1e-8F)
				{
					continue;
				}
				normal /= normalLength;

				const glm::vec3 selfCenter = (selfMinNow + selfMaxNow) * 0.5F;
				const glm::vec3 selfHalf = (selfMaxNow - selfMinNow) * 0.5F;
				const float dist = glm::dot(selfCenter - a, normal);
				const float radius = std::abs(normal.x) * selfHalf.x + std::abs(normal.y) * selfHalf.y +
					std::abs(normal.z) * selfHalf.z;
				if (std::abs(dist) >= radius)
				{
					continue;
				}

				resolvedAny = true;
				const float penetration = radius - std::abs(dist);
				const glm::vec3 push = (dist >= 0.0F ? normal : -normal) * penetration;
				result.position += push;
				if (push.y > 0.001F)
				{
					result.grounded = true;
				}
			}
		}
	}

	MeshCollisionGeometry meshCollisionFromVertices(const std::vector<float>& vertices, const int vertexStride)
	{
		MeshCollisionGeometry geometry;
		if (vertexStride < 3 || vertices.size() < static_cast<std::size_t>(vertexStride) * 3)
		{
			return geometry;
		}
		const std::size_t stride = static_cast<std::size_t>(vertexStride);
		geometry.localMin = glm::vec3(std::numeric_limits<float>::max());
		geometry.localMax = glm::vec3(std::numeric_limits<float>::lowest());
		for (std::size_t i = 0; i + 2 < vertices.size(); i += stride)
		{
			const glm::vec3 point(vertices[i], vertices[i + 1], vertices[i + 2]);
			geometry.triangleVertices.push_back(point);
			geometry.localMin = glm::min(geometry.localMin, point);
			geometry.localMax = glm::max(geometry.localMax, point);
		}
		geometry.valid = geometry.triangleVertices.size() >= 3;
		return geometry;
	}

	ColliderAabb colliderWorldAabb(const SceneEntity& entity, const MeshCollisionGeometry* importedMesh)
	{
		if (entity.isImportedMesh && importedMesh != nullptr && importedMesh->valid)
		{
			return aabbFromLocalBounds(entity, importedMesh->localMin, importedMesh->localMax);
		}
		return {entity.position - entity.scale, entity.position + entity.scale};
	}

	const MeshCollisionGeometry* importedMeshCollision(const EditorScene& scene, const SceneEntity& entity)
	{
		if (!entity.isImportedMesh || entity.importedMesh.sourcePath.empty())
		{
			return nullptr;
		}
		const std::filesystem::path fullPath = scene.projectRoot() / entity.importedMesh.sourcePath;
		const std::string key = fullPath.lexically_normal().string();
		static std::unordered_map<std::string, MeshCollisionGeometry> cache;
		if (const auto found = cache.find(key); found != cache.end())
		{
			return &found->second;
		}
		const ModelImportResult built = loadModelMesh(fullPath);
		MeshCollisionGeometry geometry = meshCollisionFromVertices(built.vertices, built.vertexStride);
		return &cache.emplace(key, std::move(geometry)).first->second;
	}

	namespace
	{
		// The original push-out solve, unchanged, lifted out of
		// resolveBoxCollision so the step-up sweep can run it a second time
		// from a raised start without duplicating any of it.
		BoxCollisionResult resolvePushOut(
			const EditorScene& scene,
			const int selfEntityId,
			const glm::vec3& startPosition,
			const float halfWidth,
			const float height,
			const ImportedMeshProvider& importedMesh)
		{
			BoxCollisionResult result{startPosition, false};

			for (int pass = 0; pass < 3; ++pass)
			{
				bool resolvedAny = false;
				for (const SceneEntity& other : scene.entities())
				{
					if (other.id == selfEntityId || !other.active || !entityOrAncestorBlocks(scene, other, selfEntityId))
					{
						continue;
					}

					if (other.isTerrain)
					{
						const float half = other.terrain.worldSize * 0.5F;
						const float localX = result.position.x - other.position.x;
						const float localZ = result.position.z - other.position.z;
						if (localX < -half || localX > half || localZ < -half || localZ > half)
						{
							continue;
						}
						const float groundY = other.position.y +
							sampleTerrainHeight(
								other.terrain.resolution,
								other.terrain.worldSize,
								other.terrain.heightScale,
								other.terrain.heights,
								localX,
								localZ);
						if (result.position.y <= groundY)
						{
							result.position.y = groundY;
							result.grounded = true;
							resolvedAny = true;
						}
						continue;
					}

					const ColliderType type = effectiveColliderType(scene, other, selfEntityId);
					const MeshCollisionGeometry* mesh = meshGeometryFor(scene, other, importedMesh);
					const bool skipPrimitiveParent =
						entityHasChildren(scene, other) && !other.isImportedMesh;

					if (type == ColliderType::Mesh)
					{
						if (mesh != nullptr && mesh->valid)
						{
							resolveImportedTriangles(result, other, *mesh, halfWidth, height, resolvedAny);
						}
						continue;
					}

					if (type == ColliderType::Convex)
					{
						if (mesh != nullptr && mesh->valid)
						{
							if (importedMesh)
							{
								const MeshCollisionGeometry hull = convexFrom(*mesh);
								resolveConvexSolid(result, other, hull, halfWidth, height, resolvedAny);
							}
							else
							{
								static std::unordered_map<std::string, MeshCollisionGeometry> convexCache;
								const std::string key = other.isImportedMesh
									? ("i:" + (scene.projectRoot() / other.importedMesh.sourcePath)
										   .lexically_normal()
										   .string())
									: ("p:" + std::to_string(static_cast<int>(other.primitive)));
								const MeshCollisionGeometry* hull = nullptr;
								if (const auto found = convexCache.find(key); found != convexCache.end())
								{
									hull = &found->second;
								}
								else
								{
									hull = &convexCache.emplace(key, convexFrom(*mesh)).first->second;
								}
								resolveConvexSolid(result, other, *hull, halfWidth, height, resolvedAny);
							}
						}
						continue;
					}

					// Box: solid AABB. Imported models use real mesh bounds, not
					// Transform Scale (1,1,1) -> 2x2x2. Primitive parents that
					// only exist to group children skip their own cube.
					if (skipPrimitiveParent)
					{
						continue;
					}
					if (other.isImportedMesh && mesh != nullptr && mesh->valid)
					{
						const ColliderAabb box = aabbFromLocalBounds(other, mesh->localMin, mesh->localMax);
						resolveSolidAabb(
							result, box.min, box.max, (box.min + box.max) * 0.5F, halfWidth, height, resolvedAny);
						continue;
					}
					resolveSolidAabb(
						result,
						other.position - other.scale,
						other.position + other.scale,
						other.position,
						halfWidth,
						height,
						resolvedAny);
				}
				if (!resolvedAny)
				{
					break;
				}
			}

			return result;
		}

		// Highest surface top strictly under `feet` (within `maxDrop`) that the
		// mover's footprint overlaps in XZ. Used both for the step-up landing
		// and for the ground probe.
		//
		// Mesh and Convex colliders are tested by their AABB here, not their
		// triangles. That is a deliberate approximation: a per-triangle
		// downward ray is a different query from the push-out solve this file
		// is built around, and for the stair/curb case the bounds top is the
		// same answer. It means a mover can be reported grounded slightly
		// early over a concave mesh - stated rather than hidden.
		std::optional<float> highestSupportUnder(
			const EditorScene& scene,
			const int selfEntityId,
			const glm::vec3& feet,
			const float halfWidth,
			const float maxDrop,
			const ImportedMeshProvider& importedMesh)
		{
			std::optional<float> best;
			const auto consider = [&best, &feet, maxDrop](const float surfaceY)
			{
				// A hair above the feet still counts: a mover resting exactly on
				// a surface has feet == surfaceY, and floating-point drift puts
				// it either side of that.
				if (surfaceY > feet.y + 0.001F || surfaceY < feet.y - maxDrop)
				{
					return;
				}
				if (!best.has_value() || surfaceY > *best)
				{
					best = surfaceY;
				}
			};

			for (const SceneEntity& other : scene.entities())
			{
				if (other.id == selfEntityId || !other.active
					|| !entityOrAncestorBlocks(scene, other, selfEntityId))
				{
					continue;
				}

				if (other.isTerrain)
				{
					const float half = other.terrain.worldSize * 0.5F;
					const float localX = feet.x - other.position.x;
					const float localZ = feet.z - other.position.z;
					if (localX < -half || localX > half || localZ < -half || localZ > half)
					{
						continue;
					}
					consider(
						other.position.y
						+ sampleTerrainHeight(
							other.terrain.resolution, other.terrain.worldSize, other.terrain.heightScale,
							other.terrain.heights, localX, localZ));
					continue;
				}

				const MeshCollisionGeometry* mesh = meshGeometryFor(scene, other, importedMesh);
				ColliderAabb box;
				if (other.isImportedMesh && mesh != nullptr && mesh->valid)
				{
					box = aabbFromLocalBounds(other, mesh->localMin, mesh->localMax);
				}
				else
				{
					if (entityHasChildren(scene, other) && !other.isImportedMesh)
					{
						// Same rule as the push-out pass: a primitive that only
						// exists to group children is not itself solid.
						continue;
					}
					box = {other.position - other.scale, other.position + other.scale};
				}

				if (feet.x + halfWidth < box.min.x || feet.x - halfWidth > box.max.x
					|| feet.z + halfWidth < box.min.z || feet.z - halfWidth > box.max.z)
				{
					continue;
				}
				consider(box.max.y);
			}
			return best;
		}
	}

	BoxCollisionResult resolveBoxCollision(
		const EditorScene& scene,
		const int selfEntityId,
		const glm::vec3& startPosition,
		const float halfWidth,
		const float height,
		const ImportedMeshProvider& importedMesh,
		const CharacterMoveOptions& options)
	{
		BoxCollisionResult result =
			resolvePushOut(scene, selfEntityId, startPosition, halfWidth, height, importedMesh);

		// --- step up ---
		// How far the solve had to shove us back horizontally. That, not a
		// contact flag, is what "the move was blocked" means here.
		const auto horizontalPushBack = [](const BoxCollisionResult& from, const glm::vec3& origin)
		{
			const float dx = from.position.x - origin.x;
			const float dz = from.position.z - origin.z;
			return std::sqrt(dx * dx + dz * dz);
		};

		constexpr float kBlockedEpsilon = 0.0005F;
		const float blocked = horizontalPushBack(result, startPosition);
		if (options.stepHeight > 0.0F && blocked > kBlockedEpsilon)
		{
			const glm::vec3 raisedStart = startPosition + glm::vec3(0.0F, options.stepHeight, 0.0F);
			const BoxCollisionResult raised =
				resolvePushOut(scene, selfEntityId, raisedStart, halfWidth, height, importedMesh);
			const float raisedBlocked = horizontalPushBack(raised, raisedStart);

			// THIS is what stops a mover walking up a wall: against anything
			// taller than stepHeight the raised attempt is pushed back just as
			// far as the flat one, so it makes no progress and is rejected.
			// Only a surface the raise actually clears gets past here.
			if (raisedBlocked < blocked - kBlockedEpsilon)
			{
				// Belt and braces: land on something real rather than teleport
				// into the air. Given the progress check above this is hard to
				// reach - if the raise cleared the obstacle, the obstacle's top
				// is by definition within stepHeight - so treat it as a guard
				// for odd geometry, not as the wall-climbing defence.
				const std::optional<float> support = highestSupportUnder(
					scene, selfEntityId, raised.position, halfWidth, options.stepHeight + 0.001F,
					importedMesh);
				if (support.has_value())
				{
					return {glm::vec3(raised.position.x, *support, raised.position.z), true};
				}
			}
		}

		// --- ground probe ---
		// Reports only; never snaps. See CharacterMoveOptions for why.
		if (!result.grounded && options.groundProbeDistance > 0.0F)
		{
			const std::optional<float> support = highestSupportUnder(
				scene, selfEntityId, result.position, halfWidth, options.groundProbeDistance, importedMesh);
			result.grounded = support.has_value();
		}

		return result;
	}
}
