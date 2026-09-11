#include "GameForger/Editor/ViewportRenderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat3x3.hpp>

#include "GameForger/Core/ProjectPaths.hpp"
#include "GameForger/Editor/ModelImport.hpp"
#include "GameForger/Editor/OverlayFont.hpp"
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

		// Shared lighting block, textually prepended to every lit fragment
		// shader (see makeLitFragmentShader below). Before this existed, all
		// four fragment shaders inlined `normalize(vec3(0.4, 0.85, 0.35))` as
		// the one and only light, five times over - changing the sun meant
		// editing five string literals, and there was no way to author a
		// light at all.
		//
		// Deliberately NOT physically-based. This renderer has no PBR, no HDR
		// and no tonemapping, so a true inverse-square falloff would make a
		// point light at intensity 1 essentially invisible two metres away
		// and force absurd intensity values to compensate. Falloff here is a
		// smooth window: full brightness at the light, zero at `range`, which
		// is what the intensity slider needs to feel predictable.
		//
		// Shadows come from ONE atlas texture rather than an array of
		// samplers: GLSL cannot index a sampler array with a non-constant
		// expression without extra extensions, and a per-light tile in a
		// single texture sidesteps that entirely while keeping one texture
		// unit bound for any number of lights.
		constexpr const char* lightingCommonSource = R"glsl(
#define GF_MAX_LIGHTS 8

uniform int lightCount;
uniform int lightType[GF_MAX_LIGHTS];        // 0 = directional, 1 = point, 2 = spot
uniform vec3 lightDirection[GF_MAX_LIGHTS];  // normalized, world space, points AWAY from the light
uniform vec3 lightPosition[GF_MAX_LIGHTS];
uniform vec3 lightColor[GF_MAX_LIGHTS];      // color premultiplied by intensity
uniform float lightRange[GF_MAX_LIGHTS];
uniform float lightCosInner[GF_MAX_LIGHTS];
uniform float lightCosOuter[GF_MAX_LIGHTS];
uniform int lightShadowSlot[GF_MAX_LIGHTS];  // atlas tile index, or -1 for no shadow
uniform float lightShadowBias[GF_MAX_LIGHTS];
uniform mat4 lightViewProjection[GF_MAX_LIGHTS];
uniform vec3 ambientColor;

uniform sampler2D shadowAtlas;
uniform vec2 shadowTileScale;   // one tile's size as a fraction of the whole atlas
uniform int shadowTilesPerRow;

// 1.0 = fully lit, 0.0 = fully shadowed. 3x3 PCF inside this light's own
// atlas tile; samples are clamped to the tile so a filter tap can never bleed
// into a neighbouring light's depth.
float gfShadowFactor(int index, vec3 worldPos, float normalDotLight)
{
	int slot = lightShadowSlot[index];
	if (slot < 0)
	{
		return 1.0;
	}
	vec4 lightClip = lightViewProjection[index] * vec4(worldPos, 1.0);
	if (lightClip.w <= 0.0)
	{
		return 1.0;
	}
	vec3 projected = lightClip.xyz / lightClip.w;
	projected = projected * 0.5 + 0.5;
	// Outside the light's own frustum: unshadowed rather than black, so a
	// scene bigger than the shadow frustum degrades to "no shadow there"
	// instead of a hard dark edge.
	if (projected.z > 1.0 || projected.x < 0.0 || projected.x > 1.0
		|| projected.y < 0.0 || projected.y > 1.0)
	{
		return 1.0;
	}

	int tileRow = slot / shadowTilesPerRow;
	int tileColumn = slot - tileRow * shadowTilesPerRow;
	vec2 tileOrigin = vec2(float(tileColumn), float(tileRow)) * shadowTileScale;

	// Slope-scaled: a surface nearly edge-on to the light needs far more bias
	// than one facing it, which is what stops acne without peter-panning.
	float bias = max(lightShadowBias[index] * (1.0 - normalDotLight) * 4.0,
		lightShadowBias[index] * 0.5);
	vec2 atlasTexel = 1.0 / vec2(textureSize(shadowAtlas, 0));
	vec2 tileLocalTexel = atlasTexel / shadowTileScale;

	float lit = 0.0;
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			vec2 local = clamp(projected.xy + vec2(float(x), float(y)) * tileLocalTexel,
				vec2(0.0015), vec2(0.9985));
			float occluderDepth = texture(shadowAtlas, tileOrigin + local * shadowTileScale).r;
			lit += (projected.z - bias) > occluderDepth ? 0.0 : 1.0;
		}
	}
	return lit / 9.0;
}

vec3 gfShade(vec3 baseColor, vec3 rawNormal, vec3 worldPos)
{
	vec3 surfaceNormal = normalize(rawNormal);
	vec3 result = ambientColor * baseColor;
	for (int i = 0; i < lightCount; ++i)
	{
		vec3 toLight;
		float attenuation = 1.0;
		if (lightType[i] == 0)
		{
			toLight = -lightDirection[i];
		}
		else
		{
			vec3 delta = lightPosition[i] - worldPos;
			float distanceToLight = length(delta);
			if (distanceToLight > lightRange[i])
			{
				continue;
			}
			toLight = delta / max(distanceToLight, 0.0001);
			float normalized = clamp(distanceToLight / max(lightRange[i], 0.0001), 0.0, 1.0);
			float window = 1.0 - normalized * normalized;
			attenuation = window * window;
			if (lightType[i] == 2)
			{
				float cosAngle = dot(-toLight, lightDirection[i]);
				float cone = clamp(
					(cosAngle - lightCosOuter[i]) / max(lightCosInner[i] - lightCosOuter[i], 0.0001),
					0.0, 1.0);
				attenuation *= cone * cone;
			}
		}
		float normalDotLight = max(dot(surfaceNormal, toLight), 0.0);
		if (normalDotLight <= 0.0 || attenuation <= 0.0)
		{
			continue;
		}
		float shadow = gfShadowFactor(i, worldPos, normalDotLight);
		result += baseColor * lightColor[i] * normalDotLight * attenuation * shadow;
	}
	return result;
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
out vec3 worldPosition;

void main()
{
	worldNormal = normalMatrix * normal;
	vec4 worldPos4 = model * vec4(position, 1.0);
	worldPosition = worldPos4.xyz;
	gl_Position = viewProjection * worldPos4;
}
)glsl";

		constexpr const char* meshFragmentShaderSource = R"glsl(
in vec3 worldNormal;
in vec3 worldPosition;
uniform vec3 baseColor;
out vec4 fragmentColor;

void main()
{
	fragmentColor = vec4(gfShade(baseColor, worldNormal, worldPosition), 1.0);
}
)glsl";

		// Depth-only pass that fills a shadow-atlas tile. No fragment work at
		// all - the default depth write is the entire point - so this shares
		// one trivial fragment stage between the static and skinned variants.
		constexpr const char* shadowDepthVertexShaderSource = R"glsl(
#version 460 core
layout (location = 0) in vec3 position;
uniform mat4 model;
uniform mat4 lightViewProjection;

void main()
{
	gl_Position = lightViewProjection * model * vec4(position, 1.0);
}
)glsl";

		// Skinned casters need the same bone blend the visible pass uses, or
		// an animated character's shadow would freeze in its bind pose while
		// the character itself moves.
		constexpr const char* shadowDepthSkinnedVertexShaderSource = R"glsl(
#version 460 core
layout (location = 0) in vec3 position;
layout (location = 2) in vec4 boneIndices;
layout (location = 3) in vec4 boneWeights;
uniform mat4 model;
uniform mat4 lightViewProjection;
uniform mat4 boneMatrices[128];

void main()
{
	mat4 skinMatrix =
		boneWeights.x * boneMatrices[int(boneIndices.x)] +
		boneWeights.y * boneMatrices[int(boneIndices.y)] +
		boneWeights.z * boneMatrices[int(boneIndices.z)] +
		boneWeights.w * boneMatrices[int(boneIndices.w)];
	gl_Position = lightViewProjection * model * (skinMatrix * vec4(position, 1.0));
}
)glsl";

		constexpr const char* shadowDepthFragmentShaderSource = R"glsl(
#version 460 core

void main()
{
}
)glsl";

		// Full-screen post pass. Draws three vertices covering the screen with
		// no vertex buffer at all - gl_VertexID arithmetic is the standard
		// trick, and it means the post stack needs no VAO of its own beyond an
		// empty one to satisfy core profile.
		//
		// A single triangle rather than a quad: the diagonal seam of a two-
		// triangle quad causes duplicated fragment shading down the middle of
		// the screen, and a triangle big enough to cover the viewport has no
		// seam at all.
		constexpr const char* postVertexShaderSource = R"glsl(
#version 460 core
out vec2 uv;

void main()
{
	vec2 corner = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
	uv = corner;
	gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

		// The cinema stack. Order matters and follows how film actually works:
		// lens distortion first (chromatic aberration happens in the glass),
		// then colour grade, then the named filter, then the gradient map,
		// then the physical film artefacts (grain, scanlines, flicker), and
		// vignette last because it is the lens shading the whole frame.
		constexpr const char* postFragmentShaderSource = R"glsl(
#version 460 core
in vec2 uv;
out vec4 fragmentColor;

uniform sampler2D sceneTexture;
uniform sampler2D gradientTexture;
uniform float time;

uniform int colorFilter;          // matches the ColorFilter enum order
uniform float filterStrength;
uniform vec3 tintColor;
uniform float tintStrength;
uniform float gradientStrength;
uniform float hasGradient;

uniform float brightness;
uniform float contrast;
uniform float saturation;

uniform float grainAmount;
uniform float grainSize;
uniform float flickerAmount;
uniform float flickerSpeed;
uniform float scanlineAmount;
uniform float scanlineCount;
uniform float vignetteAmount;
uniform float vignetteSoftness;
uniform float chromaticAberration;

uniform vec2 viewportSize;

float luminance(vec3 c)
{
	return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Cheap hash noise. Deterministic per pixel per frame, which is what makes
// grain shimmer instead of crawling.
float hash(vec2 p)
{
	return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

vec3 applyNamedFilter(vec3 c, int which)
{
	if (which == 1) // BlackAndWhite
	{
		return vec3(luminance(c));
	}
	if (which == 2) // Sepia
	{
		float l = luminance(c);
		return vec3(l * 1.07, l * 0.94, l * 0.74);
	}
	if (which == 3) // Technicolor - push each channel away from the others
	{
		vec3 boosted = c * c * 1.35;
		return clamp(boosted + (c - vec3(luminance(c))) * 0.55, 0.0, 1.0);
	}
	if (which == 4) // Cold
	{
		return clamp(c * vec3(0.78, 0.92, 1.28), 0.0, 1.0);
	}
	if (which == 5) // Warm
	{
		return clamp(c * vec3(1.25, 1.02, 0.74), 0.0, 1.0);
	}
	if (which == 6) // Infrared - swap foliage response into magenta/white
	{
		float l = luminance(c);
		return clamp(vec3(c.g * 1.4, l * 0.5, c.r * 1.1), 0.0, 1.0);
	}
	if (which == 7) // HeatMap - black -> blue -> red -> yellow -> white
	{
		float l = clamp(luminance(c), 0.0, 1.0);
		vec3 result;
		if (l < 0.25)      { result = mix(vec3(0.0, 0.0, 0.0), vec3(0.0, 0.0, 0.8), l / 0.25); }
		else if (l < 0.5)  { result = mix(vec3(0.0, 0.0, 0.8), vec3(0.85, 0.0, 0.0), (l - 0.25) / 0.25); }
		else if (l < 0.75) { result = mix(vec3(0.85, 0.0, 0.0), vec3(1.0, 0.95, 0.0), (l - 0.5) / 0.25); }
		else               { result = mix(vec3(1.0, 0.95, 0.0), vec3(1.0, 1.0, 1.0), (l - 0.75) / 0.25); }
		return result;
	}
	return c;
}

void main()
{
	vec2 centered = uv - 0.5;

	// --- lens: chromatic aberration, scaled by distance from centre so the
	// middle of the frame stays sharp, like a real lens.
	vec3 color;
	if (chromaticAberration > 0.0)
	{
		vec2 offset = centered * chromaticAberration * 0.02;
		color.r = texture(sceneTexture, uv + offset).r;
		color.g = texture(sceneTexture, uv).g;
		color.b = texture(sceneTexture, uv - offset).b;
	}
	else
	{
		color = texture(sceneTexture, uv).rgb;
	}

	// --- grade
	color = clamp(color + brightness, 0.0, 1.0);
	color = clamp((color - 0.5) * contrast + 0.5, 0.0, 1.0);
	color = clamp(mix(vec3(luminance(color)), color, saturation), 0.0, 1.0);

	// --- named filter
	if (colorFilter != 0)
	{
		color = mix(color, applyNamedFilter(color, colorFilter), clamp(filterStrength, 0.0, 1.0));
	}

	// --- gradient map: luminance picks a colour along the strip
	if (hasGradient > 0.5 && gradientStrength > 0.0)
	{
		vec3 mapped = texture(gradientTexture, vec2(clamp(luminance(color), 0.0, 1.0), 0.5)).rgb;
		color = mix(color, mapped, clamp(gradientStrength, 0.0, 1.0));
	}

	// --- tint
	if (tintStrength > 0.0)
	{
		color = mix(color, color * tintColor, clamp(tintStrength, 0.0, 1.0));
	}

	// --- projector flicker: a slow wobble plus a faster one, so it does not
	// read as a clean sine.
	if (flickerAmount > 0.0)
	{
		float wobble = sin(time * flickerSpeed) * 0.6 + sin(time * flickerSpeed * 2.7) * 0.4;
		color *= 1.0 + wobble * flickerAmount * 0.12;
	}

	// --- grain
	if (grainAmount > 0.0)
	{
		vec2 grainCell = floor(uv * viewportSize / max(grainSize, 1.0));
		float noise = hash(grainCell + fract(time) * 137.0) - 0.5;
		color += noise * grainAmount * 0.35;
	}

	// --- scanlines
	if (scanlineAmount > 0.0)
	{
		float line = sin(uv.y * scanlineCount * 3.14159265);
		color *= 1.0 - scanlineAmount * 0.5 * (0.5 + 0.5 * line);
	}

	// --- vignette, last: the lens shading everything that came before it.
	if (vignetteAmount > 0.0)
	{
		float dist = length(centered) * 1.4142;
		float falloff = smoothstep(1.0, mix(0.95, 0.15, clamp(vignetteSoftness, 0.0, 1.0)), dist);
		color *= mix(1.0, falloff, clamp(vignetteAmount, 0.0, 1.0));
	}

	fragmentColor = vec4(clamp(color, 0.0, 1.0), 1.0);
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
out vec3 worldPosition;

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
	vec4 worldPos4 = model * skinnedPosition;
	worldPosition = worldPos4.xyz;
	gl_Position = viewProjection * worldPos4;
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
		fragmentColor = vec4(gfShade(fallbackColor, worldNormal, worldPosition), 1.0);
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

	// heightShade stays a plain multiplier on albedo (a cheap AO-ish cue),
	// applied BEFORE lighting so it darkens the material rather than fighting
	// the light rig.
	float heightShade = 0.85 + 0.3 * (heightBoost - 0.5);
	fragmentColor = vec4(gfShade(diffuseColor * heightShade, bumpedNormal, worldPosition), 1.0);
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
out vec3 worldPosition;
out vec3 localPosition;
out vec3 localNormal;

void main()
{
	worldNormal = normalMatrix * normal;
	localPosition = position;
	localNormal = normal;
	vec4 worldPos4 = model * vec4(position, 1.0);
	worldPosition = worldPos4.xyz;
	gl_Position = viewProjection * worldPos4;
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
in vec3 worldNormal;
in vec3 worldPosition;
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
		fragmentColor = vec4(gfShade(fallbackColor, worldNormal, worldPosition), 1.0);
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

	float heightShade = 0.85 + 0.3 * (heightBoost - 0.5);
	fragmentColor = vec4(gfShade(diffuseColor * heightShade, worldNormal, worldPosition), 1.0);
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
		createGizmoMeshes();

		// A failed shadow atlas is NOT fatal: collectLights only hands out
		// shadow slots when shadowAtlasTexture_ is non-zero, so the editor
		// falls back to unshadowed lighting rather than refusing to open on a
		// driver that won't give us a depth-only FBO.
		if (!createOverlayResources())
		{
			std::fprintf(stderr, "HUD overlay unavailable - UI elements will not draw.\n");
		}
		if (!createShadowResources())
		{
			std::fprintf(stderr, "Shadow atlas unavailable - lighting will render without shadows.\n");
		}

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

	ViewportRenderer::GizmoMesh ViewportRenderer::uploadGizmoMesh(const std::vector<float>& vertices) const
	{
		GizmoMesh mesh;
		if (vertices.empty())
		{
			return mesh;
		}
		glGenVertexArrays(1, &mesh.vertexArray);
		glGenBuffers(1, &mesh.vertexBuffer);
		glBindVertexArray(mesh.vertexArray);
		glBindBuffer(GL_ARRAY_BUFFER, mesh.vertexBuffer);
		glBufferData(
			GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(),
			GL_STATIC_DRAW);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(
			1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
		glEnableVertexAttribArray(1);
		glBindVertexArray(0);
		mesh.vertexCount = static_cast<GLsizei>(vertices.size() / 6);
		return mesh;
	}

	void ViewportRenderer::destroyGizmoMesh(GizmoMesh& mesh) const noexcept
	{
		if (mesh.vertexBuffer != 0)
		{
			glDeleteBuffers(1, &mesh.vertexBuffer);
			mesh.vertexBuffer = 0;
		}
		if (mesh.vertexArray != 0)
		{
			glDeleteVertexArrays(1, &mesh.vertexArray);
			mesh.vertexArray = 0;
		}
		mesh.vertexCount = 0;
	}

	void ViewportRenderer::createGizmoMeshes()
	{
		const auto segment =
			[](std::vector<float>& out, const glm::vec3& a, const glm::vec3& b, const glm::vec3& color)
		{
			out.insert(out.end(), {a.x, a.y, a.z, color.r, color.g, color.b});
			out.insert(out.end(), {b.x, b.y, b.z, color.r, color.g, color.b});
		};
		// A ring in the plane spanned by `axisU`/`axisV`. Used for the point
		// light's three orthogonal rings and the spot cone's mouth.
		const auto ring = [&segment](
							  std::vector<float>& out, const glm::vec3& center, const glm::vec3& axisU,
							  const glm::vec3& axisV, const float radius, const int steps,
							  const glm::vec3& color)
		{
			for (int step = 0; step < steps; ++step)
			{
				const float a0 = glm::two_pi<float>() * static_cast<float>(step) / static_cast<float>(steps);
				const float a1 =
					glm::two_pi<float>() * static_cast<float>(step + 1) / static_cast<float>(steps);
				segment(
					out, center + (axisU * std::cos(a0) + axisV * std::sin(a0)) * radius,
					center + (axisU * std::cos(a1) + axisV * std::sin(a1)) * radius, color);
			}
		};

		// Empty: three axis crosshairs, X/Y/Z tinted the usual red/green/blue
		// so it doubles as an orientation reference for whatever hangs off it.
		{
			std::vector<float> vertices;
			segment(vertices, glm::vec3(-0.4F, 0, 0), glm::vec3(0.4F, 0, 0), glm::vec3(0.85F, 0.35F, 0.35F));
			segment(vertices, glm::vec3(0, -0.4F, 0), glm::vec3(0, 0.4F, 0), glm::vec3(0.35F, 0.85F, 0.40F));
			segment(vertices, glm::vec3(0, 0, -0.4F), glm::vec3(0, 0, 0.4F), glm::vec3(0.40F, 0.55F, 0.95F));
			emptyGizmo_ = uploadGizmoMesh(vertices);
		}

		// Directional: a small sun disc with rays, plus a long line down local
		// +Z showing exactly which way it points - the only thing that
		// actually matters for a sun, since its position does not.
		{
			constexpr glm::vec3 sunColor{1.0F, 0.88F, 0.35F};
			std::vector<float> vertices;
			ring(vertices, glm::vec3(0.0F), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), 0.28F, 16, sunColor);
			for (int step = 0; step < 8; ++step)
			{
				const float angle = glm::two_pi<float>() * static_cast<float>(step) / 8.0F;
				const glm::vec3 direction(std::cos(angle), std::sin(angle), 0.0F);
				segment(vertices, direction * 0.38F, direction * 0.58F, sunColor);
			}
			segment(vertices, glm::vec3(0.0F), glm::vec3(0.0F, 0.0F, 1.6F), sunColor);
			directionalLightGizmo_ = uploadGizmoMesh(vertices);
		}

		// Point: three orthogonal rings read as a sphere from any angle. Unit
		// radius, scaled to the light's range at draw time.
		{
			constexpr glm::vec3 pointColor{1.0F, 0.82F, 0.45F};
			std::vector<float> vertices;
			ring(vertices, glm::vec3(0.0F), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), 1.0F, 24, pointColor);
			ring(vertices, glm::vec3(0.0F), glm::vec3(1, 0, 0), glm::vec3(0, 0, 1), 1.0F, 24, pointColor);
			ring(vertices, glm::vec3(0.0F), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1), 1.0F, 24, pointColor);
			pointLightGizmo_ = uploadGizmoMesh(vertices);
		}

		// Spot: a unit cone opening along local +Z. Built at 45 degrees and
		// rescaled per light at draw time from its own outer cone angle, so
		// the gizmo always shows the real cone the shader uses.
		{
			constexpr glm::vec3 spotColor{1.0F, 0.75F, 0.30F};
			std::vector<float> vertices;
			ring(vertices, glm::vec3(0, 0, 1), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), 1.0F, 24, spotColor);
			for (int step = 0; step < 4; ++step)
			{
				const float angle = glm::two_pi<float>() * static_cast<float>(step) / 4.0F;
				segment(
					vertices, glm::vec3(0.0F), glm::vec3(std::cos(angle), std::sin(angle), 1.0F), spotColor);
			}
			spotLightGizmo_ = uploadGizmoMesh(vertices);
		}

		// UI element: a small screen-shaped rectangle with a corner tick. It
		// marks where the element hangs in the hierarchy; the element itself
		// draws as a 2D overlay in the Game view, not here.
		{
			constexpr glm::vec3 uiColor{0.55F, 0.85F, 1.0F};
			std::vector<float> vertices;
			const std::array<glm::vec3, 4> corners{
				glm::vec3(-0.45F, -0.30F, 0.0F), glm::vec3(0.45F, -0.30F, 0.0F),
				glm::vec3(0.45F, 0.30F, 0.0F), glm::vec3(-0.45F, 0.30F, 0.0F)};
			for (std::size_t i = 0; i < corners.size(); ++i)
			{
				segment(vertices, corners[i], corners[(i + 1) % corners.size()], uiColor);
			}
			segment(vertices, glm::vec3(-0.45F, 0.30F, 0.0F), glm::vec3(-0.25F, 0.10F, 0.0F), uiColor);
			uiElementGizmo_ = uploadGizmoMesh(vertices);
		}
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

		if (!createFramebuffer(width, height))
		{
			return false;
		}
		// The post buffer must match the presented one exactly, or the
		// full-screen pass samples at the wrong scale. A failure here is not
		// fatal: render() gates on sceneFramebuffer_ being valid, so the
		// renderer simply draws without effects rather than not drawing.
		if (!createPostResources(width, height))
		{
			std::fprintf(stderr, "Post-process buffer unavailable - camera effects disabled.\n");
		}
		return true;
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

		// Lights first: the shadow pass needs them resolved, and every lit
		// program then uploads the same frameLights_ set, so the Viewport and
		// the Game view can never disagree about the lighting.
		collectLights(entities);
		renderShadowMaps(entities, projectRoot, excludeEntityId);

		// With effects configured the scene renders into its own buffer and
		// the post pass writes the presented one; otherwise it goes straight
		// to the presented buffer exactly as before, so an unconfigured
		// camera costs nothing.
		const bool usePostProcess =
			cameraEffects_.anyEnabled() && postShaderProgram_ != 0 && sceneFramebuffer_ != 0;
		glBindFramebuffer(GL_FRAMEBUFFER, usePostProcess ? sceneFramebuffer_ : framebuffer_);
		glViewport(0, 0, width_, height_);
		glEnable(GL_DEPTH_TEST);
		glClearColor(0.055F, 0.067F, 0.09F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		const glm::vec3 eye = cameraPosition();
		lastProjection_ = glm::perspective(
			glm::radians(cameraFieldOfViewDegrees_),
			static_cast<float>(width_) / static_cast<float>(height_),
			cameraNearClip_,
			cameraFarClip_);
		lastView_ = glm::lookAt(eye, cameraTarget_, glm::vec3(0.0F, 1.0F, 0.0F));
		const glm::mat4 viewProjection = lastProjection_ * lastView_;

		glUseProgram(lineShaderProgram_);
		const GLint lineMvpLocation = glGetUniformLocation(lineShaderProgram_, "modelViewProjection");
		glUniformMatrix4fv(lineMvpLocation, 1, GL_FALSE, glm::value_ptr(viewProjection));
		if (showEditorGizmos_)
		{
			glBindVertexArray(gridVertexArray_);
			glDrawArrays(GL_LINES, 0, gridVertexCount_);
		}
		glBindVertexArray(0);

		// Every lit program gets the same light set. Done once per frame here
		// rather than per entity - the uniforms don't vary per draw, and the
		// four programs each keep their own copy of the uniform state.
		for (const GLuint litProgram :
			{meshShaderProgram_, skinnedMeshShaderProgram_, terrainShaderProgram_, texturedMeshShaderProgram_})
		{
			glUseProgram(litProgram);
			uploadLightUniforms(litProgram);
		}

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

		for (const SceneEntity& entity : entities)
		{
			if (!entity.active || entity.id == excludeEntityId)
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

			// Cine cameras, lights, cameras, UI elements and Empties have no
			// solid mesh - they draw as wireframe gizmos in the line-shader
			// pass below (UI elements draw as a screen overlay in the Game
			// view and nothing at all here). One predicate covers all of them
			// so a future gizmo kind cannot miss this check; before it existed
			// only isCineCamera was tested here.
			if (isGizmoOnlyEntity(entity))
			{
				continue;
			}

			const std::size_t meshIndex = static_cast<std::size_t>(entity.primitive);
			// primitiveVertexArrays_ only holds the real meshes, so an enum
			// value past the end (PrimitiveType::Empty) must never reach the
			// index. isGizmoOnlyEntity above already returns for Empty; this
			// is the second line of defence, because reading past the array
			// would be a silent out-of-bounds rather than a visible bug.
			if (meshIndex >= primitiveVertexArrays_.size())
			{
				continue;
			}
			glBindVertexArray(primitiveVertexArrays_[meshIndex]);
			glDrawArrays(GL_TRIANGLES, 0, primitiveVertexCounts_[meshIndex]);
		}
		glBindVertexArray(0);
		glUseProgram(0);

		// Wireframe gizmos for every entity kind with no solid mesh. Cine
		// cameras and real cameras share the frustum icon; lights get a shape
		// per type; Empties get axis crosshairs; UI elements get a screen
		// marker. Grouped into one pass so the line program is bound once.
		if (showEditorGizmos_)
		{
			glUseProgram(lineShaderProgram_);
			for (const SceneEntity& entity : entities)
			{
				if (!entity.active || !isGizmoOnlyEntity(entity))
				{
					continue;
				}

				GLuint gizmoVertexArray = 0;
				GLsizei gizmoVertexCount = 0;
				// Gizmos are drawn at a size that means something (a point
				// light's ring IS its range, a spot's cone IS its cone), not
				// at the entity's authored scale - scaling a light has no
				// effect on the light itself, so honouring it here would draw
				// a shape that lies about what the light does.
				glm::mat4 gizmoModel = composeEntityPivotFrame(entity);
				gizmoModel = glm::scale(
					gizmoModel, 1.0F / glm::max(glm::abs(entity.scale), glm::vec3(0.0001F)));

				if (entity.isLight)
				{
					switch (entity.light.type)
					{
						case LightType::Directional:
							gizmoVertexArray = directionalLightGizmo_.vertexArray;
							gizmoVertexCount = directionalLightGizmo_.vertexCount;
							break;
						case LightType::Point:
							gizmoVertexArray = pointLightGizmo_.vertexArray;
							gizmoVertexCount = pointLightGizmo_.vertexCount;
							gizmoModel = glm::scale(gizmoModel, glm::vec3(entity.light.range));
							break;
						case LightType::Spot:
						{
							gizmoVertexArray = spotLightGizmo_.vertexArray;
							gizmoVertexCount = spotLightGizmo_.vertexCount;
							// The mesh is a unit cone one unit deep; stretch it
							// to the light's range and flare it to the real
							// outer half-angle.
							const float depth = entity.light.range;
							const float radius =
								depth * std::tan(glm::radians(
											std::clamp(entity.light.outerConeDegrees, 1.0F, 89.0F)));
							gizmoModel = glm::scale(gizmoModel, glm::vec3(radius, radius, depth));
							break;
						}
					}
				}
				else if (entity.isCineCamera || entity.isCamera)
				{
					gizmoVertexArray = cameraIconVertexArray_;
					gizmoVertexCount = cameraIconVertexCount_;
				}
				else if (entity.isUIElement)
				{
					gizmoVertexArray = uiElementGizmo_.vertexArray;
					gizmoVertexCount = uiElementGizmo_.vertexCount;
				}
				else
				{
					gizmoVertexArray = emptyGizmo_.vertexArray;
					gizmoVertexCount = emptyGizmo_.vertexCount;
				}

				if (gizmoVertexArray == 0 || gizmoVertexCount == 0)
				{
					continue;
				}
				const glm::mat4 iconMvp = viewProjection * gizmoModel;
				glUniformMatrix4fv(lineMvpLocation, 1, GL_FALSE, glm::value_ptr(iconMvp));
				glBindVertexArray(gizmoVertexArray);
				glDrawArrays(GL_LINES, 0, gizmoVertexCount);
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

		if (usePostProcess)
		{
			runPostProcess();
		}
		// HUD last, into the presented buffer - after the lens layers, so a
		// heavy grain or vignette does not chew up the crosshair and the
		// readouts the player needs to read.
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
		glViewport(0, 0, width_, height_);
		drawUIOverlay(entities);
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

	void ViewportRenderer::setLens(
		const float fieldOfViewDegrees, const float nearClip, const float farClip) noexcept
	{
		// Clamped rather than trusted: these come from authored CameraData,
		// and a zero/negative FOV or an inverted clip range produces a
		// degenerate projection matrix that renders nothing at all - a much
		// worse failure than being quietly held to a sane range.
		cameraFieldOfViewDegrees_ = glm::clamp(fieldOfViewDegrees, 1.0F, 179.0F);
		cameraNearClip_ = glm::max(nearClip, 0.001F);
		cameraFarClip_ = glm::max(farClip, cameraNearClip_ + 0.001F);
	}

	void ViewportRenderer::shutdown() noexcept
	{
		if (shadowAtlasFramebuffer_ != 0)
		{
			glDeleteFramebuffers(1, &shadowAtlasFramebuffer_);
			shadowAtlasFramebuffer_ = 0;
		}
		if (shadowAtlasTexture_ != 0)
		{
			glDeleteTextures(1, &shadowAtlasTexture_);
			shadowAtlasTexture_ = 0;
		}
		if (shadowDepthShaderProgram_ != 0)
		{
			glDeleteProgram(shadowDepthShaderProgram_);
			shadowDepthShaderProgram_ = 0;
		}
		if (shadowDepthSkinnedShaderProgram_ != 0)
		{
			glDeleteProgram(shadowDepthSkinnedShaderProgram_);
			shadowDepthSkinnedShaderProgram_ = 0;
		}
		frameLights_.clear();
		destroyGizmoMesh(emptyGizmo_);
		destroyGizmoMesh(directionalLightGizmo_);
		destroyGizmoMesh(pointLightGizmo_);
		destroyGizmoMesh(spotLightGizmo_);
		destroyGizmoMesh(uiElementGizmo_);
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
		// Every lit fragment stage is (version + shared lighting block + its
		// own body). The bodies above deliberately omit `#version` so they
		// can only be used through this helper - forgetting the lighting
		// block would otherwise compile fine right up until gfShade() is
		// called, in a shader that is only exercised by one entity type.
		const auto makeLitFragmentShader = [](const char* body)
		{
			return std::string("#version 460 core\n") + lightingCommonSource + body;
		};
		const std::string litMeshFragment = makeLitFragmentShader(meshFragmentShaderSource);
		const std::string litTerrainFragment = makeLitFragmentShader(terrainFragmentShaderSource);
		const std::string litTexturedFragment = makeLitFragmentShader(texturedMeshFragmentShaderSource);

		// Compile everything first, then commit. The previous version undid
		// each earlier program by hand in every later failure branch, which is
		// O(n^2) lines and grew a new branch per program; this is one cleanup
		// path regardless of which stage fails.
		struct ProgramSlot
		{
			GLuint* target;
			const char* vertexSource;
			const char* fragmentSource;
		};
		GLuint line = 0;
		GLuint mesh = 0;
		GLuint skinned = 0;
		GLuint terrain = 0;
		GLuint textured = 0;
		GLuint shadowDepth = 0;
		GLuint shadowDepthSkinned = 0;
		GLuint post = 0;
		const std::array<ProgramSlot, 8> slots{
			ProgramSlot{&line, lineVertexShaderSource, lineFragmentShaderSource},
			ProgramSlot{&mesh, meshVertexShaderSource, litMeshFragment.c_str()},
			ProgramSlot{&skinned, skinnedMeshVertexShaderSource, litMeshFragment.c_str()},
			ProgramSlot{&terrain, terrainVertexShaderSource, litTerrainFragment.c_str()},
			ProgramSlot{&textured, texturedMeshVertexShaderSource, litTexturedFragment.c_str()},
			ProgramSlot{&shadowDepth, shadowDepthVertexShaderSource, shadowDepthFragmentShaderSource},
			ProgramSlot{
				&shadowDepthSkinned, shadowDepthSkinnedVertexShaderSource, shadowDepthFragmentShaderSource},
			ProgramSlot{&post, postVertexShaderSource, postFragmentShaderSource}};

		for (const ProgramSlot& slot : slots)
		{
			*slot.target = compileProgram(slot.vertexSource, slot.fragmentSource);
			if (*slot.target == 0)
			{
				for (const ProgramSlot& cleanup : slots)
				{
					if (*cleanup.target != 0)
					{
						glDeleteProgram(*cleanup.target);
						*cleanup.target = 0;
					}
				}
				return false;
			}
		}

		lineShaderProgram_ = line;
		meshShaderProgram_ = mesh;
		skinnedMeshShaderProgram_ = skinned;
		terrainShaderProgram_ = terrain;
		texturedMeshShaderProgram_ = textured;
		shadowDepthShaderProgram_ = shadowDepth;
		shadowDepthSkinnedShaderProgram_ = shadowDepthSkinned;
		postShaderProgram_ = post;
		return true;
	}

	GLuint ViewportRenderer::ensureGradientTextureGpu(const std::string& relativePath)
	{
		if (relativePath.empty())
		{
			return 0;
		}
		const auto cached = gradientTextureCache_.find(relativePath);
		if (cached != gradientTextureCache_.end())
		{
			return cached->second;
		}
		// Cache the failure as 0 too, so a bad path is attempted once rather
		// than re-decoded every frame - the same trap the mesh caches hit.
		GLuint texture = 0;
		const std::optional<std::filesystem::path> resolved =
			core::resolveProjectFile(effectsProjectRoot_, relativePath, "Game", {});
		if (resolved.has_value())
		{
			const LoadedTexture image = loadTextureImage(*resolved);
			if (image.success)
			{
				glGenTextures(1, &texture);
				glBindTexture(GL_TEXTURE_2D, texture);
				glTexImage2D(
					GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
					image.rgba.data());
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				// Clamped: a gradient is a ramp, and wrapping it would make
				// the darkest pixels sample the brightest end of the strip.
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glBindTexture(GL_TEXTURE_2D, 0);
			}
		}
		gradientTextureCache_[relativePath] = texture;
		return texture;
	}

	void ViewportRenderer::setCameraEffects(
		const CameraEffects& effects, const std::filesystem::path& projectRoot)
	{
		cameraEffects_ = effects;
		effectsProjectRoot_ = projectRoot;
	}

	bool ViewportRenderer::createPostResources(const int width, const int height)
	{
		if (sceneFramebuffer_ != 0)
		{
			glDeleteRenderbuffers(1, &sceneDepthBuffer_);
			glDeleteTextures(1, &sceneTexture_);
			glDeleteFramebuffers(1, &sceneFramebuffer_);
			sceneDepthBuffer_ = 0;
			sceneTexture_ = 0;
			sceneFramebuffer_ = 0;
		}
		if (postVertexArray_ == 0)
		{
			// Core profile requires SOME VAO bound to draw, even when the
			// vertex shader reads nothing but gl_VertexID.
			glGenVertexArrays(1, &postVertexArray_);
		}

		glGenFramebuffers(1, &sceneFramebuffer_);
		glBindFramebuffer(GL_FRAMEBUFFER, sceneFramebuffer_);
		glGenTextures(1, &sceneTexture_);
		glBindTexture(GL_TEXTURE_2D, sceneTexture_);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneTexture_, 0);

		glGenRenderbuffers(1, &sceneDepthBuffer_);
		glBindRenderbuffer(GL_RENDERBUFFER, sceneDepthBuffer_);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
		glFramebufferRenderbuffer(
			GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, sceneDepthBuffer_);

		const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		return complete;
	}

	void ViewportRenderer::runPostProcess()
	{
		// Reads sceneTexture_, writes framebuffer_ (what the caller presents).
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
		glViewport(0, 0, width_, height_);
		glDisable(GL_DEPTH_TEST);
		glUseProgram(postShaderProgram_);

		const CameraEffects& fx = cameraEffects_;
		const auto setFloat = [this](const char* name, const float value)
		{ glUniform1f(glGetUniformLocation(postShaderProgram_, name), value); };
		const auto setInt = [this](const char* name, const int value)
		{ glUniform1i(glGetUniformLocation(postShaderProgram_, name), value); };

		setInt("colorFilter", static_cast<int>(fx.colorFilter));
		setFloat("filterStrength", fx.filterStrength);
		glUniform3fv(
			glGetUniformLocation(postShaderProgram_, "tintColor"), 1, glm::value_ptr(fx.tintColor));
		setFloat("tintStrength", fx.tintStrength);
		setFloat("gradientStrength", fx.gradientStrength);
		setFloat("brightness", fx.brightness);
		setFloat("contrast", fx.contrast);
		setFloat("saturation", fx.saturation);
		setFloat("grainAmount", fx.grainAmount);
		setFloat("grainSize", fx.grainSize);
		setFloat("flickerAmount", fx.flickerAmount);
		setFloat("flickerSpeed", fx.flickerSpeed);
		setFloat("scanlineAmount", fx.scanlineAmount);
		setFloat("scanlineCount", fx.scanlineCount);
		setFloat("vignetteAmount", fx.vignetteAmount);
		setFloat("vignetteSoftness", fx.vignetteSoftness);
		setFloat("chromaticAberration", fx.chromaticAberration);
		setFloat("time", static_cast<float>(glfwGetTime()));
		glUniform2f(
			glGetUniformLocation(postShaderProgram_, "viewportSize"), static_cast<float>(width_),
			static_cast<float>(height_));

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, sceneTexture_);
		setInt("sceneTexture", 0);

		const GLuint gradient = ensureGradientTextureGpu(fx.gradientTexturePath);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, gradient != 0 ? gradient : fallbackWhiteTexture_);
		setInt("gradientTexture", 1);
		setFloat("hasGradient", gradient != 0 ? 1.0F : 0.0F);
		glActiveTexture(GL_TEXTURE0);

		glBindVertexArray(postVertexArray_);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
		glUseProgram(0);
		glEnable(GL_DEPTH_TEST);
	}

	bool ViewportRenderer::createShadowResources()
	{
		glGenTextures(1, &shadowAtlasTexture_);
		glBindTexture(GL_TEXTURE_2D, shadowAtlasTexture_);
		glTexImage2D(
			GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kShadowAtlasSize, kShadowAtlasSize, 0,
			GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		// Clamp to a border of depth 1.0 (= "nothing ever occluded this") so a
		// sample that lands outside the atlas reads as lit rather than
		// wrapping into a neighbouring tile.
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		const std::array<float, 4> border{1.0F, 1.0F, 1.0F, 1.0F};
		glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border.data());
		glBindTexture(GL_TEXTURE_2D, 0);

		glGenFramebuffers(1, &shadowAtlasFramebuffer_);
		glBindFramebuffer(GL_FRAMEBUFFER, shadowAtlasFramebuffer_);
		glFramebufferTexture2D(
			GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowAtlasTexture_, 0);
		// Depth-only: with no colour attachment both buffers must be NONE, or
		// the FBO is incomplete.
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
		const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		if (!complete)
		{
			glDeleteFramebuffers(1, &shadowAtlasFramebuffer_);
			glDeleteTextures(1, &shadowAtlasTexture_);
			shadowAtlasFramebuffer_ = 0;
			shadowAtlasTexture_ = 0;
			return false;
		}
		return true;
	}

	void ViewportRenderer::collectLights(const std::vector<SceneEntity>& entities)
	{
		frameLights_.clear();

		// Scene bounds, used to fit the directional shadow frustum. Only
		// entities that actually receive/cast shadows count - a light's own
		// gizmo sitting 200 units away must not stretch the sun's frustum
		// across the whole world and shred its effective resolution.
		glm::vec3 boundsMin(std::numeric_limits<float>::max());
		glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
		bool haveBounds = false;
		for (const SceneEntity& entity : entities)
		{
			if (!entity.active || isGizmoOnlyEntity(entity))
			{
				continue;
			}
			// A generous per-entity extent rather than real mesh bounds:
			// this only needs to be conservative, and pulling exact bounds
			// would mean loading every imported mesh here every frame.
			const float extent =
				entity.isTerrain
					? std::max(entity.terrain.worldSize, entity.terrain.heightScale)
					: (std::max({std::abs(entity.scale.x), std::abs(entity.scale.y), std::abs(entity.scale.z)})
						* 2.0F);
			boundsMin = glm::min(boundsMin, entity.position - glm::vec3(extent));
			boundsMax = glm::max(boundsMax, entity.position + glm::vec3(extent));
			haveBounds = true;
		}
		if (!haveBounds)
		{
			boundsMin = glm::vec3(-20.0F);
			boundsMax = glm::vec3(20.0F);
		}
		const glm::vec3 boundsCenter = (boundsMin + boundsMax) * 0.5F;
		const float boundsRadius = std::max(glm::length(boundsMax - boundsCenter), 1.0F);

		int nextShadowSlot = 0;
		for (const SceneEntity& entity : entities)
		{
			if (!entity.isLight || !entity.active)
			{
				continue;
			}
			if (static_cast<int>(frameLights_.size()) >= kMaxLights)
			{
				// Over the cap the extra lights are simply ignored. Silently,
				// deliberately: this runs every frame, so logging here would
				// spam the console 60 times a second for one authoring
				// mistake. The Inspector shows the cap instead.
				break;
			}

			FrameLight light;
			light.type = static_cast<int>(entity.light.type);
			light.direction = entityForward(entity);
			light.position = entity.position;
			light.color = entity.light.color * entity.light.intensity;
			light.range = std::max(entity.light.range, 0.0001F);
			light.cosInner = std::cos(glm::radians(entity.light.innerConeDegrees));
			light.cosOuter = std::cos(glm::radians(entity.light.outerConeDegrees));
			light.shadowBias = entity.light.shadowBias;

			// Point lights need a cube map to shadow correctly (6 faces); this
			// pass ships directional + spot shadows only, so a point light
			// lights the scene but never occludes. Stated in the Inspector
			// rather than left as a mystery.
			const bool shadowCapable =
				entity.light.castShadows && entity.light.type != LightType::Point;
			if (shadowCapable && nextShadowSlot < kMaxShadowLights && shadowAtlasTexture_ != 0)
			{
				light.shadowSlot = nextShadowSlot++;
				if (entity.light.type == LightType::Directional)
				{
					// A sun has no position, so place a virtual eye outside
					// the scene along -direction and fit an ortho box to the
					// bounding sphere. Using the sphere (not the box) keeps
					// the frustum stable as the light rotates, which is what
					// stops shadow edges from swimming.
					const glm::vec3 eye = boundsCenter - light.direction * (boundsRadius * 2.0F);
					// lookAt degenerates when the view direction is parallel
					// to the up vector - a sun pointing straight down is the
					// single most common case there is.
					const glm::vec3 up =
						std::abs(light.direction.y) > 0.99F ? glm::vec3(0.0F, 0.0F, 1.0F)
														   : glm::vec3(0.0F, 1.0F, 0.0F);
					const glm::mat4 lightView = glm::lookAt(eye, boundsCenter, up);
					const glm::mat4 lightProjection = glm::ortho(
						-boundsRadius, boundsRadius, -boundsRadius, boundsRadius,
						0.05F, boundsRadius * 4.0F);
					light.lightViewProjection = lightProjection * lightView;
				}
				else
				{
					const glm::vec3 up =
						std::abs(light.direction.y) > 0.99F ? glm::vec3(0.0F, 0.0F, 1.0F)
														   : glm::vec3(0.0F, 1.0F, 0.0F);
					const glm::mat4 lightView =
						glm::lookAt(light.position, light.position + light.direction, up);
					// Widen slightly past the outer cone so the cone's own
					// edge isn't sitting exactly on the frustum boundary,
					// where PCF taps would fall outside and read as lit.
					const float fov = glm::radians(
						std::clamp(entity.light.outerConeDegrees * 2.0F + 8.0F, 5.0F, 175.0F));
					const glm::mat4 lightProjection =
						glm::perspective(fov, 1.0F, 0.05F, light.range);
					light.lightViewProjection = lightProjection * lightView;
				}
			}
			frameLights_.push_back(light);
		}

		if (frameLights_.empty())
		{
			// No authored lights: reproduce EXACTLY the single hardcoded light
			// every shader used to inline (direction normalize(0.4,0.85,0.35),
			// 0.65 diffuse over 0.35 ambient). Without this, every scene made
			// before lights existed would open flat-shaded - a silent, total
			// visual regression on content that never opted in.
			FrameLight legacy;
			legacy.type = static_cast<int>(LightType::Directional);
			legacy.direction = -glm::normalize(glm::vec3(0.4F, 0.85F, 0.35F));
			legacy.color = glm::vec3(0.65F);
			legacy.shadowSlot = -1;
			frameLights_.push_back(legacy);
		}
	}

	void ViewportRenderer::drawEntityGeometryForShadow(
		const SceneEntity& entity,
		const glm::mat4& model,
		const GLuint staticProgram,
		const GLuint skinnedProgram,
		const std::filesystem::path& projectRoot)
	{
		if (entity.isTerrain)
		{
			ensureTerrainGpu(entity, projectRoot);
			const auto cacheEntry = terrainCache_.find(entity.id);
			if (cacheEntry == terrainCache_.end() || cacheEntry->second.vertexCount <= 0)
			{
				return;
			}
			glUseProgram(staticProgram);
			glUniformMatrix4fv(
				glGetUniformLocation(staticProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
			glBindVertexArray(cacheEntry->second.vertexArray);
			glDrawArrays(GL_TRIANGLES, 0, cacheEntry->second.vertexCount);
			return;
		}

		if (entity.isImportedMesh)
		{
			ensureImportedMeshGpu(entity, projectRoot);
			const auto cacheEntry = importedMeshCache_.find(entity.id);
			if (cacheEntry == importedMeshCache_.end() || cacheEntry->second.vertexCount <= 0)
			{
				return;
			}
			if (cacheEntry->second.hasSkeleton)
			{
				const std::vector<glm::mat4> boneMatrices = computeSkinningMatrices(
					cacheEntry->second.bones, cacheEntry->second.animations, glfwGetTime());
				glUseProgram(skinnedProgram);
				glUniformMatrix4fv(
					glGetUniformLocation(skinnedProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
				if (!boneMatrices.empty())
				{
					glUniformMatrix4fv(
						glGetUniformLocation(skinnedProgram, "boneMatrices"),
						static_cast<GLsizei>(boneMatrices.size()), GL_FALSE,
						glm::value_ptr(boneMatrices.front()));
				}
			}
			else
			{
				glUseProgram(staticProgram);
				glUniformMatrix4fv(
					glGetUniformLocation(staticProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
			}
			glBindVertexArray(cacheEntry->second.vertexArray);
			glDrawArrays(GL_TRIANGLES, 0, cacheEntry->second.vertexCount);
			return;
		}

		if (entity.isTextMesh)
		{
			ensureTextMeshGpu(entity, projectRoot);
			const auto cacheEntry = textMeshCache_.find(entity.id);
			if (cacheEntry == textMeshCache_.end() || cacheEntry->second.vertexCount <= 0)
			{
				return;
			}
			glUseProgram(staticProgram);
			glUniformMatrix4fv(
				glGetUniformLocation(staticProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
			glBindVertexArray(cacheEntry->second.vertexArray);
			glDrawArrays(GL_TRIANGLES, 0, cacheEntry->second.vertexCount);
			return;
		}

		const std::size_t meshIndex = static_cast<std::size_t>(entity.primitive);
		if (meshIndex >= primitiveVertexArrays_.size() || primitiveVertexCounts_[meshIndex] <= 0)
		{
			return;
		}
		glUseProgram(staticProgram);
		glUniformMatrix4fv(
			glGetUniformLocation(staticProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
		glBindVertexArray(primitiveVertexArrays_[meshIndex]);
		glDrawArrays(GL_TRIANGLES, 0, primitiveVertexCounts_[meshIndex]);
	}

	void ViewportRenderer::renderShadowMaps(
		const std::vector<SceneEntity>& entities,
		const std::filesystem::path& projectRoot,
		const int excludeEntityId)
	{
		if (shadowAtlasFramebuffer_ == 0 || shadowDepthShaderProgram_ == 0)
		{
			return;
		}
		const bool anyShadowCaster = std::any_of(
			frameLights_.begin(), frameLights_.end(),
			[](const FrameLight& light) { return light.shadowSlot >= 0; });
		if (!anyShadowCaster)
		{
			return;
		}

		glBindFramebuffer(GL_FRAMEBUFFER, shadowAtlasFramebuffer_);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glDepthMask(GL_TRUE);
		// Scissor so clearing one tile cannot wipe the others - the atlas is
		// one texture shared by every shadow-casting light this frame.
		glEnable(GL_SCISSOR_TEST);
		// Front-face culling during the depth pass pushes acne to surfaces
		// the camera cannot see, which is what lets the bias stay small
		// enough to keep contact shadows attached.
		glEnable(GL_CULL_FACE);
		glCullFace(GL_FRONT);

		for (const FrameLight& light : frameLights_)
		{
			if (light.shadowSlot < 0)
			{
				continue;
			}
			const int tileRow = light.shadowSlot / kShadowTilesPerRow;
			const int tileColumn = light.shadowSlot % kShadowTilesPerRow;
			const GLint tileX = tileColumn * kShadowTileSize;
			const GLint tileY = tileRow * kShadowTileSize;
			glViewport(tileX, tileY, kShadowTileSize, kShadowTileSize);
			glScissor(tileX, tileY, kShadowTileSize, kShadowTileSize);
			glClear(GL_DEPTH_BUFFER_BIT);

			for (const GLuint program : {shadowDepthShaderProgram_, shadowDepthSkinnedShaderProgram_})
			{
				glUseProgram(program);
				glUniformMatrix4fv(
					glGetUniformLocation(program, "lightViewProjection"), 1, GL_FALSE,
					glm::value_ptr(light.lightViewProjection));
			}

			for (const SceneEntity& entity : entities)
			{
				// Same exclusions as the visible pass, plus gizmo-only kinds:
				// a light's own wireframe icon must not cast a shadow, and the
				// player's hidden first-person body must not either - its
				// shadow would give away a mesh the camera is inside.
				if (!entity.active || isGizmoOnlyEntity(entity) || entity.id == excludeEntityId)
				{
					continue;
				}
				drawEntityGeometryForShadow(
					entity, composeEntityTransform(entity), shadowDepthShaderProgram_,
					shadowDepthSkinnedShaderProgram_, projectRoot);
			}
		}

		glBindVertexArray(0);
		glUseProgram(0);
		glCullFace(GL_BACK);
		glDisable(GL_CULL_FACE);
		glDisable(GL_SCISSOR_TEST);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void ViewportRenderer::uploadLightUniforms(const GLuint program) const
	{
		if (program == 0)
		{
			return;
		}
		const int count = std::min(static_cast<int>(frameLights_.size()), kMaxLights);
		glUniform1i(glGetUniformLocation(program, "lightCount"), count);
		glUniform3fv(glGetUniformLocation(program, "ambientColor"), 1, glm::value_ptr(ambientColor_));

		// Uniform arrays are set element by element via "name[i]". Uploading
		// the whole array in one call would need the elements contiguous in
		// memory, which they are not - FrameLight is struct-of-arrays on the
		// GPU side and array-of-structs here. At kMaxLights = 8 the call count
		// is small and this stays readable.
		for (int index = 0; index < count; ++index)
		{
			const FrameLight& light = frameLights_[static_cast<std::size_t>(index)];
			const auto uniformName = [index](const char* base)
			{
				std::array<char, 64> buffer{};
				std::snprintf(buffer.data(), buffer.size(), "%s[%d]", base, index);
				return std::string(buffer.data());
			};
			glUniform1i(glGetUniformLocation(program, uniformName("lightType").c_str()), light.type);
			glUniform3fv(
				glGetUniformLocation(program, uniformName("lightDirection").c_str()), 1,
				glm::value_ptr(light.direction));
			glUniform3fv(
				glGetUniformLocation(program, uniformName("lightPosition").c_str()), 1,
				glm::value_ptr(light.position));
			glUniform3fv(
				glGetUniformLocation(program, uniformName("lightColor").c_str()), 1,
				glm::value_ptr(light.color));
			glUniform1f(glGetUniformLocation(program, uniformName("lightRange").c_str()), light.range);
			glUniform1f(glGetUniformLocation(program, uniformName("lightCosInner").c_str()), light.cosInner);
			glUniform1f(glGetUniformLocation(program, uniformName("lightCosOuter").c_str()), light.cosOuter);
			glUniform1i(
				glGetUniformLocation(program, uniformName("lightShadowSlot").c_str()), light.shadowSlot);
			glUniform1f(
				glGetUniformLocation(program, uniformName("lightShadowBias").c_str()), light.shadowBias);
			glUniformMatrix4fv(
				glGetUniformLocation(program, uniformName("lightViewProjection").c_str()), 1, GL_FALSE,
				glm::value_ptr(light.lightViewProjection));
		}

		// The shadow atlas lives on a texture unit well past the 9 the terrain
		// shader claims for its splat layers, so binding it here can never
		// stomp one of those.
		constexpr GLint kShadowAtlasTextureUnit = 12;
		glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + kShadowAtlasTextureUnit));
		glBindTexture(GL_TEXTURE_2D, shadowAtlasTexture_);
		glUniform1i(glGetUniformLocation(program, "shadowAtlas"), kShadowAtlasTextureUnit);
		glActiveTexture(GL_TEXTURE0);

		const float tileScale = 1.0F / static_cast<float>(kShadowTilesPerRow);
		glUniform2f(glGetUniformLocation(program, "shadowTileScale"), tileScale, tileScale);
		glUniform1i(glGetUniformLocation(program, "shadowTilesPerRow"), kShadowTilesPerRow);
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

namespace gameforger::editor
{
	namespace
	{
		// One shader for both jobs: a flat tinted rect (useTexture=0) or a
		// glyph/image modulated by the tint (useTexture=1). Same single-program
		// trick GameMenu uses - a HUD draws far more solid rects than glyphs,
		// so a second program would earn nothing.
		constexpr const char* overlayVertexShaderSource = R"glsl(
#version 460 core
layout (location = 0) in vec2 position;
layout (location = 1) in vec2 uv;
out vec2 fragUv;
uniform mat4 projection;

void main()
{
	fragUv = uv;
	gl_Position = projection * vec4(position, 0.0, 1.0);
}
)glsl";

		constexpr const char* overlayFragmentShaderSource = R"glsl(
#version 460 core
in vec2 fragUv;
out vec4 fragColor;
uniform sampler2D atlas;
uniform vec4 tintColor;
uniform int useTexture;
// Glyphs come from a single-channel atlas (coverage in .r); an imported image
// is full RGBA. One flag rather than two shaders.
uniform int textureIsAlphaOnly;

void main()
{
	if (useTexture == 0)
	{
		fragColor = tintColor;
		return;
	}
	vec4 sampled = texture(atlas, fragUv);
	if (textureIsAlphaOnly != 0)
	{
		fragColor = vec4(tintColor.rgb, tintColor.a * sampled.r);
	}
	else
	{
		fragColor = sampled * tintColor;
	}
}
)glsl";
	}

	const ViewportRenderer::OverlayFontEntry* ViewportRenderer::ensureOverlayFont(
		const std::string& relativeFontPath)
	{
		const auto existing = overlayFonts_.find(relativeFontPath);
		if (existing != overlayFonts_.end())
		{
			return existing->second.font.success ? &existing->second : nullptr;
		}

		OverlayFontEntry entry;
		entry.attempted = true;

		std::filesystem::path fontFile;
		if (!relativeFontPath.empty())
		{
			fontFile = uiProjectRoot_ / relativeFontPath;
		}
		else
		{
			// No authored font: take the first usable one in Game/Fonts.
			// TrueType is preferred because stb_truetype cannot rasterise
			// CFF/PostScript-flavoured OTF, so a .ttf is far more likely to
			// bake than an .otf sitting beside it.
			const std::filesystem::path fontsDirectory = uiProjectRoot_ / "Game" / "Fonts";
			std::error_code error;
			std::filesystem::path firstAnyFont;
			for (const auto& item : std::filesystem::directory_iterator(fontsDirectory, error))
			{
				if (!item.is_regular_file())
				{
					continue;
				}
				std::string extension = item.path().extension().string();
				std::transform(
					extension.begin(), extension.end(), extension.begin(),
					[](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (extension == ".ttf")
				{
					fontFile = item.path();
					break;
				}
				if ((extension == ".otf" || extension == ".ttc") && firstAnyFont.empty())
				{
					firstAnyFont = item.path();
				}
			}
			if (fontFile.empty())
			{
				fontFile = firstAnyFont;
			}
		}

		if (!fontFile.empty())
		{
			entry.font = bakeOverlayFont(fontFile);
		}
		if (entry.font.success)
		{
			glGenTextures(1, &entry.texture);
			glBindTexture(GL_TEXTURE_2D, entry.texture);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage2D(
				GL_TEXTURE_2D, 0, GL_R8, OverlayFont::kAtlasWidth, OverlayFont::kAtlasHeight, 0, GL_RED,
				GL_UNSIGNED_BYTE, entry.font.alphaPixels.data());
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
			glBindTexture(GL_TEXTURE_2D, 0);
			// The pixels are on the GPU now; keep the glyph metrics, drop the
			// atlas bitmap. At 512x512 per font that is a quarter megabyte
			// each, held for nothing.
			entry.font.alphaPixels.clear();
			entry.font.alphaPixels.shrink_to_fit();
		}
		else
		{
			std::fprintf(
				stderr, "HUD text: no usable font%s - add a .ttf under Game/Fonts.\n",
				relativeFontPath.empty() ? "" : (" at " + relativeFontPath).c_str());
		}

		const auto inserted = overlayFonts_.emplace(relativeFontPath, std::move(entry)).first;
		return inserted->second.font.success ? &inserted->second : nullptr;
	}

	bool ViewportRenderer::createOverlayResources()
	{
		overlayShaderProgram_ = compileProgram(overlayVertexShaderSource, overlayFragmentShaderSource);
		if (overlayShaderProgram_ == 0)
		{
			return false;
		}

		glGenVertexArrays(1, &overlayVertexArray_);
		glGenBuffers(1, &overlayVertexBuffer_);
		glBindVertexArray(overlayVertexArray_);
		glBindBuffer(GL_ARRAY_BUFFER, overlayVertexBuffer_);
		// 6 vertices per quad, position(2) + uv(2). Re-uploaded per quad: a HUD
		// is a handful of quads, not a performance-sensitive path.
		glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(
			1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<const void*>(2 * sizeof(float)));
		glEnableVertexAttribArray(1);
		glBindVertexArray(0);
		return true;
	}

	void ViewportRenderer::setUIOverlay(
		const int hostCameraId, const std::filesystem::path& projectRoot) noexcept
	{
		uiHostCameraId_ = hostCameraId;
		uiProjectRoot_ = projectRoot;
	}

	void ViewportRenderer::drawUIOverlay(const std::vector<SceneEntity>& entities)
	{
		if (overlayShaderProgram_ == 0 || uiHostCameraId_ < 0 || width_ <= 0 || height_ <= 0)
		{
			return;
		}

		// An element draws only if the host camera is anywhere up its parent
		// chain - not just its immediate parent - so a HUD can be organised
		// under an Empty ("Camera > HUD > HealthText") the way anyone would lay
		// one out. Depth-capped rather than cycle-tracked: applyParentConstraints
		// already tolerates cycles, and a fixed cap keeps this allocation-free
		// on a path that runs every frame.
		const auto findEntityByName = [&entities](const std::string& name) -> const SceneEntity*
		{
			for (const SceneEntity& candidate : entities)
			{
				if (candidate.name == name)
				{
					return &candidate;
				}
			}
			return nullptr;
		};
		const auto descendsFromHost = [&](const SceneEntity& element)
		{
			constexpr int kMaxParentDepth = 32;
			const SceneEntity* walk = &element;
			for (int depth = 0; depth < kMaxParentDepth; ++depth)
			{
				if (walk->parentName.empty())
				{
					return false;
				}
				const SceneEntity* parent = findEntityByName(walk->parentName);
				if (parent == nullptr)
				{
					return false;
				}
				if (parent->id == uiHostCameraId_)
				{
					return true;
				}
				walk = parent;
			}
			return false;
		};

		bool anyToDraw = false;
		for (const SceneEntity& element : entities)
		{
			if (element.isUIElement && element.active && descendsFromHost(element))
			{
				anyToDraw = true;
				break;
			}
		}
		if (!anyToDraw)
		{
			return;
		}

		const float viewWidth = static_cast<float>(width_);
		const float viewHeight = static_cast<float>(height_);
		// Y-down screen space, matching how anchors and pixel offsets are
		// authored and how every 2D UI system expresses them.
		const glm::mat4 projection = glm::ortho(0.0F, viewWidth, viewHeight, 0.0F, -1.0F, 1.0F);

		glDisable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glUseProgram(overlayShaderProgram_);
		glUniformMatrix4fv(
			glGetUniformLocation(overlayShaderProgram_, "projection"), 1, GL_FALSE,
			glm::value_ptr(projection));
		glBindVertexArray(overlayVertexArray_);
		glBindBuffer(GL_ARRAY_BUFFER, overlayVertexBuffer_);

		const GLint tintLocation = glGetUniformLocation(overlayShaderProgram_, "tintColor");
		const GLint useTextureLocation = glGetUniformLocation(overlayShaderProgram_, "useTexture");
		const GLint alphaOnlyLocation = glGetUniformLocation(overlayShaderProgram_, "textureIsAlphaOnly");
		glUniform1i(glGetUniformLocation(overlayShaderProgram_, "atlas"), 0);

		const auto drawQuad = [&](const float x, const float y, const float w, const float h,
								  const float u0, const float v0, const float u1, const float v1)
		{
			const std::array<float, 24> vertices{
				x,     y,     u0, v0,
				x + w, y,     u1, v0,
				x + w, y + h, u1, v1,
				x,     y,     u0, v0,
				x + w, y + h, u1, v1,
				x,     y + h, u0, v1};
			glBufferSubData(
				GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data());
			glDrawArrays(GL_TRIANGLES, 0, 6);
		};
		const auto setTint = [&](const glm::vec3& color, const float alpha)
		{ glUniform4f(tintLocation, color.r, color.g, color.b, alpha); };
		const auto drawRect =
			[&](const float x, const float y, const float w, const float h, const glm::vec3& color,
				const float alpha)
		{
			setTint(color, alpha);
			glUniform1i(useTextureLocation, 0);
			drawQuad(x, y, w, h, 0.0F, 0.0F, 1.0F, 1.0F);
		};

		for (const SceneEntity& element : entities)
		{
			if (!element.isUIElement || !element.active || !descendsFromHost(element))
			{
				continue;
			}

			float anchorX = viewWidth * 0.5F;
			float anchorY = viewHeight * 0.5F;
			switch (element.ui.anchor)
			{
				case UIAnchor::TopLeft:      anchorX = 0.0F;             anchorY = 0.0F;             break;
				case UIAnchor::TopCenter:    anchorX = viewWidth * 0.5F; anchorY = 0.0F;             break;
				case UIAnchor::TopRight:     anchorX = viewWidth;        anchorY = 0.0F;             break;
				case UIAnchor::MiddleLeft:   anchorX = 0.0F;             anchorY = viewHeight * 0.5F; break;
				case UIAnchor::MiddleRight:  anchorX = viewWidth;        anchorY = viewHeight * 0.5F; break;
				case UIAnchor::BottomLeft:   anchorX = 0.0F;             anchorY = viewHeight;       break;
				case UIAnchor::BottomCenter: anchorX = viewWidth * 0.5F; anchorY = viewHeight;       break;
				case UIAnchor::BottomRight:  anchorX = viewWidth;        anchorY = viewHeight;       break;
				case UIAnchor::Center: break;
			}
			const float centerX = anchorX + element.ui.offsetPixels.x;
			const float centerY = anchorY + element.ui.offsetPixels.y;
			const float alpha = std::clamp(element.ui.opacity, 0.0F, 1.0F);

			switch (element.ui.kind)
			{
				case UIElementKind::Crosshair:
				{
					const float arm = std::max(element.ui.sizePixels.x, 1.0F);
					const float gap = std::max(element.ui.gapPixels, 0.0F);
					const float thickness = std::max(element.ui.thicknessPixels, 1.0F);
					const float half = thickness * 0.5F;
					drawRect(centerX - gap - arm, centerY - half, arm, thickness, element.ui.color, alpha);
					drawRect(centerX + gap, centerY - half, arm, thickness, element.ui.color, alpha);
					drawRect(centerX - half, centerY - gap - arm, thickness, arm, element.ui.color, alpha);
					drawRect(centerX - half, centerY + gap, thickness, arm, element.ui.color, alpha);
					break;
				}
				case UIElementKind::Panel:
				{
					drawRect(
						centerX - element.ui.sizePixels.x * 0.5F, centerY - element.ui.sizePixels.y * 0.5F,
						element.ui.sizePixels.x, element.ui.sizePixels.y, element.ui.color, alpha);
					break;
				}
				case UIElementKind::Image:
				{
					const GLuint texture =
						element.ui.imagePath.empty() ? 0U : ensureGradientTextureGpu(element.ui.imagePath);
					const float w = element.ui.sizePixels.x;
					const float h = element.ui.sizePixels.y;
					const float x = centerX - w * 0.5F;
					const float y = centerY - h * 0.5F;
					if (texture != 0)
					{
						glActiveTexture(GL_TEXTURE0);
						glBindTexture(GL_TEXTURE_2D, texture);
						setTint(element.ui.color, alpha);
						glUniform1i(useTextureLocation, 1);
						glUniform1i(alphaOnlyLocation, 0);
						drawQuad(x, y, w, h, 0.0F, 0.0F, 1.0F, 1.0F);
					}
					else
					{
						// No image, or it failed to load. An outline beats
						// drawing nothing: a missing path then reads as a
						// missing path rather than as a broken HUD.
						constexpr float kEdge = 1.0F;
						drawRect(x, y, w, kEdge, element.ui.color, alpha);
						drawRect(x, y + h - kEdge, w, kEdge, element.ui.color, alpha);
						drawRect(x, y, kEdge, h, element.ui.color, alpha);
						drawRect(x + w - kEdge, y, kEdge, h, element.ui.color, alpha);
					}
					break;
				}
				case UIElementKind::Text:
				{
					// The shared baked atlas, scaled. A UI element's own
					// authored fontPath still round-trips through the scene
					// file but does not change rendering yet - honouring it
					// needs one atlas per font, a cache with its own lifetime
					// rules rather than a one-line change.
					const OverlayFontEntry* fontEntry = ensureOverlayFont(element.ui.fontPath);
					if (fontEntry == nullptr)
					{
						break;
					}
					const float pixelHeight = std::max(element.ui.fontSizePixels, 1.0F);
					const float scale = pixelHeight / OverlayFont::kBakedPixelHeight;
					const float textWidth = fontEntry->font.measure(element.ui.text, pixelHeight);
					// Alignment follows the anchor rather than always centring.
					// A top-LEFT label centred on its anchor point spills half
					// its width off the left edge of the screen, which is
					// exactly what the demo's instruction line did.
					float penX = centerX - textWidth * 0.5F;
					switch (element.ui.anchor)
					{
						case UIAnchor::TopLeft:
						case UIAnchor::MiddleLeft:
						case UIAnchor::BottomLeft:
							penX = centerX;
							break;
						case UIAnchor::TopRight:
						case UIAnchor::MiddleRight:
						case UIAnchor::BottomRight:
							penX = centerX - textWidth;
							break;
						default:
							break;
					}
					const float baselineY = centerY + pixelHeight * 0.35F;

					glActiveTexture(GL_TEXTURE0);
					glBindTexture(GL_TEXTURE_2D, fontEntry->texture);
					setTint(element.ui.color, alpha);
					glUniform1i(useTextureLocation, 1);
					glUniform1i(alphaOnlyLocation, 1);
					for (const char character : element.ui.text)
					{
						const int index =
							static_cast<int>(static_cast<unsigned char>(character)) - OverlayFont::kFirstChar;
						if (index < 0 || index >= OverlayFont::kCharCount)
						{
							continue;
						}
						const OverlayGlyph& glyph = fontEntry->font.glyphs[static_cast<std::size_t>(index)];
						drawQuad(
							penX + glyph.xOffset * scale, baselineY + glyph.yOffset * scale,
							glyph.width * scale, glyph.height * scale, glyph.u0, glyph.v0, glyph.u1, glyph.v1);
						penX += glyph.xAdvance * scale;
					}
					break;
				}
			}
		}

		glBindVertexArray(0);
		glUseProgram(0);
		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);
		glActiveTexture(GL_TEXTURE0);
	}
}
