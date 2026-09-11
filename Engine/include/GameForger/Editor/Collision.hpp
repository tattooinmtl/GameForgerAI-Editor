#pragma once

#include <functional>
#include <vector>

#include <glm/vec3.hpp>

#include "GameForger/Editor/EditorScene.hpp"

namespace gameforger::editor
{
	// Triangle list in the mesh's local space (imported file or primitive).
	// Mesh collider tests the mover against these triangles. Convex builds a
	// solid hull from the same points. Box uses localMin/localMax as a solid AABB.
	struct MeshCollisionGeometry
	{
		std::vector<glm::vec3> triangleVertices;
		glm::vec3 localMin{-1.0F};
		glm::vec3 localMax{1.0F};
		bool valid = false;
	};

	[[nodiscard]] MeshCollisionGeometry meshCollisionFromVertices(
		const std::vector<float>& vertices, int vertexStride);

	struct BoxCollisionResult
	{
		glm::vec3 position{0.0F};
		bool grounded = false;
	};

	// Optional override so tests can inject fake castle geometry without
	// loading a GLB. Empty = load/cache from entity.importedMesh.sourcePath.
	using ImportedMeshProvider = std::function<const MeshCollisionGeometry*(const SceneEntity&)>;

	// Character-controller behaviour on top of the raw push-out below. Both
	// default to zero, which reproduces the original behaviour exactly - every
	// caller that does not opt in is unaffected.
	struct CharacterMoveOptions
	{
		// Maximum height the mover will climb without jumping. A blocked
		// horizontal move is retried raised by this much and accepted only if
		// the raised position both clears the obstacle AND has ground under
		// it - the second condition is what stops a character walking up a
		// wall one step per frame. Unity calls this Step Offset.
		float stepHeight = 0.0F;
		// How far below the feet still counts as standing on something. Zero
		// means "only while actually touching", which makes a mover stutter
		// between grounded and airborne on stair edges and slopes.
		//
		// This reports `grounded`; it deliberately does NOT snap the mover
		// down onto the surface. A snap would cancel the first frame of a
		// jump, when the feet are still within probe range but moving up. The
		// caller knows its own vertical velocity, so the caller decides: pass
		// zero while rising, the real distance while falling or level (see
		// fps_controller.lua).
		float groundProbeDistance = 0.0F;
	};

	// Axis-aligned mover box (halfWidth in X/Z, height in Y, position is
	// feet) vs Collider-enabled entities. Shape comes from colliderType:
	// Box (solid AABB), Mesh (triangles), Convex (solid hull). Descendants
	// of a Collider-enabled parent are solid too.
	[[nodiscard]] BoxCollisionResult resolveBoxCollision(
		const EditorScene& scene,
		int selfEntityId,
		const glm::vec3& startPosition,
		float halfWidth,
		float height,
		const ImportedMeshProvider& importedMesh = {},
		const CharacterMoveOptions& options = {});

	struct ColliderAabb
	{
		glm::vec3 min{0.0F};
		glm::vec3 max{0.0F};
	};

	// World AABB for projectile hit tests. Imported meshes use the mesh's
	// local bounds transformed by the entity, not position+/-scale.
	[[nodiscard]] ColliderAabb colliderWorldAabb(
		const SceneEntity& entity, const MeshCollisionGeometry* importedMesh = nullptr);

	[[nodiscard]] const MeshCollisionGeometry* importedMeshCollision(
		const EditorScene& scene, const SceneEntity& entity);
}
