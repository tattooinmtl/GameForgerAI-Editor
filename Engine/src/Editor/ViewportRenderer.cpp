#include "GameForger/Editor/ViewportRenderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>

#include <GLFW/glfw3.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat3x3.hpp>

#include "GameForger/Core/ProjectPaths.hpp"
#include "GameForger/Editor/ModelImport.hpp"
#include "GameForger/Editor/PrimitiveMeshes.hpp"
#include "GameForger/Editor/Terrain.hpp"
#include "GameForger/Editor/TerrainTexture.hpp"
#include "GameForger/Editor/TextMesh.hpp"
#include "GameForger/Editor/Transform.hpp"

namespace gameforger::editor
{
	namespace
	{
		// Simple unlit position+color pipeline, used for the ground grid and the
		// selection outline.
		constexpr const char* lineVertexShaderSource = R"glsl(
#version 460 core
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 color;
uniform mat4 modelViewProjection;
out vec3 vertexColor;

void main()
{
	vertexColor = color;
	gl_Position = modelViewProjection * vec4(position, 1.0);
}
)glsl";

		constexpr const char* lineFragmentShaderSource = R"glsl(
#version 460 core
in vec3 vertexColor;
out vec4 fragmentColor;

void main()
{
	fragmentColor = vec4(vertexColor, 1.0);
}
)glsl";

		// Lit position+normal pipeline used to draw scene entities.
		constexpr const char* meshVertexShaderSource = R"glsl(
#version 460 core
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
uniform mat4 model;
uniform mat4 viewProjection;
uniform mat3 normalMatrix;
out vec3 worldNormal;

void main()
{
	worldNormal = normalMatrix * normal;
	gl_Position = viewProjection * model * vec4(position, 1.0);
}
)glsl";

		constexpr const char* meshFragmentShaderSource = R"glsl(
#version 460 core
in vec3 worldNormal;
uniform vec3 baseColor;
out vec4 fragmentColor;

void main()
{
	vec3 lightDirection = normalize(vec3(0.4, 0.85, 0.35));
	float diffuse = max(dot(normalize(worldNormal), lightDirection), 0.0);
	vec3 shaded = baseColor * (0.35 + 0.65 * diffuse);
	fragmentColor = vec4(shaded, 1.0);
}
)glsl";

		// GPU-skinned counterpart of meshVertexShaderSource, used for
		// isImportedMesh entities that came in with skin weights (see
		// ModelImportResult::hasSkeleton) - shares meshFragmentShaderSource,
		// only the vertex stage differs. boneMatrices[128] must match
		// kMaxSkinningBones in ModelImport.cpp - that's the cap import
		// enforces before ever marking a model as skinned, so an in-range
		// bone index here is guaranteed.
		constexpr const char* skinnedMeshVertexShaderSource = R"glsl(
#version 460 core
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec4 boneIndices;
layout (location = 3) in vec4 boneWeights;
uniform mat4 model;
uniform mat4 viewProjection;
uniform mat3 normalMatrix;
uniform mat4 boneMatrices[128];
// Per-bone inverse-transpose, supplied from C++ alongside boneMatrices.
// mat3(skinMatrix) * normal only preserves normals correctly under uniform
// scale; under non-uniform bone scale (e.g. an animation that stretches
// a bone on one axis) the normal direction is wrong. The CPU passes the
// inverse-transpose of each bone's upper 3x3 here so the shader can apply
// it to the normal in lockstep with skinMatrix's vertex transform.
uniform mat3 boneInverseTransposeMatrices[128];
out vec3 worldNormal;

void main()
{
	mat4 skinMatrix =
		boneWeights.x * boneMatrices[int(boneIndices.x)] +
		boneWeights.y * boneMatrices[int(boneIndices.y)] +
		boneWeights.z * boneMatrices[int(boneIndices.z)] +
		boneWeights.w * boneMatrices[int(boneIndices.w)];
	mat3 skinNormalMatrix =
		boneWeights.x * boneInverseTransposeMatrices[int(boneIndices.x)] +
		boneWeights.y * boneInverseTransposeMatrices[int(boneIndices.y)] +
		boneWeights.z * boneInverseTransposeMatrices[int(boneIndices.z)] +
		boneWeights.w * boneInverseTransposeMatrices[int(boneIndices.w)];
	vec4 skinnedPosition = skinMatrix * vec4(position, 1.0);
	vec3 skinnedNormal = skinNormalMatrix * normal;
	worldNormal = normalMatrix * skinnedNormal;
	gl_Position = viewProjection * model * skinnedPosition;
}
)glsl";

		// isTerrain-only pipeline: blends up to 3 diffuse/normal/height
		// texture sets by the per-vertex splat weight (see
		// Terrain.hpp's buildTerrainMesh) instead of meshVertexShaderSource's
		// flat baseColor. UV comes from world-space XZ (tiled), not a
		// dedicated UV attribute - terrain has no per-vertex UV data, and
		// world-space tiling is simple and sufficient for a mostly-planar
		// heightfield.
		constexpr const char* terrainVertexShaderSource = R"glsl(
#version 460 core
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec3 splatWeight;
uniform mat4 model;
uniform mat4 viewProjection;
uniform mat3 normalMatrix;
out vec3 worldNormal;
out vec3 worldPosition;
out vec3 vertexSplatWeight;

void main()
{
	worldNormal = normalMatrix * normal;
	vec4 worldPos4 = model * vec4(position, 1.0);
	worldPosition = worldPos4.xyz;
	vertexSplatWeight = splatWeight;
	gl_Position = viewProjection * worldPos4;
}
)glsl";

		// Terrain is close to horizontal everywhere it visually matters, so
		// rather than build a full tangent-space TBN basis per triangle,
		// the normal map's XY is used as a direct perturbation of the
		// vertex's own (already slope-correct) world normal - a standard,
		// cheap approximation for mostly-flat ground, not true tangent-
		// space bump mapping. Height isn't used for real parallax
		// occlusion mapping either - it's a subtle shading multiplier for
		// visible "3D effect" without that complexity.
		constexpr const char* terrainFragmentShaderSource = R"glsl(
#version 460 core
in vec3 worldNormal;
in vec3 worldPosition;
in vec3 vertexSplatWeight;
uniform sampler2D diffuseTex0;
uniform sampler2D diffuseTex1;
uniform sampler2D diffuseTex2;
uniform sampler2D normalTex0;
uniform sampler2D normalTex1;
uniform sampler2D normalTex2;
uniform sampler2D heightTex0;
uniform sampler2D heightTex1;
uniform sampler2D heightTex2;
uniform vec3 fallbackColor;
uniform vec2 textureTileScale;
out vec4 fragmentColor;

void main()
{
	vec2 uv = worldPosition.xz / textureTileScale;
	float weightSum = vertexSplatWeight.x + vertexSplatWeight.y + vertexSplatWeight.z;
	if (weightSum < 0.0001)
	{
		vec3 lightDirection = normalize(vec3(0.4, 0.85, 0.35));
		float diffuseTerm = max(dot(normalize(worldNormal), lightDirection), 0.0);
		fragmentColor = vec4(fallbackColor * (0.35 + 0.65 * diffuseTerm), 1.0);
		return;
	}
	vec3 weight = vertexSplatWeight / weightSum;

	vec3 diffuseColor =
		texture(diffuseTex0, uv).rgb * weight.x +
		texture(diffuseTex1, uv).rgb * weight.y +
		texture(diffuseTex2, uv).rgb * weight.z;

	vec3 tangentNormal =
		(texture(normalTex0, uv).rgb * 2.0 - 1.0) * weight.x +
		(texture(normalTex1, uv).rgb * 2.0 - 1.0) * weight.y +
		(texture(normalTex2, uv).rgb * 2.0 - 1.0) * weight.z;

	float heightBoost =
		texture(heightTex0, uv).r * weight.x +
		texture(heightTex1, uv).r * weight.y +
		texture(heightTex2, uv).r * weight.z;

	vec3 bumpedNormal = normalize(worldNormal + vec3(tangentNormal.x, 0.0, tangentNormal.y) * 0.6);

	vec3 lightDirection = normalize(vec3(0.4, 0.85, 0.35));
	float diffuseTerm = max(dot(bumpedNormal, lightDirection), 0.0);
	float heightShade = 0.85 + 0.3 * (heightBoost - 0.5);
	vec3 shaded = diffuseColor * heightShade * (0.35 + 0.65 * diffuseTerm);
	fragmentColor = vec4(shaded, 1.0);
}
)glsl";

		// Textured "Appearance" material for non-terrain entities
		// (SceneEntity::materialLayers/materialBlendWeight - the Inspector's
		// per-object texture import + RGB mask). Passes local-space
		// position/normal through (not just world-space) for the fragment
		// stage's triplanar sampling below.
		constexpr const char* texturedMeshVertexShaderSource = R"glsl(
#version 460 core
layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
uniform mat4 model;
uniform mat4 viewProjection;
uniform mat3 normalMatrix;
out vec3 worldNormal;
out vec3 localPosition;
out vec3 localNormal;

void main()
{
	worldNormal = normalMatrix * normal;
	localPosition = position;
	localNormal = normal;
	gl_Position = viewProjection * model * vec4(position, 1.0);
}
)glsl";

		// None of the 6 shared primitives carry real per-vertex UVs (see
		// PrimitiveMeshes.hpp - position+normal only), so rather than add a
		// per-shape UV unwrap for cube/sphere/cylinder/cone/plane/capsule
		// (six different, fiddly mappings, and a vertex-format change that
		// would ripple into every other primitive consumer), this samples
		// via triplanar projection instead: three axis-aligned planar
		// projections of local position, blended by how much the local
		// normal faces each axis. Standard technique for texturing
		// arbitrary geometry without real UVs. Deliberately diffuse+height
		// only, no normal-map perturbation (unlike the terrain shader) -
		// correct triplanar normal mapping needs each axis's sample
		// reoriented into object space before blending, real extra
		// complexity for a secondary visual detail on non-terrain objects.
		constexpr const char* texturedMeshFragmentShaderSource = R"glsl(
#version 460 core
in vec3 worldNormal;
in vec3 localPosition;
in vec3 localNormal;
uniform sampler2D diffuseTex0;
uniform sampler2D diffuseTex1;
uniform sampler2D diffuseTex2;
uniform sampler2D heightTex0;
uniform sampler2D heightTex1;
uniform sampler2D heightTex2;
uniform vec3 materialWeight;
uniform vec3 fallbackColor;
uniform vec2 textureTileScale;
out vec4 fragmentColor;

vec3 sampleTriplanar(sampler2D tex, vec3 blend, vec3 pos)
{
	vec3 cx = texture(tex, pos.zy / textureTileScale).rgb;
	vec3 cy = texture(tex, pos.xz / textureTileScale).rgb;
	vec3 cz = texture(tex, pos.xy / textureTileScale).rgb;
	return cx * blend.x + cy * blend.y + cz * blend.z;
}

void main()
{
	float weightSum = materialWeight.x + materialWeight.y + materialWeight.z;
	if (weightSum < 0.0001)
	{
		vec3 lightDirection = normalize(vec3(0.4, 0.85, 0.35));
		float diffuseTerm = max(dot(normalize(worldNormal), lightDirection), 0.0);
		fragmentColor = vec4(fallbackColor * (0.35 + 0.65 * diffuseTerm), 1.0);
		return;
	}
	vec3 layerWeight = materialWeight / weightSum;
	vec3 triplanarBlend = abs(normalize(localNormal));
	triplanarBlend = triplanarBlend / (triplanarBlend.x + triplanarBlend.y + triplanarBlend.z + 0.0001);

	vec3 diffuseColor =
		sampleTriplanar(diffuseTex0, triplanarBlend, localPosition) * layerWeight.x +
		sampleTriplanar(diffuseTex1, triplanarBlend, localPosition) * layerWeight.y +
		sampleTriplanar(diffuseTex2, triplanarBlend, localPosition) * layerWeight.z;

	float heightBoost =
		sampleTriplanar(heightTex0, triplanarBlend, localPosition).r * layerWeight.x +
		sampleTriplanar(heightTex1, triplanarBlend, localPosition).r * layerWeight.y +
		sampleTriplanar(heightTex2, triplanarBlend, localPosition).r * layerWeight.z;

	vec3 lightDirection = normalize(vec3(0.4, 0.85, 0.35));
	float diffuseTerm = max(dot(normalize(worldNormal), lightDirection), 0.0);
	float heightShade = 0.85 + 0.3 * (heightBoost - 0.5);
	vec3 shaded = diffuseColor * heightShade * (0.35 + 0.65 * diffuseTerm);
	fragmentColor = vec4(shaded, 1.0);
}
)glsl";

		// Independent linear (position/scale) and spherical (rotation)
		// interpolation across a bone track's keys - Assimp gives each of
		// position/rotation/scale its own key list with its own count/times
		// (see aiNodeAnim), not one merged timeline, so they're sampled
		// separately rather than resampled onto a shared grid at import time.
		glm::vec3 sampleVectorTrack(
			const std::vector<ImportedVectorKey>& keys, const float timeSeconds, const glm::vec3& fallback)
		{
			if (keys.empty())
			{
				return fallback;
			}
			if (keys.size() == 1 || timeSeconds <= keys.front().timeSeconds)
			{
				return keys.front().value;
			}
			if (timeSeconds >= keys.back().timeSeconds)
			{
				return keys.back().value;
			}
			for (std::size_t index = 0; index + 1 < keys.size(); ++index)
			{
				if (timeSeconds >= keys[index].timeSeconds && timeSeconds <= keys[index + 1].timeSeconds)
				{
					const float span = keys[index + 1].timeSeconds - keys[index].timeSeconds;
					const float t = span > 0.0F ? (timeSeconds - keys[index].timeSeconds) / span : 0.0F;
					return glm::mix(keys[index].value, keys[index + 1].value, t);
				}
			}
			return keys.back().value;
		}

		struct DecomposedTransform
		{
			glm::vec3 translation{0.0F};
			glm::quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
			glm::vec3 scale{1.0F};
		};

		// Manual TRS decomposition (assumes no shear/negative scale, true for
		// every bind-pose bone transform in practice) - used as the
		// per-component fallback when an animation channel exists for a bone
		// but only animates SOME of position/rotation/scale (a common,
		// legitimate export shape - e.g. a channel that only rotates a bone
		// and never had position/scale keys baked at all). Falling back to a
		// hardcoded identity for the missing component instead of the bone's
		// own bind value would snap it to the origin / bind rotation / unit
		// scale instead of preserving it.
		DecomposedTransform decomposeTRS(const glm::mat4& transform)
		{
			DecomposedTransform result;
			result.translation = glm::vec3(transform[3]);
			const glm::vec3 col0(transform[0]);
			const glm::vec3 col1(transform[1]);
			const glm::vec3 col2(transform[2]);
			result.scale = glm::vec3(glm::length(col0), glm::length(col1), glm::length(col2));
			const glm::mat3 rotationBasis(
				result.scale.x > 0.0F ? col0 / result.scale.x : col0,
				result.scale.y > 0.0F ? col1 / result.scale.y : col1,
				result.scale.z > 0.0F ? col2 / result.scale.z : col2);
			result.rotation = glm::quat_cast(rotationBasis);
			return result;
		}

		glm::quat sampleQuatTrack(
			const std::vector<ImportedQuatKey>& keys, const float timeSeconds, const glm::quat& fallback)
		{
			if (keys.empty())
			{
				return fallback;
			}
			if (keys.size() == 1 || timeSeconds <= keys.front().timeSeconds)
			{
				return keys.front().value;
			}
			if (timeSeconds >= keys.back().timeSeconds)
			{
				return keys.back().value;
			}
			for (std::size_t index = 0; index + 1 < keys.size(); ++index)
			{
				if (timeSeconds >= keys[index].timeSeconds && timeSeconds <= keys[index + 1].timeSeconds)
				{
					const float span = keys[index + 1].timeSeconds - keys[index].timeSeconds;
					const float t = span > 0.0F ? (timeSeconds - keys[index].timeSeconds) / span : 0.0F;
					return glm::slerp(keys[index].value, keys[index + 1].value, t);
				}
			}
			return keys.back().value;
		}

		bool checkShader(const GLuint shader, const char* label)
		{
			GLint success = GL_FALSE;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
			if (success == GL_TRUE)
			{
				return true;
			}

			std::array<char, 2048> log{};
			glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
			std::fprintf(stderr, "%s shader compilation failed: %s\n", label, log.data());
			return false;
		}

		bool checkProgram(const GLuint program)
		{
			GLint success = GL_FALSE;
			glGetProgramiv(program, GL_LINK_STATUS, &success);
			if (success == GL_TRUE)
			{
				return true;
			}

			std::array<char, 2048> log{};
			glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
			std::fprintf(stderr, "Viewport shader linking failed: %s\n", log.data());
			return false;
		}

		GLuint compileProgram(const char* vertexSource, const char* fragmentSource)
		{
			const GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
			glShaderSource(vertexShader, 1, &vertexSource, nullptr);
			glCompileShader(vertexShader);
			if (!checkShader(vertexShader, "Vertex"))
			{
				glDeleteShader(vertexShader);
				return 0;
			}

			const GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
			glShaderSource(fragmentShader, 1, &fragmentSource, nullptr);
			glCompileShader(fragmentShader);
			if (!checkShader(fragmentShader, "Fragment"))
			{
				glDeleteShader(vertexShader);
				glDeleteShader(fragmentShader);
				return 0;
			}

			const GLuint program = glCreateProgram();
			glAttachShader(program, vertexShader);
			glAttachShader(program, fragmentShader);
			glLinkProgram(program);
			glDeleteShader(vertexShader);
			glDeleteShader(fragmentShader);

			if (!checkProgram(program))
			{
				glDeleteProgram(program);
				return 0;
			}
			return program;
		}

		constexpr std::array<PrimitiveType, 6> kAllPrimitives{
			PrimitiveType::Cube,
			PrimitiveType::Sphere,
			PrimitiveType::Cylinder,
			PrimitiveType::Cone,
			PrimitiveType::Plane,
			PrimitiveType::Capsule};

		GLuint createSolidColorTexture(
			const unsigned char r, const unsigned char g, const unsigned char b)
		{
			const std::array<unsigned char, 4> pixel{r, g, b, 255};
			GLuint texture = 0;
			glGenTextures(1, &texture);
			glBindTexture(GL_TEXTURE_2D, texture);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glBindTexture(GL_TEXTURE_2D, 0);
			return texture;
		}
	}

	ViewportRenderer::~ViewportRenderer()
	{
		shutdown();
	}

	bool ViewportRenderer::initialize()
	{
		if (!createShaderPrograms())
		{
			return false;
		}

		createPrimitiveMeshes();
		createOutlineMesh();
		createCameraIconMesh();

		fallbackWhiteTexture_ = createSolidColorTexture(255, 255, 255);
		fallbackFlatNormalTexture_ = createSolidColorTexture(128, 128, 255);
		fallbackMidHeightTexture_ = createSolidColorTexture(128, 128, 128);

		std::vector<float> gridVertices;
		gridVertices.reserve(21 * 2 * 6 * 2);
		for (int coordinate = -10; coordinate <= 10; ++coordinate)
		{
			const float value = static_cast<float>(coordinate);
			const std::array<float, 6> color{0.24F, 0.27F, 0.32F};
			const std::array<float, 3> horizontalStart{-10.0F, -1.05F, value};
			const std::array<float, 3> horizontalEnd{10.0F, -1.05F, value};
			const std::array<float, 3> verticalStart{value, -1.05F, -10.0F};
			const std::array<float, 3> verticalEnd{value, -1.05F, 10.0F};
			for (const auto& point : {horizontalStart, horizontalEnd, verticalStart, verticalEnd})
			{
				gridVertices.insert(gridVertices.end(), point.begin(), point.end());
				gridVertices.insert(gridVertices.end(), color.begin(), color.begin() + 3);
			}
		}

		glGenVertexArrays(1, &gridVertexArray_);
		glGenBuffers(1, &gridVertexBuffer_);
		glBindVertexArray(gridVertexArray_);
		glBindBuffer(GL_ARRAY_BUFFER, gridVertexBuffer_);
		glBufferData(
			GL_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(gridVertices.size() * sizeof(float)),
			gridVertices.data(),
			GL_STATIC_DRAW);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(
			1,
			3,
			GL_FLOAT,
			GL_FALSE,
			6 * sizeof(float),
			reinterpret_cast<const void*>(3 * sizeof(float)));
		glEnableVertexAttribArray(1);
		glBindVertexArray(0);
		gridVertexCount_ = static_cast<GLsizei>(gridVertices.size() / 6);

		return resize(1, 1);
	}

	void ViewportRenderer::createPrimitiveMeshes()
	{
		for (const PrimitiveType primitive : kAllPrimitives)
		{
			const std::size_t index = static_cast<std::size_t>(primitive);
			const PrimitiveMeshData mesh = generatePrimitiveMesh(primitive);

			glGenVertexArrays(1, &primitiveVertexArrays_[index]);
			glGenBuffers(1, &primitiveVertexBuffers_[index]);
			glBindVertexArray(primitiveVertexArrays_[index]);
			glBindBuffer(GL_ARRAY_BUFFER, primitiveVertexBuffers_[index]);
			glBufferData(
				GL_ARRAY_BUFFER,
				static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(float)),
				mesh.vertices.data(),
				GL_STATIC_DRAW);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(
				1,
				3,
				GL_FLOAT,
				GL_FALSE,
				6 * sizeof(float),
				reinterpret_cast<const void*>(3 * sizeof(float)));
			glEnableVertexAttribArray(1);
			glBindVertexArray(0);

			primitiveVertexCounts_[index] = static_cast<GLsizei>(mesh.vertices.size() / 6);
		}
	}

	void ViewportRenderer::createOutlineMesh()
	{
		constexpr std::array<float, 3> outlineColor{1.0F, 0.65F, 0.1F};
		constexpr std::array<std::array<float, 3>, 8> corners{{
			{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
			{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}}};
		constexpr std::array<std::array<int, 2>, 12> edges{{
			{0, 1}, {1, 2}, {2, 3}, {3, 0},
			{4, 5}, {5, 6}, {6, 7}, {7, 4},
			{0, 4}, {1, 5}, {2, 6}, {3, 7}}};

		std::vector<float> vertices;
		vertices.reserve(edges.size() * 2 * 6);
		for (const auto& edge : edges)
		{
			for (const int cornerIndex : edge)
			{
				const auto& corner = corners[static_cast<std::size_t>(cornerIndex)];
				vertices.insert(vertices.end(), corner.begin(), corner.end());
				vertices.insert(vertices.end(), outlineColor.begin(), outlineColor.end());
			}
		}

		glGenVertexArrays(1, &outlineVertexArray_);
		glGenBuffers(1, &outlineVertexBuffer_);
		glBindVertexArray(outlineVertexArray_);
		glBindBuffer(GL_ARRAY_BUFFER, outlineVertexBuffer_);
		glBufferData(
			GL_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
			vertices.data(),
			GL_STATIC_DRAW);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(
			1,
			3,
			GL_FLOAT,
			GL_FALSE,
			6 * sizeof(float),
			reinterpret_cast<const void*>(3 * sizeof(float)));
		glEnableVertexAttribArray(1);
		glBindVertexArray(0);
		outlineVertexCount_ = static_cast<GLsizei>(vertices.size() / 6);
	}

	void ViewportRenderer::createCameraIconMesh()
	{
		constexpr std::array<float, 3> iconColor{0.25F, 0.85F, 0.95F};
		// Apex at the local origin (the "lens"), base rectangle out along
		// local +Z (this project's forward axis, see yawForward/entity
		// getForward) - reads as a small camera/frustum icon. A rectangular
		// (not square) base also hints at "up" without a separate tick mark.
		constexpr glm::vec3 apex{0.0F, 0.0F, 0.0F};
		constexpr std::array<glm::vec3, 4> base{
			glm::vec3(-0.5F, -0.35F, 1.2F),
			glm::vec3(0.5F, -0.35F, 1.2F),
			glm::vec3(0.5F, 0.35F, 1.2F),
			glm::vec3(-0.5F, 0.35F, 1.2F)};

		std::vector<float> vertices;
		const auto pushSegment = [&vertices](const glm::vec3& a, const glm::vec3& b)
		{
			vertices.insert(vertices.end(), {a.x, a.y, a.z, iconColor[0], iconColor[1], iconColor[2]});
			vertices.insert(vertices.end(), {b.x, b.y, b.z, iconColor[0], iconColor[1], iconColor[2]});
		};
		for (const glm::vec3& corner : base)
		{
			pushSegment(apex, corner);
		}
		for (std::size_t i = 0; i < base.size(); ++i)
		{
			pushSegment(base[i], base[(i + 1) % base.size()]);
		}

		glGenVertexArrays(1, &cameraIconVertexArray_);
		glGenBuffers(1, &cameraIconVertexBuffer_);
		glBindVertexArray(cameraIconVertexArray_);
		glBindBuffer(GL_ARRAY_BUFFER, cameraIconVertexBuffer_);
		glBufferData(
			GL_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
			vertices.data(),
			GL_STATIC_DRAW);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(
			1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(3 * sizeof(float)));
		glEnableVertexAttribArray(1);
		glBindVertexArray(0);
		cameraIconVertexCount_ = static_cast<GLsizei>(vertices.size() / 6);
	}

	bool ViewportRenderer::resize(const int width, const int height)
	{
		if (width <= 0 || height <= 0)
		{
			return false;
		}

		if (width == width_ && height == height_ && framebuffer_ != 0)
		{
			return true;
		}

		return createFramebuffer(width, height);
	}

	void ViewportRenderer::render(
		const std::vector<SceneEntity>& entities,
		const std::vector<int>& selectedEntityIds,
		const std::filesystem::path& projectRoot,
		const int excludeEntityId)
	{
		if (framebuffer_ == 0 || meshShaderProgram_ == 0 || width_ <= 0 || height_ <= 0)
		{
			return;
		}

		// Drop cached GPU meshes for text entities that no longer exist, so a
		// long editing session doesn't slowly leak VAOs/VBOs for deleted text.
		for (auto it = textMeshCache_.begin(); it != textMeshCache_.end();)
		{
			const int cachedId = it->first;
			const bool stillExists = std::any_of(
				entities.begin(),
				entities.end(),
				[cachedId](const SceneEntity& entity) { return entity.id == cachedId; });
			if (stillExists)
			{
				++it;
				continue;
			}
			glDeleteBuffers(1, &it->second.vertexBuffer);
			glDeleteVertexArrays(1, &it->second.vertexArray);
			it = textMeshCache_.erase(it);
		}

		// Same pruning for terrain GPU meshes.
		for (auto it = terrainCache_.begin(); it != terrainCache_.end();)
		{
			const int cachedId = it->first;
			const bool stillExists = std::any_of(
				entities.begin(),
				entities.end(),
				[cachedId](const SceneEntity& entity) { return entity.id == cachedId; });
			if (stillExists)
			{
				++it;
				continue;
			}
			glDeleteBuffers(1, &it->second.vertexBuffer);
			glDeleteVertexArrays(1, &it->second.vertexArray);
			it = terrainCache_.erase(it);
		}

		// Same pruning for imported-mesh GPU meshes.
		for (auto it = importedMeshCache_.begin(); it != importedMeshCache_.end();)
		{
			const int cachedId = it->first;
			const bool stillExists = std::any_of(
				entities.begin(),
				entities.end(),
				[cachedId](const SceneEntity& entity) { return entity.id == cachedId; });
			if (stillExists)
			{
				++it;
				continue;
			}
			glDeleteBuffers(1, &it->second.vertexBuffer);
			glDeleteVertexArrays(1, &it->second.vertexArray);
			it = importedMeshCache_.erase(it);
		}

		// Same pruning for material (terrain splat) textures. Each cached
		// entity owns 9 GL textures (3 layers x diffuse/normal/height) - a
		// long session of adding+deleting textured primitives would leak
		// them all until shutdown() ran, since materialCache_ was previously
		// only freed at shutdown.
		for (auto it = materialCache_.begin(); it != materialCache_.end();)
		{
			const int cachedId = it->first;
			const bool stillExists = std::any_of(
				entities.begin(),
				entities.end(),
				[cachedId](const SceneEntity& entity) { return entity.id == cachedId; });
			if (stillExists)
			{
				++it;
				continue;
			}
			for (TerrainLayerGpuEntry& layer : it->second)
			{
				if (layer.diffuseTexture != 0)
				{
					glDeleteTextures(1, &layer.diffuseTexture);
					layer.diffuseTexture = 0;
				}
				if (layer.normalTexture != 0)
				{
					glDeleteTextures(1, &layer.normalTexture);
					layer.normalTexture = 0;
				}
				if (layer.heightTexture != 0)
				{
					glDeleteTextures(1, &layer.heightTexture);
					layer.heightTexture = 0;
				}
			}
			it = materialCache_.erase(it);
		}

		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
		glViewport(0, 0, width_, height_);
		glEnable(GL_DEPTH_TEST);
		glClearColor(0.055F, 0.067F, 0.09F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		const glm::vec3 eye = cameraPosition();
		lastProjection_ = glm::perspective(
			glm::radians(50.0F),
			static_cast<float>(width_) / static_cast<float>(height_),
			0.05F,
			250.0F);
		lastView_ = glm::lookAt(eye, cameraTarget_, glm::vec3(0.0F, 1.0F, 0.0F));
		const glm::mat4 viewProjection = lastProjection_ * lastView_;

		glUseProgram(lineShaderProgram_);
		const GLint lineMvpLocation = glGetUniformLocation(lineShaderProgram_, "modelViewProjection");
		glUniformMatrix4fv(lineMvpLocation, 1, GL_FALSE, glm::value_ptr(viewProjection));
		glBindVertexArray(gridVertexArray_);
		glDrawArrays(GL_LINES, 0, gridVertexCount_);
		glBindVertexArray(0);

		glUseProgram(meshShaderProgram_);
		const GLint modelLocation = glGetUniformLocation(meshShaderProgram_, "model");
		const GLint viewProjectionLocation = glGetUniformLocation(meshShaderProgram_, "viewProjection");
		const GLint normalMatrixLocation = glGetUniformLocation(meshShaderProgram_, "normalMatrix");
		const GLint baseColorLocation = glGetUniformLocation(meshShaderProgram_, "baseColor");
		glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, glm::value_ptr(viewProjection));

		// Querying a program's uniform locations doesn't require it to be
		// the currently active one (glUseProgram) - only the glUniform*
		// setter calls do - so these can be fetched once up front alongside
		// meshShaderProgram_'s own, without disturbing the viewProjection
		// upload just above.
		const GLint skinnedModelLocation = glGetUniformLocation(skinnedMeshShaderProgram_, "model");
		const GLint skinnedViewProjectionLocation = glGetUniformLocation(skinnedMeshShaderProgram_, "viewProjection");
		const GLint skinnedNormalMatrixLocation = glGetUniformLocation(skinnedMeshShaderProgram_, "normalMatrix");
		const GLint skinnedBaseColorLocation = glGetUniformLocation(skinnedMeshShaderProgram_, "baseColor");
		const GLint boneMatricesLocation = glGetUniformLocation(skinnedMeshShaderProgram_, "boneMatrices");
		const GLint boneInverseTransposeMatricesLocation =
			glGetUniformLocation(skinnedMeshShaderProgram_, "boneInverseTransposeMatrices");

		const GLint terrainModelLocation = glGetUniformLocation(terrainShaderProgram_, "model");
		const GLint terrainViewProjectionLocation = glGetUniformLocation(terrainShaderProgram_, "viewProjection");
		const GLint terrainNormalMatrixLocation = glGetUniformLocation(terrainShaderProgram_, "normalMatrix");
		const GLint terrainFallbackColorLocation = glGetUniformLocation(terrainShaderProgram_, "fallbackColor");
		const GLint terrainTileSizeLocation = glGetUniformLocation(terrainShaderProgram_, "textureTileScale");
		const std::array<GLint, 3> terrainDiffuseLocations{
			glGetUniformLocation(terrainShaderProgram_, "diffuseTex0"),
			glGetUniformLocation(terrainShaderProgram_, "diffuseTex1"),
			glGetUniformLocation(terrainShaderProgram_, "diffuseTex2")};
		const std::array<GLint, 3> terrainNormalLocations{
			glGetUniformLocation(terrainShaderProgram_, "normalTex0"),
			glGetUniformLocation(terrainShaderProgram_, "normalTex1"),
			glGetUniformLocation(terrainShaderProgram_, "normalTex2")};
		const std::array<GLint, 3> terrainHeightLocations{
			glGetUniformLocation(terrainShaderProgram_, "heightTex0"),
			glGetUniformLocation(terrainShaderProgram_, "heightTex1"),
			glGetUniformLocation(terrainShaderProgram_, "heightTex2")};

		const GLint texturedModelLocation = glGetUniformLocation(texturedMeshShaderProgram_, "model");
		const GLint texturedViewProjectionLocation =
			glGetUniformLocation(texturedMeshShaderProgram_, "viewProjection");
		const GLint texturedNormalMatrixLocation = glGetUniformLocation(texturedMeshShaderProgram_, "normalMatrix");
		const GLint texturedFallbackColorLocation = glGetUniformLocation(texturedMeshShaderProgram_, "fallbackColor");
		const GLint texturedMaterialWeightLocation =
			glGetUniformLocation(texturedMeshShaderProgram_, "materialWeight");
		const GLint texturedTileSizeLocation = glGetUniformLocation(texturedMeshShaderProgram_, "textureTileScale");
		const std::array<GLint, 3> texturedDiffuseLocations{
			glGetUniformLocation(texturedMeshShaderProgram_, "diffuseTex0"),
			glGetUniformLocation(texturedMeshShaderProgram_, "diffuseTex1"),
			glGetUniformLocation(texturedMeshShaderProgram_, "diffuseTex2")};
		const std::array<GLint, 3> texturedHeightLocations{
			glGetUniformLocation(texturedMeshShaderProgram_, "heightTex0"),
			glGetUniformLocation(texturedMeshShaderProgram_, "heightTex1"),
			glGetUniformLocation(texturedMeshShaderProgram_, "heightTex2")};

		// activeInHierarchy: an entity is hidden when it or any ancestor is
		// inactive (a hidden weapon model hides all its parts; an item in
		// the inventory hides its children too).
		std::unordered_map<std::string, const SceneEntity*> entitiesByName;
		entitiesByName.reserve(entities.size());
		for (const SceneEntity& entity : entities)
		{
			entitiesByName.emplace(entity.name, &entity);
		}
		const auto activeInHierarchy = [&entitiesByName, &entities](const SceneEntity& entity)
		{
			const SceneEntity* current = &entity;
			for (std::size_t depth = 0; current != nullptr && depth <= entities.size(); ++depth)
			{
				if (!current->active)
				{
					return false;
				}
				if (current->parentName.empty())
				{
					return true;
				}
				const auto parent = entitiesByName.find(current->parentName);
				current = parent != entitiesByName.end() ? parent->second : nullptr;
			}
			return true;
		};

		for (const SceneEntity& entity : entities)
		{
			// "Empty" = transform-only group node (e.g. the FPS rig's
			// hand/weapon groups) - it positions its children but has no
			// look of its own.
			if (entity.id == excludeEntityId || !activeInHierarchy(entity) || hasTag(entity, "Empty"))
			{
				continue;
			}

			const glm::mat4 model = composeEntityTransform(entity);
			const glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(model));

			// Any non-terrain entity with a real Appearance material (the
			// Inspector's per-object texture import + RGB mask) draws with
			// texturedMeshShaderProgram_ instead of the flat-color
			// meshShaderProgram_ - terrain is excluded here since it always
			// uses its own dedicated terrainShaderProgram_ regardless
			// (isTerrain branch below), and materialBlendWeight defaults to
			// all-zero anyway for a terrain entity.
			const float materialWeightSum = entity.materialBlendWeight.x + entity.materialBlendWeight.y +
				entity.materialBlendWeight.z;
			const bool hasMaterial = !entity.isTerrain && materialWeightSum > 0.0001F;
			if (hasMaterial)
			{
				std::array<TerrainLayerGpuEntry, 3>& materialLayers = materialCache_[entity.id];
				for (std::size_t layerIndex = 0; layerIndex < materialLayers.size(); ++layerIndex)
				{
					ensureTerrainLayerTexturesGpu(materialLayers[layerIndex], entity.materialLayers[layerIndex], projectRoot);
				}

				glUseProgram(texturedMeshShaderProgram_);
				glUniformMatrix4fv(texturedModelLocation, 1, GL_FALSE, glm::value_ptr(model));
				glUniformMatrix4fv(texturedViewProjectionLocation, 1, GL_FALSE, glm::value_ptr(viewProjection));
				glUniformMatrix3fv(texturedNormalMatrixLocation, 1, GL_FALSE, glm::value_ptr(normalMatrix));
				glUniform3fv(texturedFallbackColorLocation, 1, glm::value_ptr(entity.color));
				glUniform3fv(texturedMaterialWeightLocation, 1, glm::value_ptr(entity.materialBlendWeight));
				glUniform2fv(texturedTileSizeLocation, 1, glm::value_ptr(entity.materialUvScale));
				for (std::size_t layerIndex = 0; layerIndex < materialLayers.size(); ++layerIndex)
				{
					const TerrainLayerGpuEntry& layer = materialLayers[layerIndex];
					glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + layerIndex * 2));
					glBindTexture(
						GL_TEXTURE_2D, layer.diffuseTexture != 0 ? layer.diffuseTexture : fallbackWhiteTexture_);
					glUniform1i(texturedDiffuseLocations[layerIndex], static_cast<GLint>(layerIndex * 2));

					glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + layerIndex * 2 + 1));
					glBindTexture(
						GL_TEXTURE_2D,
						layer.heightTexture != 0 ? layer.heightTexture : fallbackMidHeightTexture_);
					glUniform1i(texturedHeightLocations[layerIndex], static_cast<GLint>(layerIndex * 2 + 1));
				}
			}
			else
			{
				glUseProgram(meshShaderProgram_);
				glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(model));
				glUniformMatrix3fv(normalMatrixLocation, 1, GL_FALSE, glm::value_ptr(normalMatrix));
				glUniform3fv(baseColorLocation, 1, glm::value_ptr(entity.color));
			}

			if (entity.isTextMesh)
			{
				ensureTextMeshGpu(entity, projectRoot);
				const auto cacheEntry = textMeshCache_.find(entity.id);
				if (cacheEntry != textMeshCache_.end() && cacheEntry->second.vertexCount > 0)
				{
					glBindVertexArray(cacheEntry->second.vertexArray);
					glDrawArrays(GL_TRIANGLES, 0, cacheEntry->second.vertexCount);
				}
				continue;
			}

			if (entity.isTerrain)
			{
				ensureTerrainGpu(entity, projectRoot);
				const auto cacheEntry = terrainCache_.find(entity.id);
				if (cacheEntry != terrainCache_.end() && cacheEntry->second.vertexCount > 0)
				{
					glUseProgram(terrainShaderProgram_);
					glUniformMatrix4fv(terrainModelLocation, 1, GL_FALSE, glm::value_ptr(model));
					glUniformMatrix4fv(terrainViewProjectionLocation, 1, GL_FALSE, glm::value_ptr(viewProjection));
					glUniformMatrix3fv(terrainNormalMatrixLocation, 1, GL_FALSE, glm::value_ptr(normalMatrix));
					glUniform3fv(terrainFallbackColorLocation, 1, glm::value_ptr(entity.color));
					glUniform2fv(terrainTileSizeLocation, 1, glm::value_ptr(entity.terrain.uvScale));
					for (std::size_t layerIndex = 0; layerIndex < cacheEntry->second.layers.size(); ++layerIndex)
					{
						const TerrainLayerGpuEntry& layer = cacheEntry->second.layers[layerIndex];
						glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + layerIndex * 3));
						glBindTexture(
							GL_TEXTURE_2D, layer.diffuseTexture != 0 ? layer.diffuseTexture : fallbackWhiteTexture_);
						glUniform1i(terrainDiffuseLocations[layerIndex], static_cast<GLint>(layerIndex * 3));

						glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + layerIndex * 3 + 1));
						glBindTexture(
							GL_TEXTURE_2D,
							layer.normalTexture != 0 ? layer.normalTexture : fallbackFlatNormalTexture_);
						glUniform1i(terrainNormalLocations[layerIndex], static_cast<GLint>(layerIndex * 3 + 1));

						glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + layerIndex * 3 + 2));
						glBindTexture(
							GL_TEXTURE_2D,
							layer.heightTexture != 0 ? layer.heightTexture : fallbackMidHeightTexture_);
						glUniform1i(terrainHeightLocations[layerIndex], static_cast<GLint>(layerIndex * 3 + 2));
					}
					glBindVertexArray(cacheEntry->second.vertexArray);
					glDrawArrays(GL_TRIANGLES, 0, cacheEntry->second.vertexCount);
					glActiveTexture(GL_TEXTURE0);
					glUseProgram(meshShaderProgram_);
				}
				continue;
			}

			if (entity.isImportedMesh)
			{
				ensureImportedMeshGpu(entity, projectRoot);
				const auto cacheEntry = importedMeshCache_.find(entity.id);
				if (cacheEntry != importedMeshCache_.end() && cacheEntry->second.vertexCount > 0)
				{
					if (cacheEntry->second.hasSkeleton)
					{
						// Switch to the skinning program just for this draw -
						// re-set the uniforms it needs (a different program
						// means different uniform locations, even for
						// same-named uniforms) and switch back to
						// meshShaderProgram_ immediately after so the next
						// loop iteration's cached modelLocation/etc. target
						// the right active program again.
						glUseProgram(skinnedMeshShaderProgram_);
						glUniformMatrix4fv(skinnedModelLocation, 1, GL_FALSE, glm::value_ptr(model));
						glUniformMatrix4fv(
							skinnedViewProjectionLocation, 1, GL_FALSE, glm::value_ptr(viewProjection));
						glUniformMatrix3fv(skinnedNormalMatrixLocation, 1, GL_FALSE, glm::value_ptr(normalMatrix));
						glUniform3fv(skinnedBaseColorLocation, 1, glm::value_ptr(entity.color));
						const std::vector<glm::mat4> boneMatrices = computeSkinningMatrices(
							cacheEntry->second.bones, cacheEntry->second.animations, glfwGetTime());
						// The shader declares a fixed `uniform mat4 boneMatrices[128]`.
						// glUniformMatrix4fv with `count < 128` only updates the first
						// `count` entries; the remaining slots keep whatever the
						// previous draw call set (or zero at program creation). A
						// model with bone index >= bones.size() would then read
						// stale garbage and produce wrong skinned positions.
						// Always upload all 128 slots: real matrices where
						// available, identity for the rest.
						if (!boneMatrices.empty())
						{
							std::array<glm::mat4, 128> padded{};
							std::array<glm::mat3, 128> paddedNormalMat{};
							const std::size_t realCount = std::min<std::size_t>(boneMatrices.size(), padded.size());
							for (std::size_t i = 0; i < realCount; ++i)
							{
								padded[i] = boneMatrices[i];
								// Normal transform = inverse-transpose of the
								// upper 3x3 of boneMatrices[i]. Computed on
								// the CPU once per frame per bone; the shader
								// then blends the same way it blends bone
								// positions, but using these matrices
								// instead of mat3(skinMatrix) so non-uniform
								// bone scales don't skew normals.
								paddedNormalMat[i] = glm::transpose(glm::inverse(glm::mat3(boneMatrices[i])));
							}
							for (std::size_t i = realCount; i < padded.size(); ++i)
							{
								padded[i] = glm::mat4(1.0F);
								paddedNormalMat[i] = glm::mat3(1.0F);
							}
							glUniformMatrix4fv(
								boneMatricesLocation,
								static_cast<GLsizei>(padded.size()),
								GL_FALSE,
								glm::value_ptr(padded.front()));
							glUniformMatrix3fv(
								boneInverseTransposeMatricesLocation,
								static_cast<GLsizei>(paddedNormalMat.size()),
								GL_FALSE,
								glm::value_ptr(paddedNormalMat.front()));
						}
						glBindVertexArray(cacheEntry->second.vertexArray);
						glDrawArrays(GL_TRIANGLES, 0, cacheEntry->second.vertexCount);
						glUseProgram(meshShaderProgram_);
					}
					else
					{
						glBindVertexArray(cacheEntry->second.vertexArray);
						glDrawArrays(GL_TRIANGLES, 0, cacheEntry->second.vertexCount);
					}
				}
				continue;
			}

			// Cine-camera entities render as a wireframe icon (drawn below, in
			// the line-shader pass) instead of a solid lit primitive.
			if (entity.isCineCamera)
			{
				continue;
			}

			const std::size_t meshIndex = static_cast<std::size_t>(entity.primitive);
			glBindVertexArray(primitiveVertexArrays_[meshIndex]);
			glDrawArrays(GL_TRIANGLES, 0, primitiveVertexCounts_[meshIndex]);
		}
		glBindVertexArray(0);
		glUseProgram(0);

		if (cameraIconVertexArray_ != 0)
		{
			glUseProgram(lineShaderProgram_);
			glBindVertexArray(cameraIconVertexArray_);
			for (const SceneEntity& entity : entities)
			{
				if (!entity.active || !entity.isCineCamera)
				{
					continue;
				}
				const glm::mat4 iconMvp = viewProjection * composeEntityTransform(entity);
				glUniformMatrix4fv(lineMvpLocation, 1, GL_FALSE, glm::value_ptr(iconMvp));
				glDrawArrays(GL_LINES, 0, cameraIconVertexCount_);
			}
			glBindVertexArray(0);
			glUseProgram(0);
		}

		if (!selectedEntityIds.empty())
		{
			glUseProgram(lineShaderProgram_);
			glBindVertexArray(outlineVertexArray_);
			for (const int selectedId : selectedEntityIds)
			{
				const auto iterator = std::find_if(
					entities.begin(),
					entities.end(),
					[selectedId](const SceneEntity& entity) { return entity.id == selectedId; });
				if (iterator == entities.end() || !iterator->active)
				{
					continue;
				}
				SceneEntity outlineEntity = *iterator;
				outlineEntity.scale *= 1.03F;
				const glm::mat4 outlineModel = composeEntityTransform(outlineEntity);
				const glm::mat4 outlineMvp = viewProjection * outlineModel;

				glUniformMatrix4fv(lineMvpLocation, 1, GL_FALSE, glm::value_ptr(outlineMvp));
				glDrawArrays(GL_LINES, 0, outlineVertexCount_);
			}
			glBindVertexArray(0);
			glUseProgram(0);
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void ViewportRenderer::blitToCurrentFramebuffer(const int width, const int height) const
	{
		if (framebuffer_ == 0)
		{
			return;
		}
		glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer_);
		glBlitFramebuffer(0, 0, width_, height_, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	}

	void ViewportRenderer::ensureTextMeshGpu(const SceneEntity& entity, const std::filesystem::path& projectRoot)
	{
		TextMeshGpuEntry& cacheEntry = textMeshCache_[entity.id];
		const TextMeshData& data = entity.textMesh;
		if (cacheEntry.buildAttempted && cacheEntry.lastContent == data.content &&
			cacheEntry.lastFontPath == data.fontPath && cacheEntry.lastFontSize == data.fontSize &&
			cacheEntry.lastDepth == data.depth)
		{
			return;
		}

		const std::optional<std::filesystem::path> resolvedFontPath =
			core::resolveProjectFile(projectRoot, data.fontPath, "Game/Fonts");
		const TextMeshBuildResult built = resolvedFontPath.has_value()
			? buildTextMesh(*resolvedFontPath, data.content, data.fontSize, data.depth)
			: TextMeshBuildResult{};
		cacheEntry.lastContent = data.content;
		cacheEntry.lastFontPath = data.fontPath;
		cacheEntry.lastFontSize = data.fontSize;
		cacheEntry.lastDepth = data.depth;
		cacheEntry.buildAttempted = true;

		if (!built.success || built.vertices.empty())
		{
			cacheEntry.vertexCount = 0;
			return;
		}

		if (cacheEntry.vertexArray == 0)
		{
			glGenVertexArrays(1, &cacheEntry.vertexArray);
			glGenBuffers(1, &cacheEntry.vertexBuffer);
		}
		glBindVertexArray(cacheEntry.vertexArray);
		glBindBuffer(GL_ARRAY_BUFFER, cacheEntry.vertexBuffer);
		glBufferData(
			GL_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(built.vertices.size() * sizeof(float)),
			built.vertices.data(),
			GL_DYNAMIC_DRAW);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(
			1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(3 * sizeof(float)));
		glEnableVertexAttribArray(1);
		glBindVertexArray(0);

		cacheEntry.vertexCount = static_cast<GLsizei>(built.vertices.size() / 6);
	}

	void ViewportRenderer::ensureTerrainGpu(const SceneEntity& entity, const std::filesystem::path& projectRoot)
	{
		TerrainGpuEntry& cacheEntry = terrainCache_[entity.id];
		const TerrainData& data = entity.terrain;
		const bool meshDirty = cacheEntry.vertexArray == 0 || cacheEntry.lastResolution != data.resolution ||
			cacheEntry.lastWorldSize != data.worldSize || cacheEntry.lastHeightScale != data.heightScale ||
			cacheEntry.lastHeights != data.heights || cacheEntry.lastSplatWeights != data.splatWeights;

		if (meshDirty)
		{
			const std::vector<float> vertices = buildTerrainMesh(
				data.resolution, data.worldSize, data.heightScale, data.heights, data.splatWeights);
			cacheEntry.lastResolution = data.resolution;
			cacheEntry.lastWorldSize = data.worldSize;
			cacheEntry.lastHeightScale = data.heightScale;
			cacheEntry.lastHeights = data.heights;
			cacheEntry.lastSplatWeights = data.splatWeights;

			if (vertices.empty())
			{
				cacheEntry.vertexCount = 0;
			}
			else
			{
				if (cacheEntry.vertexArray == 0)
				{
					glGenVertexArrays(1, &cacheEntry.vertexArray);
					glGenBuffers(1, &cacheEntry.vertexBuffer);
				}
				glBindVertexArray(cacheEntry.vertexArray);
				glBindBuffer(GL_ARRAY_BUFFER, cacheEntry.vertexBuffer);
				glBufferData(
					GL_ARRAY_BUFFER,
					static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
					vertices.data(),
					GL_DYNAMIC_DRAW);
				glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), nullptr);
				glEnableVertexAttribArray(0);
				glVertexAttribPointer(
					1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), reinterpret_cast<const void*>(3 * sizeof(float)));
				glEnableVertexAttribArray(1);
				glVertexAttribPointer(
					2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), reinterpret_cast<const void*>(6 * sizeof(float)));
				glEnableVertexAttribArray(2);
				glBindVertexArray(0);

				cacheEntry.vertexCount = static_cast<GLsizei>(vertices.size() / 9);
			}
		}

		for (std::size_t index = 0; index < cacheEntry.layers.size() && index < data.layers.size(); ++index)
		{
			ensureTerrainLayerTexturesGpu(cacheEntry.layers[index], data.layers[index], projectRoot);
		}
	}

	void ViewportRenderer::ensureTerrainLayerTexturesGpu(
		TerrainLayerGpuEntry& layerEntry, const TerrainLayerData& layerData, const std::filesystem::path& projectRoot)
	{
		if (layerEntry.loadAttempted && layerEntry.lastDiffusePath == layerData.diffusePath &&
			layerEntry.lastNormalPath == layerData.normalPath && layerEntry.lastHeightPath == layerData.heightPath)
		{
			return;
		}
		layerEntry.lastDiffusePath = layerData.diffusePath;
		layerEntry.lastNormalPath = layerData.normalPath;
		layerEntry.lastHeightPath = layerData.heightPath;
		layerEntry.loadAttempted = true;

		const auto uploadOrClear = [](GLuint& textureId, const LoadedTexture& image)
		{
			if (!image.success)
			{
				if (textureId != 0)
				{
					glDeleteTextures(1, &textureId);
					textureId = 0;
				}
				return;
			}
			if (textureId == 0)
			{
				glGenTextures(1, &textureId);
			}
			glBindTexture(GL_TEXTURE_2D, textureId);
			glTexImage2D(
				GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
				image.rgba.data());
			glGenerateMipmap(GL_TEXTURE_2D);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glBindTexture(GL_TEXTURE_2D, 0);
		};

		if (layerData.diffusePath.empty())
		{
			uploadOrClear(layerEntry.diffuseTexture, LoadedTexture{});
			uploadOrClear(layerEntry.normalTexture, LoadedTexture{});
			uploadOrClear(layerEntry.heightTexture, LoadedTexture{});
			return;
		}

		// Terrain/material texture paths come from scene JSON - confine them to
		// Game/Textures the same way model and font paths are confined above.
		const auto loadConfinedTexture = [&projectRoot](const std::string& relativePath) -> LoadedTexture
		{
			const std::optional<std::filesystem::path> resolved =
				core::resolveProjectFile(projectRoot, relativePath, "Game/Textures");
			return resolved.has_value() ? loadTextureImage(*resolved) : LoadedTexture{};
		};

		const LoadedTexture diffuse = loadConfinedTexture(layerData.diffusePath);
		uploadOrClear(layerEntry.diffuseTexture, diffuse);

		const LoadedTexture normalImage = layerData.normalPath.empty()
			? generateNormalMapFromDiffuse(diffuse)
			: loadConfinedTexture(layerData.normalPath);
		uploadOrClear(layerEntry.normalTexture, normalImage);

		const LoadedTexture heightImage = layerData.heightPath.empty()
			? generateHeightMapFromDiffuse(diffuse)
			: loadConfinedTexture(layerData.heightPath);
		uploadOrClear(layerEntry.heightTexture, heightImage);
	}

	std::vector<glm::mat4> ViewportRenderer::computeSkinningMatrices(
		const std::vector<ImportedBone>& bones,
		const std::vector<ImportedAnimationClip>& animations,
		const double timeSeconds) const
	{
		std::vector<glm::mat4> worldTransforms(bones.size(), glm::mat4(1.0F));
		const ImportedAnimationClip* clip = animations.empty() ? nullptr : &animations.front();
		float clipTime = 0.0F;
		if (clip != nullptr && clip->durationSeconds > 0.0F)
		{
			clipTime = std::fmod(static_cast<float>(timeSeconds), clip->durationSeconds);
		}

		for (std::size_t index = 0; index < bones.size(); ++index)
		{
			const ImportedBone& bone = bones[index];
			glm::mat4 localTransform = bone.localBindTransform;
			if (clip != nullptr)
			{
				for (const ImportedBoneTrack& track : clip->tracks)
				{
					if (track.boneIndex != static_cast<int>(index))
					{
						continue;
					}
					// Fall back to the bone's OWN bind-pose translation/
					// rotation/scale per component, not a hardcoded identity
					// - a channel that only animates e.g. rotation (position/
					// scale key arrays empty) should keep this bone's real
					// bind position/scale, not snap it to the origin/unit
					// scale.
					const DecomposedTransform bind = decomposeTRS(bone.localBindTransform);
					const glm::vec3 position = sampleVectorTrack(track.positionKeys, clipTime, bind.translation);
					const glm::quat rotation = sampleQuatTrack(track.rotationKeys, clipTime, bind.rotation);
					const glm::vec3 scale = sampleVectorTrack(track.scaleKeys, clipTime, bind.scale);
					localTransform = glm::translate(glm::mat4(1.0F), position) * glm::mat4_cast(rotation) *
						glm::scale(glm::mat4(1.0F), scale);
					break;
				}
			}
			const glm::mat4 parentWorld =
				bone.parent >= 0 ? worldTransforms[static_cast<std::size_t>(bone.parent)] : glm::mat4(1.0F);
			worldTransforms[index] = parentWorld * localTransform;
		}

		// bones[0] is always the file's true scene root (buildHierarchy in
		// ModelImport.cpp walks depth-first starting there) - its bind-pose
		// transform needs to be factored back out of the palette. Vertex
		// weights (aiBone::mOffsetMatrix) are defined relative to "mesh
		// space" as the exporter/Assimp established it, which does not
		// double-count the scene root's own transform the way naively
		// walking worldTransforms from that same root does - most files
		// have an identity root transform, where this is a no-op, but files
		// with a non-identity root (axis/unit-conversion nodes some
		// exporters add) would otherwise render skinned meshes visibly
		// mis-scaled/rotated/offset even though the static (non-skinned)
		// import path looks correct.
		const glm::mat4 globalInverseTransform =
			bones.empty() ? glm::mat4(1.0F) : glm::inverse(bones.front().localBindTransform);

		std::vector<glm::mat4> skinningMatrices(bones.size());
		for (std::size_t index = 0; index < bones.size(); ++index)
		{
			skinningMatrices[index] = globalInverseTransform * worldTransforms[index] * bones[index].inverseBindMatrix;
		}
		return skinningMatrices;
	}

	void ViewportRenderer::ensureImportedMeshGpu(const SceneEntity& entity, const std::filesystem::path& projectRoot)
	{
		ImportedMeshGpuEntry& cacheEntry = importedMeshCache_[entity.id];
		const ImportedMeshData& data = entity.importedMesh;
		if (cacheEntry.loadAttempted && cacheEntry.lastSourcePath == data.sourcePath)
		{
			return;
		}

		cacheEntry.lastSourcePath = data.sourcePath;
		cacheEntry.loadAttempted = true;

		// Model paths come from scene JSON, which a project file could carry
		// a crafted "../../" sourcePath in - confine resolution to Game/Models
		// the same way scripts are confined to Game/Scripts (see ProjectPaths.hpp).
		const std::optional<std::filesystem::path> resolvedPath =
			core::resolveProjectFile(projectRoot, data.sourcePath, "Game/Models");
		if (!resolvedPath.has_value())
		{
			cacheEntry.vertexCount = 0;
			return;
		}

		const ModelImportResult built = loadModelMesh(*resolvedPath);
		cacheEntry.hasSkeleton = built.hasSkeleton;
		cacheEntry.vertexStride = built.vertexStride;
		cacheEntry.bones = built.bones;
		cacheEntry.animations = built.animations;

		if (!built.success || built.vertices.empty())
		{
			cacheEntry.vertexCount = 0;
			return;
		}

		if (cacheEntry.vertexArray == 0)
		{
			glGenVertexArrays(1, &cacheEntry.vertexArray);
			glGenBuffers(1, &cacheEntry.vertexBuffer);
		}
		glBindVertexArray(cacheEntry.vertexArray);
		glBindBuffer(GL_ARRAY_BUFFER, cacheEntry.vertexBuffer);
		glBufferData(
			GL_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(built.vertices.size() * sizeof(float)),
			built.vertices.data(),
			GL_DYNAMIC_DRAW);
		const GLsizei stride = static_cast<GLsizei>(built.vertexStride) * static_cast<GLsizei>(sizeof(float));
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(3 * sizeof(float)));
		glEnableVertexAttribArray(1);
		if (built.hasSkeleton)
		{
			glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(6 * sizeof(float)));
			glEnableVertexAttribArray(2);
			glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(10 * sizeof(float)));
			glEnableVertexAttribArray(3);
		}
		else
		{
			// A VAO can be reused across an entity's imports (e.g. its
			// source path was pointed at a different file) - explicitly
			// disable these in case a previous build of this same cache
			// entry was skinned and left them enabled.
			glDisableVertexAttribArray(2);
			glDisableVertexAttribArray(3);
		}
		glBindVertexArray(0);

		cacheEntry.vertexCount = static_cast<GLsizei>(built.vertices.size() / static_cast<std::size_t>(built.vertexStride));
	}

	void ViewportRenderer::setCamera(
		const float yaw,
		const float pitch,
		const float distance,
		const glm::vec3& target) noexcept
	{
		cameraYaw_ = yaw;
		cameraPitch_ = glm::clamp(pitch, -1.45F, 1.45F);
		cameraDistance_ = glm::clamp(distance, 1.5F, 60.0F);
		cameraTarget_ = target;
	}

	void ViewportRenderer::shutdown() noexcept
	{
		if (depthBuffer_ != 0)
		{
			glDeleteRenderbuffers(1, &depthBuffer_);
			depthBuffer_ = 0;
		}
		if (colorTexture_ != 0)
		{
			glDeleteTextures(1, &colorTexture_);
			colorTexture_ = 0;
		}
		if (framebuffer_ != 0)
		{
			glDeleteFramebuffers(1, &framebuffer_);
			framebuffer_ = 0;
		}
		for (std::size_t index = 0; index < primitiveVertexArrays_.size(); ++index)
		{
			if (primitiveVertexBuffers_[index] != 0)
			{
				glDeleteBuffers(1, &primitiveVertexBuffers_[index]);
				primitiveVertexBuffers_[index] = 0;
			}
			if (primitiveVertexArrays_[index] != 0)
			{
				glDeleteVertexArrays(1, &primitiveVertexArrays_[index]);
				primitiveVertexArrays_[index] = 0;
			}
			primitiveVertexCounts_[index] = 0;
		}
		for (auto& [id, entry] : textMeshCache_)
		{
			if (entry.vertexBuffer != 0)
			{
				glDeleteBuffers(1, &entry.vertexBuffer);
			}
			if (entry.vertexArray != 0)
			{
				glDeleteVertexArrays(1, &entry.vertexArray);
			}
		}
		textMeshCache_.clear();
		for (auto& [id, entry] : terrainCache_)
		{
			if (entry.vertexBuffer != 0)
			{
				glDeleteBuffers(1, &entry.vertexBuffer);
			}
			if (entry.vertexArray != 0)
			{
				glDeleteVertexArrays(1, &entry.vertexArray);
			}
			for (TerrainLayerGpuEntry& layer : entry.layers)
			{
				if (layer.diffuseTexture != 0)
				{
					glDeleteTextures(1, &layer.diffuseTexture);
				}
				if (layer.normalTexture != 0)
				{
					glDeleteTextures(1, &layer.normalTexture);
				}
				if (layer.heightTexture != 0)
				{
					glDeleteTextures(1, &layer.heightTexture);
				}
			}
		}
		terrainCache_.clear();
		if (fallbackWhiteTexture_ != 0)
		{
			glDeleteTextures(1, &fallbackWhiteTexture_);
			fallbackWhiteTexture_ = 0;
		}
		if (fallbackFlatNormalTexture_ != 0)
		{
			glDeleteTextures(1, &fallbackFlatNormalTexture_);
			fallbackFlatNormalTexture_ = 0;
		}
		if (fallbackMidHeightTexture_ != 0)
		{
			glDeleteTextures(1, &fallbackMidHeightTexture_);
			fallbackMidHeightTexture_ = 0;
		}
		for (auto& [id, entry] : importedMeshCache_)
		{
			if (entry.vertexBuffer != 0)
			{
				glDeleteBuffers(1, &entry.vertexBuffer);
			}
			if (entry.vertexArray != 0)
			{
				glDeleteVertexArrays(1, &entry.vertexArray);
			}
		}
		importedMeshCache_.clear();
		if (gridVertexBuffer_ != 0)
		{
			glDeleteBuffers(1, &gridVertexBuffer_);
			gridVertexBuffer_ = 0;
		}
		if (gridVertexArray_ != 0)
		{
			glDeleteVertexArrays(1, &gridVertexArray_);
			gridVertexArray_ = 0;
		}
		gridVertexCount_ = 0;
		if (outlineVertexBuffer_ != 0)
		{
			glDeleteBuffers(1, &outlineVertexBuffer_);
			outlineVertexBuffer_ = 0;
		}
		if (outlineVertexArray_ != 0)
		{
			glDeleteVertexArrays(1, &outlineVertexArray_);
			outlineVertexArray_ = 0;
		}
		outlineVertexCount_ = 0;
		if (cameraIconVertexBuffer_ != 0)
		{
			glDeleteBuffers(1, &cameraIconVertexBuffer_);
			cameraIconVertexBuffer_ = 0;
		}
		if (cameraIconVertexArray_ != 0)
		{
			glDeleteVertexArrays(1, &cameraIconVertexArray_);
			cameraIconVertexArray_ = 0;
		}
		cameraIconVertexCount_ = 0;
		if (lineShaderProgram_ != 0)
		{
			glDeleteProgram(lineShaderProgram_);
			lineShaderProgram_ = 0;
		}
		if (meshShaderProgram_ != 0)
		{
			glDeleteProgram(meshShaderProgram_);
			meshShaderProgram_ = 0;
		}
		if (skinnedMeshShaderProgram_ != 0)
		{
			glDeleteProgram(skinnedMeshShaderProgram_);
			skinnedMeshShaderProgram_ = 0;
		}
		if (terrainShaderProgram_ != 0)
		{
			glDeleteProgram(terrainShaderProgram_);
			terrainShaderProgram_ = 0;
		}
		if (texturedMeshShaderProgram_ != 0)
		{
			glDeleteProgram(texturedMeshShaderProgram_);
			texturedMeshShaderProgram_ = 0;
		}
		for (auto& [id, layers] : materialCache_)
		{
			for (TerrainLayerGpuEntry& layer : layers)
			{
				if (layer.diffuseTexture != 0)
				{
					glDeleteTextures(1, &layer.diffuseTexture);
				}
				if (layer.normalTexture != 0)
				{
					glDeleteTextures(1, &layer.normalTexture);
				}
				if (layer.heightTexture != 0)
				{
					glDeleteTextures(1, &layer.heightTexture);
				}
			}
		}
		materialCache_.clear();
		width_ = 0;
		height_ = 0;
	}

	GLuint ViewportRenderer::texture() const noexcept
	{
		return colorTexture_;
	}

	int ViewportRenderer::width() const noexcept
	{
		return width_;
	}

	int ViewportRenderer::height() const noexcept
	{
		return height_;
	}

	const glm::mat4& ViewportRenderer::view() const noexcept
	{
		return lastView_;
	}

	const glm::mat4& ViewportRenderer::projection() const noexcept
	{
		return lastProjection_;
	}

	glm::vec3 ViewportRenderer::cameraPosition() const noexcept
	{
		return cameraTarget_ + glm::vec3(
			cameraDistance_ * std::cos(cameraPitch_) * std::sin(cameraYaw_),
			cameraDistance_ * std::sin(cameraPitch_),
			cameraDistance_ * std::cos(cameraPitch_) * std::cos(cameraYaw_));
	}

	bool ViewportRenderer::createShaderPrograms()
	{
		lineShaderProgram_ = compileProgram(lineVertexShaderSource, lineFragmentShaderSource);
		if (lineShaderProgram_ == 0)
		{
			return false;
		}

		meshShaderProgram_ = compileProgram(meshVertexShaderSource, meshFragmentShaderSource);
		if (meshShaderProgram_ == 0)
		{
			glDeleteProgram(lineShaderProgram_);
			lineShaderProgram_ = 0;
			return false;
		}

		skinnedMeshShaderProgram_ = compileProgram(skinnedMeshVertexShaderSource, meshFragmentShaderSource);
		if (skinnedMeshShaderProgram_ == 0)
		{
			glDeleteProgram(lineShaderProgram_);
			lineShaderProgram_ = 0;
			glDeleteProgram(meshShaderProgram_);
			meshShaderProgram_ = 0;
			return false;
		}

		terrainShaderProgram_ = compileProgram(terrainVertexShaderSource, terrainFragmentShaderSource);
		if (terrainShaderProgram_ == 0)
		{
			glDeleteProgram(lineShaderProgram_);
			lineShaderProgram_ = 0;
			glDeleteProgram(meshShaderProgram_);
			meshShaderProgram_ = 0;
			glDeleteProgram(skinnedMeshShaderProgram_);
			skinnedMeshShaderProgram_ = 0;
			return false;
		}

		texturedMeshShaderProgram_ = compileProgram(texturedMeshVertexShaderSource, texturedMeshFragmentShaderSource);
		if (texturedMeshShaderProgram_ == 0)
		{
			glDeleteProgram(lineShaderProgram_);
			lineShaderProgram_ = 0;
			glDeleteProgram(meshShaderProgram_);
			meshShaderProgram_ = 0;
			glDeleteProgram(skinnedMeshShaderProgram_);
			skinnedMeshShaderProgram_ = 0;
			glDeleteProgram(terrainShaderProgram_);
			terrainShaderProgram_ = 0;
			return false;
		}

		return true;
	}

	bool ViewportRenderer::createFramebuffer(const int width, const int height)
	{
		if (framebuffer_ != 0)
		{
			glDeleteRenderbuffers(1, &depthBuffer_);
			glDeleteTextures(1, &colorTexture_);
			glDeleteFramebuffers(1, &framebuffer_);
			depthBuffer_ = 0;
			colorTexture_ = 0;
			framebuffer_ = 0;
		}

		glGenFramebuffers(1, &framebuffer_);
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);

		glGenTextures(1, &colorTexture_);
		glBindTexture(GL_TEXTURE_2D, colorTexture_);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture_, 0);

		glGenRenderbuffers(1, &depthBuffer_);
		glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer_);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthBuffer_);

		const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
		if (!complete)
		{
			std::fprintf(stderr, "Viewport framebuffer is incomplete.\n");
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		width_ = complete ? width : 0;
		height_ = complete ? height : 0;
		return complete;
	}
}
