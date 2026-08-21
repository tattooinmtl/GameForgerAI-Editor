#include "GameForger/Editor/ModelImport.hpp"

#include <array>
#include <exception>
#include <unordered_map>

#include <assimp/Importer.hpp>
#include <assimp/anim.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec4.hpp>

namespace gameforger::editor
{
	namespace
	{
		// GLSL uniform array of bone matrices is fixed-size at shader compile
		// time (see ViewportRenderer's skinned shader) - a file with more
		// nodes than this falls back to the static (non-skinned) import path
		// rather than risk out-of-range bone indices on the GPU.
		constexpr int kMaxSkinningBones = 128;

		// Assimp's aiMatrix4x4 already uses the same M*v (column-vector)
		// convention as glm, just stored row-major in memory: translation
		// lives in the last COLUMN of each row (a4, b4, c4), and the last
		// row (d1..d4) is always (0,0,0,1) for an affine transform -
		// confirmed empirically (dumped a real rigged FBX's bone matrices:
		// a4/b4/c4 held a plausible hip-height translation while d1..d3 were
		// always exactly zero, never the reverse). glm::mat4's constructor
		// fills COLUMN by column, so converting means transposing the
		// row-major reading into glm's column-major one, i.e. column i takes
		// entry i from EACH of Assimp's rows (a_i, b_i, c_i, d_i) - the
		// opposite grouping from just reading a1..a4 as one glm column.
		glm::mat4 toGlm(const aiMatrix4x4& m)
		{
			return glm::mat4(
				m.a1, m.b1, m.c1, m.d1,
				m.a2, m.b2, m.c2, m.d2,
				m.a3, m.b3, m.c3, m.d3,
				m.a4, m.b4, m.c4, m.d4);
		}

		glm::quat toGlm(const aiQuaternion& q)
		{
			return glm::quat(q.w, q.x, q.y, q.z);
		}

		glm::vec3 toGlm(const aiVector3D& v)
		{
			return glm::vec3(v.x, v.y, v.z);
		}

		void pushVertex(std::vector<float>& out, const glm::vec3& position, const glm::vec3& normal)
		{
			out.push_back(position.x);
			out.push_back(position.y);
			out.push_back(position.z);
			out.push_back(normal.x);
			out.push_back(normal.y);
			out.push_back(normal.z);
		}

		bool sceneHasSkin(const aiScene& scene)
		{
			for (unsigned int i = 0; i < scene.mNumMeshes; ++i)
			{
				if (scene.mMeshes[i] != nullptr && scene.mMeshes[i]->mNumBones > 0)
				{
					return true;
				}
			}
			return false;
		}

		// Depth-first walk of the WHOLE node hierarchy (not just nodes that
		// happen to skin a mesh) - a helper/pivot node between the root and a
		// real bone still needs its own transform accounted for when
		// computing that bone's animated world matrix at runtime. Always
		// appends before recursing, so a bone's parent index is guaranteed to
		// be smaller than its own index (callers can walk the array in order
		// and rely on the parent already being processed).
		void buildHierarchy(
			const aiNode& node,
			const int parentIndex,
			std::vector<ImportedBone>& bones,
			std::unordered_map<std::string, int>& indexByName)
		{
			const int myIndex = static_cast<int>(bones.size());
			ImportedBone bone;
			bone.name = node.mName.C_Str();
			bone.parent = parentIndex;
			bone.localBindTransform = toGlm(node.mTransformation);
			bones.push_back(std::move(bone));
			indexByName[bones[static_cast<std::size_t>(myIndex)].name] = myIndex;

			for (unsigned int childIndex = 0; childIndex < node.mNumChildren; ++childIndex)
			{
				buildHierarchy(*node.mChildren[childIndex], myIndex, bones, indexByName);
			}
		}

		struct VertexSkin
		{
			std::array<int, 4> boneIndices{-1, -1, -1, -1};
			std::array<float, 4> boneWeights{0.0F, 0.0F, 0.0F, 0.0F};
		};

		// Assimp exposes skin weights per-bone (aiBone::mWeights lists which
		// vertices it influences), the opposite of what a vertex buffer
		// needs (per-vertex, top N influences) - this inverts that mapping,
		// keeping only the 4 strongest influences per vertex and normalizing
		// them to sum to 1.
		std::vector<VertexSkin> buildVertexSkin(
			const aiMesh& mesh, const std::unordered_map<std::string, int>& indexByName)
		{
			std::vector<VertexSkin> skin(mesh.mNumVertices);
			for (unsigned int boneIndex = 0; boneIndex < mesh.mNumBones; ++boneIndex)
			{
				const aiBone* bone = mesh.mBones[boneIndex];
				if (bone == nullptr)
				{
					continue;
				}
				const auto found = indexByName.find(bone->mName.C_Str());
				if (found == indexByName.end())
				{
					continue;
				}
				const int nodeIndex = found->second;
				for (unsigned int weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex)
				{
					const aiVertexWeight& weight = bone->mWeights[weightIndex];
					if (weight.mVertexId >= mesh.mNumVertices)
					{
						continue;
					}
					VertexSkin& vertexSkin = skin[weight.mVertexId];
					int weakestSlot = 0;
					for (int slot = 1; slot < 4; ++slot)
					{
						if (vertexSkin.boneWeights[static_cast<std::size_t>(slot)] <
							vertexSkin.boneWeights[static_cast<std::size_t>(weakestSlot)])
						{
							weakestSlot = slot;
						}
					}
					if (weight.mWeight > vertexSkin.boneWeights[static_cast<std::size_t>(weakestSlot)])
					{
						vertexSkin.boneWeights[static_cast<std::size_t>(weakestSlot)] = weight.mWeight;
						vertexSkin.boneIndices[static_cast<std::size_t>(weakestSlot)] = nodeIndex;
					}
				}
			}
			for (VertexSkin& vertexSkin : skin)
			{
				const float sum = vertexSkin.boneWeights[0] + vertexSkin.boneWeights[1] +
					vertexSkin.boneWeights[2] + vertexSkin.boneWeights[3];
				if (sum > 0.0F)
				{
					for (float& weight : vertexSkin.boneWeights)
					{
						weight /= sum;
					}
				}
				for (int& index : vertexSkin.boneIndices)
				{
					if (index < 0)
					{
						index = 0; // unused slot: index 0 with weight 0 is harmless
					}
				}
			}
			return skin;
		}

		// Skinned counterpart of appendMeshNode below - only ever called once
		// the whole file has already been confirmed to have skin data.
		// Unlike the static path, positions/normals are NOT baked into the
		// node's accumulated world transform: they're left in raw mesh-local
		// bind space, because the standard GPU-skinning formula
		// (bone-world-matrix * bone-offset-matrix, weighted-summed per
		// vertex) already reconstructs the correct world position by walking
		// the same node hierarchy through the bones - baking the mesh node's
		// own static transform on top would double-transform. Sub-meshes
		// with zero bones of their own (e.g. a small helper/proxy mesh
		// alongside the real skinned character) are skipped entirely rather
		// than mixed into the skinned vertex format with meaningless
		// all-zero weights, which would collapse them to the origin.
		void appendSkinnedMeshes(
			const aiScene& scene,
			const aiNode& node,
			const std::unordered_map<std::string, int>& indexByName,
			std::vector<float>& out)
		{
			for (unsigned int meshIndex = 0; meshIndex < node.mNumMeshes; ++meshIndex)
			{
				const unsigned int sceneMeshIndex = node.mMeshes[meshIndex];
				if (sceneMeshIndex >= scene.mNumMeshes)
				{
					continue;
				}
				const aiMesh* mesh = scene.mMeshes[sceneMeshIndex];
				if (mesh == nullptr || mesh->mNumBones == 0 || !mesh->HasNormals() || mesh->mVertices == nullptr ||
					mesh->mNumVertices == 0)
				{
					continue;
				}

				const std::vector<VertexSkin> skin = buildVertexSkin(*mesh, indexByName);

				for (unsigned int face = 0; face < mesh->mNumFaces; ++face)
				{
					const aiFace& triangle = mesh->mFaces[face];
					if (triangle.mNumIndices != 3)
					{
						continue;
					}
					bool indicesInRange = true;
					for (unsigned int corner = 0; corner < 3; ++corner)
					{
						if (triangle.mIndices[corner] >= mesh->mNumVertices)
						{
							indicesInRange = false;
							break;
						}
					}
					if (!indicesInRange)
					{
						continue;
					}
					for (unsigned int corner = 0; corner < 3; ++corner)
					{
						const unsigned int vertexIndex = triangle.mIndices[corner];
						const aiVector3D& position = mesh->mVertices[vertexIndex];
						const aiVector3D& normal = mesh->mNormals[vertexIndex];
						const VertexSkin& vertexSkin = skin[vertexIndex];
						pushVertex(out, toGlm(position), toGlm(normal));
						for (const int boneIndex : vertexSkin.boneIndices)
						{
							out.push_back(static_cast<float>(boneIndex));
						}
						for (const float weight : vertexSkin.boneWeights)
						{
							out.push_back(weight);
						}
					}
				}
			}

			for (unsigned int childIndex = 0; childIndex < node.mNumChildren; ++childIndex)
			{
				appendSkinnedMeshes(scene, *node.mChildren[childIndex], indexByName, out);
			}
		}

		// Walks the node hierarchy depth-first, transforming each node's
		// meshes into world space by its accumulated parent transform, and
		// appends every triangle into one flat vertex buffer - deliberately
		// flattens the whole file into a single combined mesh rather than
		// preserving it as a sub-object hierarchy (this editor's SceneEntity
		// model has no concept of child entities yet). Only used for
		// non-skinned imports (see appendSkinnedMeshes above for those).
		void appendMeshNode(
			const aiScene& scene, const aiNode& node, const glm::mat4& parentTransform, std::vector<float>& out)
		{
			const glm::mat4 worldTransform = parentTransform * toGlm(node.mTransformation);
			const glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(worldTransform));

			for (unsigned int meshIndex = 0; meshIndex < node.mNumMeshes; ++meshIndex)
			{
				// node.mMeshes[meshIndex] indexes scene.mMeshes - guard against a
				// malformed file claiming an out-of-range index rather than
				// trusting it blindly.
				const unsigned int sceneMeshIndex = node.mMeshes[meshIndex];
				if (sceneMeshIndex >= scene.mNumMeshes)
				{
					continue;
				}
				const aiMesh* mesh = scene.mMeshes[sceneMeshIndex];
				if (mesh == nullptr || !mesh->HasNormals() || mesh->mVertices == nullptr ||
					mesh->mNumVertices == 0)
				{
					continue;
				}
				for (unsigned int face = 0; face < mesh->mNumFaces; ++face)
				{
					const aiFace& triangle = mesh->mFaces[face];
					if (triangle.mNumIndices != 3)
					{
						continue;
					}
					bool indicesInRange = true;
					for (unsigned int corner = 0; corner < 3; ++corner)
					{
						if (triangle.mIndices[corner] >= mesh->mNumVertices)
						{
							indicesInRange = false;
							break;
						}
					}
					if (!indicesInRange)
					{
						continue;
					}
					for (unsigned int corner = 0; corner < 3; ++corner)
					{
						const unsigned int vertexIndex = triangle.mIndices[corner];
						const aiVector3D& position = mesh->mVertices[vertexIndex];
						const aiVector3D& normal = mesh->mNormals[vertexIndex];
						const glm::vec3 worldPosition =
							glm::vec3(worldTransform * glm::vec4(position.x, position.y, position.z, 1.0F));
						const glm::vec3 worldNormal =
							glm::normalize(normalMatrix * glm::vec3(normal.x, normal.y, normal.z));
						pushVertex(out, worldPosition, worldNormal);
					}
				}
			}

			for (unsigned int childIndex = 0; childIndex < node.mNumChildren; ++childIndex)
			{
				appendMeshNode(scene, *node.mChildren[childIndex], worldTransform, out);
			}
		}

		ImportedAnimationClip buildAnimationClip(
			const aiAnimation& anim, const std::unordered_map<std::string, int>& indexByName)
		{
			ImportedAnimationClip clip;
			clip.name = anim.mName.C_Str();
			const double ticksPerSecond = anim.mTicksPerSecond != 0.0 ? anim.mTicksPerSecond : 25.0;
			clip.durationSeconds = static_cast<float>(anim.mDuration / ticksPerSecond);

			for (unsigned int channelIndex = 0; channelIndex < anim.mNumChannels; ++channelIndex)
			{
				const aiNodeAnim* channel = anim.mChannels[channelIndex];
				if (channel == nullptr)
				{
					continue;
				}
				const auto found = indexByName.find(channel->mNodeName.C_Str());
				if (found == indexByName.end())
				{
					continue;
				}

				ImportedBoneTrack track;
				track.boneIndex = found->second;

				track.positionKeys.reserve(channel->mNumPositionKeys);
				for (unsigned int keyIndex = 0; keyIndex < channel->mNumPositionKeys; ++keyIndex)
				{
					const aiVectorKey& key = channel->mPositionKeys[keyIndex];
					track.positionKeys.push_back(
						{static_cast<float>(key.mTime / ticksPerSecond), toGlm(key.mValue)});
				}
				track.rotationKeys.reserve(channel->mNumRotationKeys);
				for (unsigned int keyIndex = 0; keyIndex < channel->mNumRotationKeys; ++keyIndex)
				{
					const aiQuatKey& key = channel->mRotationKeys[keyIndex];
					track.rotationKeys.push_back(
						{static_cast<float>(key.mTime / ticksPerSecond), toGlm(key.mValue)});
				}
				track.scaleKeys.reserve(channel->mNumScalingKeys);
				for (unsigned int keyIndex = 0; keyIndex < channel->mNumScalingKeys; ++keyIndex)
				{
					const aiVectorKey& key = channel->mScalingKeys[keyIndex];
					track.scaleKeys.push_back(
						{static_cast<float>(key.mTime / ticksPerSecond), toGlm(key.mValue)});
				}
				clip.tracks.push_back(std::move(track));
			}
			return clip;
		}
	}

	ModelImportResult loadModelMesh(const std::filesystem::path& filePath)
	{
		// Assimp's importers - FBX's especially, being one of the more
		// complex/fragile ones in the library - can throw on malformed or
		// edge-case input rather than always failing gracefully via a null
		// return. This whole function runs on the UI thread (once on import,
		// and again on every pick of an isImportedMesh entity), so an
		// uncaught exception here would unwind straight through the ImGui
		// render loop and take down the whole editor instead of just this
		// one import - catch broadly and report it as a normal failure.
		try
		{
			Assimp::Importer importer;
			const aiScene* scene = importer.ReadFile(
				filePath.string(),
				aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices |
					aiProcess_ImproveCacheLocality | aiProcess_ValidateDataStructure);
			if (scene == nullptr || scene->mRootNode == nullptr)
			{
				return {false, std::string("Could not load model: ") + importer.GetErrorString(), {}};
			}

			ModelImportResult result;

			if (sceneHasSkin(*scene))
			{
				std::vector<ImportedBone> bones;
				std::unordered_map<std::string, int> indexByName;
				buildHierarchy(*scene->mRootNode, -1, bones, indexByName);

				if (static_cast<int>(bones.size()) <= kMaxSkinningBones)
				{
					for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
					{
						const aiMesh* mesh = scene->mMeshes[meshIndex];
						if (mesh == nullptr)
						{
							continue;
						}
						for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
						{
							const aiBone* bone = mesh->mBones[boneIndex];
							if (bone == nullptr)
							{
								continue;
							}
							const auto found = indexByName.find(bone->mName.C_Str());
							if (found == indexByName.end())
							{
								continue;
							}
							bones[static_cast<std::size_t>(found->second)].inverseBindMatrix =
								toGlm(bone->mOffsetMatrix);
						}
					}

					appendSkinnedMeshes(*scene, *scene->mRootNode, indexByName, result.vertices);

					if (!result.vertices.empty())
					{
						result.hasSkeleton = true;
						result.vertexStride = 14;
						result.bones = std::move(bones);
						if (scene->mNumAnimations > 0 && scene->mAnimations[0] != nullptr)
						{
							result.animations.push_back(buildAnimationClip(*scene->mAnimations[0], indexByName));
						}
					}
				}
			}

			if (!result.hasSkeleton)
			{
				result.vertices.clear();
				appendMeshNode(*scene, *scene->mRootNode, glm::mat4(1.0F), result.vertices);
				result.vertexStride = 6;
			}

			if (result.vertices.empty())
			{
				// Some character-pack exports ship separate per-engine
				// "animation only" FBX variants (no mesh, just a skeleton +
				// clip, meant to be paired with a base mesh file elsewhere in
				// the same folder) alongside the real combined mesh+skeleton
				// file - distinguishing that case from a genuinely broken
				// file saves a confusing "why is this entity invisible"
				// round-trip.
				if (scene->mNumMeshes == 0 && scene->mNumAnimations > 0)
				{
					return {
						false,
						"Model file has animation data but no mesh geometry - it looks like an animation-only "
						"export variant (common for character packs with separate per-engine files). Check the "
						"same folder for a file with the actual mesh (often the plain, un-suffixed filename).",
						{}};
				}
				return {false, "Model file contains no triangulated mesh data.", {}};
			}
			result.success = true;
			result.message = "Model loaded.";
			return result;
		}
		catch (const std::exception& error)
		{
			return {false, std::string("Could not load model (exception): ") + error.what(), {}};
		}
		catch (...)
		{
			return {false, "Could not load model: unknown error during import.", {}};
		}
	}
}
