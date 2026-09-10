#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <glad/gl.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "GameForger/Editor/EditorScene.hpp"
#include "GameForger/Editor/ModelImport.hpp"

namespace gameforger::editor
{
	class ViewportRenderer final
	{
	public:
		ViewportRenderer() = default;
		~ViewportRenderer();

		ViewportRenderer(const ViewportRenderer&) = delete;
		ViewportRenderer& operator=(const ViewportRenderer&) = delete;

		[[nodiscard]] bool initialize();
		[[nodiscard]] bool resize(int width, int height);
		// `projectRoot` resolves each isTextMesh entity's relative fontPath -
		// their GPU mesh is unique per entity (unlike the 6 shared primitive
		// meshes) and is lazily built/rebuilt here whenever an entity's text,
		// font, size, or depth changes since the last render() call.
		// `excludeEntityId` skips drawing one entity entirely (defaults to
		// -1, draw everything) - used by the Game view to hide a player's
		// own body mesh while its script is in first-person camera mode
		// (self.camera:setMode("fps")), the standard "don't render your own
		// head/torso from inside it" FPS convention. The editable Viewport
		// never passes this - it always shows every entity for editing.
		void render(
			const std::vector<SceneEntity>& entities,
			const std::vector<int>& selectedEntityIds,
			const std::filesystem::path& projectRoot,
			int excludeEntityId = -1);
		void setCamera(float yaw, float pitch, float distance, const glm::vec3& target) noexcept;
		// Vertical FOV (degrees) plus the near/far planes used by the next
		// render(). Defaults reproduce the values that used to be hardcoded
		// in render(), so a caller that never touches this is unaffected.
		// The Game view sets these from the active Camera entity's CameraData;
		// without it, that entity's FOV / clip settings would be authored,
		// serialized and displayed but never actually applied.
		void setLens(float fieldOfViewDegrees, float nearClip, float farClip) noexcept;
		// Whether to draw editor-only decoration: the ground grid and the
		// wireframe gizmos for lights, cameras, Empties and UI markers.
		// Defaults true so the Editor is unaffected; GameForgerRuntime turns
		// it off, because a shipped game must not show authoring furniture.
		void setShowEditorGizmos(bool show) noexcept { showEditorGizmos_ = show; }
		// The "lens layers" - grade, named filter, gradient map, grain,
		// flicker, scanlines, vignette - applied as one full-screen pass over
		// the rendered image. Set from the active camera's CameraEffects each
		// frame; pass a default-constructed one (or leave it) for no post at
		// all, in which case the pass is skipped entirely rather than run as
		// an identity transform.
		//
		// `projectRoot` resolves the gradient texture, cached by path like
		// every other texture here.
		void setCameraEffects(const CameraEffects& effects, const std::filesystem::path& projectRoot);
		void shutdown() noexcept;

		// Blits this renderer's own offscreen color buffer (as of the most
		// recent render() call) into whichever framebuffer is currently
		// bound for drawing - framebuffer 0 (the real window) for
		// GameForgerRuntime's standalone game loop, which has no ImGui to
		// present through. The Editor never calls this; it always presents
		// via ImGui::Image(texture()) instead. `width`/`height` are the
		// destination's size (normally the window's own framebuffer size,
		// which may differ from this renderer's own width()/height() if a
		// resize() hasn't happened yet this frame).
		void blitToCurrentFramebuffer(int width, int height) const;

		[[nodiscard]] GLuint texture() const noexcept;
		[[nodiscard]] int width() const noexcept;
		[[nodiscard]] int height() const noexcept;
		[[nodiscard]] const glm::mat4& view() const noexcept;
		[[nodiscard]] const glm::mat4& projection() const noexcept;
		[[nodiscard]] glm::vec3 cameraPosition() const noexcept;

		// Shading limits. kMaxLights must match GF_MAX_LIGHTS in the shared
		// lighting GLSL (ViewportRenderer.cpp) - the uniform arrays are sized
		// by it on both sides.
		static constexpr int kMaxLights = 8;
		// Shadow-casting lights get one tile each in a single atlas texture,
		// laid out kShadowTilesPerRow x kShadowTilesPerRow. One texture (not a
		// sampler array) because GLSL cannot index sampler arrays with a
		// non-constant expression without extensions.
		static constexpr int kShadowTilesPerRow = 2;
		static constexpr int kMaxShadowLights = kShadowTilesPerRow * kShadowTilesPerRow;
		static constexpr int kShadowTileSize = 1024;
		static constexpr int kShadowAtlasSize = kShadowTileSize * kShadowTilesPerRow;

	private:
		// One light resolved for the current frame: world-space direction and
		// position pulled off the entity's transform, colour premultiplied by
		// intensity, and - if it got an atlas tile - the matrix that maps
		// world space into that tile's depth space.
		struct FrameLight
		{
			int type = 0; // matches the shader's 0=directional, 1=point, 2=spot
			glm::vec3 direction{0.0F, -1.0F, 0.0F};
			glm::vec3 position{0.0F};
			glm::vec3 color{1.0F};
			float range = 25.0F;
			float cosInner = 1.0F;
			float cosOuter = 0.0F;
			int shadowSlot = -1; // atlas tile, or -1 for an unshadowed light
			float shadowBias = 0.0015F;
			glm::mat4 lightViewProjection{1.0F};
		};

		[[nodiscard]] bool createShaderPrograms();
		[[nodiscard]] bool createFramebuffer(int width, int height);
		[[nodiscard]] bool createShadowResources();
		// Resolves every isLight entity into frameLights_, assigns atlas tiles
		// to the shadow casters, and builds each caster's light-space matrix
		// from the scene's own bounds. When a scene contains NO lights at all
		// this synthesises the single hardcoded directional light every shader
		// used to inline, so scenes authored before lights existed render
		// exactly as they did before.
		void collectLights(const std::vector<SceneEntity>& entities);
		// Depth-only pass filling each shadow caster's atlas tile.
		void renderShadowMaps(
			const std::vector<SceneEntity>& entities,
			const std::filesystem::path& projectRoot,
			int excludeEntityId);
		// Pushes frameLights_ plus the ambient/atlas uniforms into `program`.
		// Called once per lit program per frame, before it draws anything.
		void uploadLightUniforms(GLuint program) const;
		// Draws one entity's geometry with whatever program is already bound,
		// setting only `model`. Shared by the shadow pass across primitives,
		// terrain, text and imported meshes so a caster's shadow always
		// matches the shape actually drawn in the visible pass.
		void drawEntityGeometryForShadow(
			const SceneEntity& entity,
			const glm::mat4& model,
			GLuint staticProgram,
			GLuint skinnedProgram,
			const std::filesystem::path& projectRoot);
		void createPrimitiveMeshes();
		void createOutlineMesh();
		void createCameraIconMesh();
		void ensureTextMeshGpu(const SceneEntity& entity, const std::filesystem::path& projectRoot);
		void ensureTerrainGpu(const SceneEntity& entity, const std::filesystem::path& projectRoot);
		void ensureImportedMeshGpu(const SceneEntity& entity, const std::filesystem::path& projectRoot);
		// Per-frame: walks each bone's animation track (or its bind pose, if
		// the clip has no track for it) at `timeSeconds`, accumulates parent
		// world transforms, and combines with each bone's inverse bind
		// matrix to produce the final skinning matrix the vertex shader
		// weights against. Empty `animations` just returns the bind pose
		// (bones don't move, but the mesh still renders correctly skinned).
		[[nodiscard]] std::vector<glm::mat4> computeSkinningMatrices(
			const std::vector<ImportedBone>& bones,
			const std::vector<ImportedAnimationClip>& animations,
			double timeSeconds) const;

		GLuint lineShaderProgram_ = 0;
		GLuint meshShaderProgram_ = 0;
		// Second mesh program for isImportedMesh entities that came in with
		// skin weights (see ModelImportResult::hasSkeleton) - GPU-skins the
		// vertex position/normal from a `boneMatrices` uniform array instead
		// of taking them as-is. Shares meshFragmentShaderSource with
		// meshShaderProgram_ (lighting only, no textures either way).
		GLuint skinnedMeshShaderProgram_ = 0;
		// Third mesh program, isTerrain-only: blends up to 3 diffuse/normal/
		// height texture sets by the per-vertex splat weight (see
		// TerrainGpuEntry/TerrainLayerGpuEntry) instead of using a flat
		// baseColor - terrain is never skinned, so this is independent of
		// skinnedMeshShaderProgram_ above.
		GLuint terrainShaderProgram_ = 0;
		// Fourth mesh program: any non-terrain entity with a real
		// SceneEntity::materialBlendWeight (Appearance section's "mask
		// RGB") - blends up to 3 diffuse/normal/height sets via triplanar
		// projection (no real per-vertex UVs exist on the 6 shared
		// primitives, so this samples from local position/normal directly
		// instead, the same "no dedicated UV data" approach terrainShader-
		// Program_ already uses via world-space tiling).
		GLuint texturedMeshShaderProgram_ = 0;
		// Depth-only programs for the shadow pass. The skinned variant exists
		// so an animated character's shadow follows its animation instead of
		// freezing in the bind pose.
		GLuint shadowDepthShaderProgram_ = 0;
		GLuint shadowDepthSkinnedShaderProgram_ = 0;

		// Post-processing. The scene renders into sceneFramebuffer_, then the
		// post pass writes into framebuffer_/colorTexture_, which is what
		// texture() hands to ImGui and what blitToCurrentFramebuffer() copies.
		// With no effects configured the scene renders straight into
		// framebuffer_ as it always did, so the extra buffer costs nothing
		// when it is not used.
		GLuint postShaderProgram_ = 0;
		GLuint postVertexArray_ = 0;     // empty VAO; the triangle is gl_VertexID maths
		GLuint sceneFramebuffer_ = 0;
		GLuint sceneTexture_ = 0;
		GLuint sceneDepthBuffer_ = 0;
		CameraEffects cameraEffects_;
		std::filesystem::path effectsProjectRoot_;
		// Cached gradient-map textures, keyed by project-relative path - the
		// same load-once pattern the terrain layers and icons already use.
		std::unordered_map<std::string, GLuint> gradientTextureCache_;
		[[nodiscard]] GLuint ensureGradientTextureGpu(const std::string& relativePath);
		[[nodiscard]] bool createPostResources(int width, int height);
		void runPostProcess();

		GLuint shadowAtlasFramebuffer_ = 0;
		GLuint shadowAtlasTexture_ = 0;
		// Rebuilt every render() from the scene's isLight entities.
		std::vector<FrameLight> frameLights_;
		// Ambient fill. 0.30 is close to the 0.35 constant the old hardcoded
		// shading used, so an unlit scene keeps roughly its previous look.
		glm::vec3 ambientColor_{0.30F, 0.31F, 0.34F};

		std::array<GLuint, 6> primitiveVertexArrays_{};
		std::array<GLuint, 6> primitiveVertexBuffers_{};
		std::array<GLsizei, 6> primitiveVertexCounts_{};

		// Per-entity GPU mesh for isTextMesh entities - unlike the 6 shared
		// primitives, every TextMesh entity's geometry is unique to its own
		// content/font/size/depth, so each gets its own VAO/VBO, rebuilt only
		// when those fields actually change (tracked via the "last*" fields).
		struct TextMeshGpuEntry
		{
			GLuint vertexArray = 0;
			GLuint vertexBuffer = 0;
			GLsizei vertexCount = 0;
			std::string lastContent;
			std::string lastFontPath;
			float lastFontSize = 0.0F;
			float lastDepth = 0.0F;
			// Set after the first build attempt for the current last*
			// fields, success or not - without this, a build that FAILS
			// (bad font path, empty content) leaves vertexArray at 0
			// forever, and the old guard (`vertexArray != 0 && ...`) would
			// never short-circuit, re-running buildTextMesh() every single
			// render() call forever instead of once.
			bool buildAttempted = false;
		};
		std::unordered_map<int, TextMeshGpuEntry> textMeshCache_;

		// Per-entity GPU mesh for isTerrain entities - same "unique per
		// entity, rebuilt only when the source data changes" reasoning as
		// TextMeshGpuEntry above, just comparing the heightmap by value
		// instead of a font/string.
		// One splat layer's GL textures (diffuse always attempted; normal/
		// height are either the layer's own uploaded map or, when that path
		// is empty, generated from the diffuse texture's own brightness -
		// see TerrainTexture.hpp). Tracks each of the 3 source paths
		// independently so only a changed slot gets reloaded/regenerated.
		struct TerrainLayerGpuEntry
		{
			GLuint diffuseTexture = 0;
			GLuint normalTexture = 0;
			GLuint heightTexture = 0;
			std::string lastDiffusePath;
			std::string lastNormalPath;
			std::string lastHeightPath;
			bool loadAttempted = false;
		};

		struct TerrainGpuEntry
		{
			GLuint vertexArray = 0;
			GLuint vertexBuffer = 0;
			GLsizei vertexCount = 0;
			int lastResolution = 0;
			float lastWorldSize = 0.0F;
			float lastHeightScale = 0.0F;
			std::vector<float> lastHeights;
			std::vector<float> lastSplatWeights;
			std::array<TerrainLayerGpuEntry, 3> layers;
		};
		std::unordered_map<int, TerrainGpuEntry> terrainCache_;

		// Uploads (or regenerates) one splat layer's diffuse/normal/height
		// GL textures - normal/height fall back to TerrainTexture.hpp's
		// brightness-derived generation when that layer's own path is empty.
		// Declared here, after TerrainLayerGpuEntry/TerrainLayerData are both
		// visible, rather than up with the other ensureXGpu methods.
		void ensureTerrainLayerTexturesGpu(
			TerrainLayerGpuEntry& layerEntry, const TerrainLayerData& layerData, const std::filesystem::path& projectRoot);

		// Per-entity textures for the non-terrain "Appearance" material
		// (SceneEntity::materialLayers/materialBlendWeight) - reuses
		// TerrainLayerGpuEntry/ensureTerrainLayerTexturesGpu directly since
		// they were already generic (diffuse/normal/height loading, nothing
		// terrain-specific about the implementation), just keyed separately
		// since a regular entity has no TerrainGpuEntry of its own (its
		// geometry comes from the shared primitiveVertexArrays_/imported-
		// mesh/text-mesh caches, only the textures are per-entity here).
		std::unordered_map<int, std::array<TerrainLayerGpuEntry, 3>> materialCache_;

		// Per-entity GPU mesh for isImportedMesh entities - same pattern
		// again, rebuilt only when the source file path changes (an imported
		// mesh has no other parameters to vary post-import).
		struct ImportedMeshGpuEntry
		{
			GLuint vertexArray = 0;
			GLuint vertexBuffer = 0;
			GLsizei vertexCount = 0;
			std::string lastSourcePath;
			// Populated from the same loadModelMesh() call that builds
			// vertexBuffer above - cached here (not re-derived per frame)
			// since computeSkinningMatrices() needs them every render() call
			// while the geometry itself only needs rebuilding when
			// lastSourcePath changes.
			bool hasSkeleton = false;
			int vertexStride = 6;
			std::vector<ImportedBone> bones;
			std::vector<ImportedAnimationClip> animations;
			// Set after the first loadModelMesh() attempt for lastSourcePath,
			// success or not. Without this, a file that fails to import (no
			// triangulated mesh data, unsupported format, etc.) leaves
			// vertexArray at 0 forever - the old guard (`vertexArray != 0 &&
			// ...`) would never short-circuit, re-running the full Assimp
			// parse every single render() call forever instead of once. This
			// is what turned "picked a mesh-less animation-only FBX by
			// mistake" into an editor hang/freeze.
			bool loadAttempted = false;
		};
		std::unordered_map<int, ImportedMeshGpuEntry> importedMeshCache_;

		// Bound in place of a terrain layer slot's texture when that slot is
		// empty/failed to load, so every sampler in the terrain shader
		// always has a defined texture bound (sampling an unbound texture
		// unit is otherwise implementation-defined) - white diffuse (so an
		// empty layer just tints to baseColor's own value), a "pointing
		// straight up" flat normal, and a neutral mid-gray height.
		GLuint fallbackWhiteTexture_ = 0;
		GLuint fallbackFlatNormalTexture_ = 0;
		GLuint fallbackMidHeightTexture_ = 0;

		GLuint gridVertexArray_ = 0;
		GLuint gridVertexBuffer_ = 0;
		GLsizei gridVertexCount_ = 0;

		GLuint outlineVertexArray_ = 0;
		GLuint outlineVertexBuffer_ = 0;
		GLsizei outlineVertexCount_ = 0;

		// Shared wireframe icon drawn in place of the solid mesh for
		// isCineCamera entities - a small frustum shape, apex at the local
		// origin, base out along local +Z (this project's forward axis).
		GLuint cameraIconVertexArray_ = 0;
		GLuint cameraIconVertexBuffer_ = 0;
		GLsizei cameraIconVertexCount_ = 0;

		// A GL_LINES wireframe drawn in place of a solid mesh for the
		// gizmo-only entity kinds. Colour is baked per vertex (the line shader
		// takes a colour attribute), so each gizmo carries its own identity
		// and they stay distinguishable at a glance in a busy scene.
		struct GizmoMesh
		{
			GLuint vertexArray = 0;
			GLuint vertexBuffer = 0;
			GLsizei vertexCount = 0;
		};
		GizmoMesh emptyGizmo_;             // three axis crosshairs
		GizmoMesh directionalLightGizmo_;  // sun disc + rays + a long aim line
		GizmoMesh pointLightGizmo_;        // three orthogonal rings
		GizmoMesh spotLightGizmo_;         // cone opening along local +Z
		GizmoMesh uiElementGizmo_;         // small screen-ish rectangle

		void createGizmoMeshes();
		// Uploads one GL_LINES vertex buffer (x,y,z,r,g,b per vertex).
		[[nodiscard]] GizmoMesh uploadGizmoMesh(const std::vector<float>& vertices) const;
		void destroyGizmoMesh(GizmoMesh& mesh) const noexcept;

		GLuint framebuffer_ = 0;
		GLuint colorTexture_ = 0;
		GLuint depthBuffer_ = 0;
		int width_ = 0;
		int height_ = 0;

		float cameraYaw_ = 0.7F;
		float cameraPitch_ = 0.35F;
		float cameraDistance_ = 4.0F;
		glm::vec3 cameraTarget_{0.0F};
		bool showEditorGizmos_ = true;
		float cameraFieldOfViewDegrees_ = 50.0F;
		float cameraNearClip_ = 0.1F;
		float cameraFarClip_ = 200.0F;

		glm::mat4 lastView_{1.0F};
		glm::mat4 lastProjection_{1.0F};
	};
}
