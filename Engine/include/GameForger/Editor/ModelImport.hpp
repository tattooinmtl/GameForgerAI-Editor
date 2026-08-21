#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace gameforger::editor
{
	// One entry in the imported file's node hierarchy - covers EVERY node,
	// not just ones that actually skin a mesh, since a helper/pivot node
	// between the root and a real bone still needs to contribute its
	// transform when computing that bone's animated world matrix. `parent`
	// indexes back into the same ImportedBone vector (-1 for the root);
	// vectors are always built in depth-first order, so a bone's parent
	// index is always < its own index.
	struct ImportedBone
	{
		std::string name;
		int parent = -1;
		// Bind-pose local transform (aiNode::mTransformation), used when no
		// animation channel targets this node.
		glm::mat4 localBindTransform{1.0F};
		// aiBone::mOffsetMatrix (mesh-space -> this bone's bind space).
		// Identity for nodes that don't actually skin any mesh vertex.
		glm::mat4 inverseBindMatrix{1.0F};
	};

	struct ImportedVectorKey
	{
		float timeSeconds = 0.0F;
		glm::vec3 value{0.0F};
	};

	struct ImportedQuatKey
	{
		float timeSeconds = 0.0F;
		glm::quat value{1.0F, 0.0F, 0.0F, 0.0F};
	};

	// Mirrors aiNodeAnim: position/rotation/scale keys are independent
	// tracks (not necessarily the same count or times), sampled separately.
	struct ImportedBoneTrack
	{
		int boneIndex = -1;
		std::vector<ImportedVectorKey> positionKeys;
		std::vector<ImportedQuatKey> rotationKeys;
		std::vector<ImportedVectorKey> scaleKeys;
	};

	struct ImportedAnimationClip
	{
		std::string name;
		float durationSeconds = 0.0F;
		std::vector<ImportedBoneTrack> tracks;
	};

	struct ModelImportResult
	{
		bool success = false;
		std::string message;
		// Flat vertex buffer, `vertexStride` floats per vertex:
		// - not skinned (vertexStride == 6): position(3) + normal(3), already
		//   baked into the file's own node-hierarchy world transform - matches
		//   PrimitiveMeshData's convention exactly, same as before skinning
		//   support existed.
		// - skinned (vertexStride == 14): position(3) + normal(3) + boneIndex(4)
		//   + boneWeight(4), left in raw mesh-local bind space (NOT
		//   world-baked) - GPU skinning reconstructs the correct world
		//   position from `bones`/the per-frame bone matrices instead.
		std::vector<float> vertices;
		int vertexStride = 6;
		bool hasSkeleton = false;
		std::vector<ImportedBone> bones;
		// At most one clip is imported (the file's first aiAnimation) - this
		// editor only auto-plays a model's own baked-in animation, it doesn't
		// yet support selecting among several.
		std::vector<ImportedAnimationClip> animations;
	};

	// Loads a GLB/glTF/FBX/3DS/OBJ/Blend file via Assimp (see CMakeLists.txt's
	// ASSIMP_BUILD_*_IMPORTER flags for exactly which formats are compiled
	// in) and flattens it into a single combined mesh: every aiMesh in the
	// file's node hierarchy is concatenated into one vertex buffer - not a
	// multi-part scene graph. If any mesh in the file carries skin weights,
	// the whole import switches to skinned mode (see ModelImportResult) and
	// only skinned sub-meshes are included; the file's node hierarchy, bone
	// offset matrices, and first animation clip are captured for runtime GPU
	// skinning. No materials/textures are read (this editor has no
	// texture-mapping pipeline yet). Never throws - Assimp's importers (FBX's
	// especially) can throw on malformed input, so every failure path,
	// including exceptions, is caught and returned as a normal
	// `{success = false}` result instead of propagating.
	[[nodiscard]] ModelImportResult loadModelMesh(const std::filesystem::path& filePath);
}
