#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "GameForger/Editor/AICommandBus.hpp"
#include "GameForger/Editor/AnimationData.hpp"

namespace gameforger::editor
{

	// Configures the Game view camera used by a script that calls
	// self.camera:setMode("fps"|"third_person") on this entity - see
	// scriptedPlayCamera() in main.cpp, which reads these every frame.
	struct EntityCameraRig
	{
		float fpsEyeHeight = 0.75F;
		float thirdPersonDistance = 4.0F;
		float thirdPersonHeight = 2.2F;
		float thirdPersonAimHeight = 1.1F;
		// Rotates the third-person camera around the entity, away from
		// directly-behind - e.g. for an over-the-shoulder angle.
		float thirdPersonYawOffsetDegrees = 0.0F;
		// lockCursor used to live here as a per-entity authored flag. Cursor
		// ownership is session state, not a property of a crate, so it moved
		// to GameplayState::cursorLockDesired, driven by whichever script
		// calls self.gameManager:setCursorLock(). See game_manager.lua.
	};

	// One paintable ground material - mirrors Unity's TerrainLayer asset:
	// each map type is an independently-uploadable texture slot rather than
	// one fixed bundle. `normalPath`/`heightPath` are optional - when empty,
	// the renderer derives that map from `diffusePath`'s own brightness
	// (see Terrain.cpp) instead of requiring a real authored map, so a
	// layer works immediately with just a color texture and can be
	// upgraded later by filling in real normal/height maps. More map slots
	// (roughness, AO, ...) can be added here later without reshaping the
	// rest of the splat system.
	struct TerrainLayerData
	{
		// Relative to projectRoot, e.g.
		// "Game/Textures/TEX_PACK_01/SAND_GROUND.jpg" - empty means this
		// layer slot is unused (paints nothing, see splatWeights below).
		std::string diffusePath;
		std::string normalPath;
		std::string heightPath;
	};

	// Data for an entity with isTerrain=true (see below). A real heightmap,
	// not a primitive standing in for one - `heights` is a resolution x
	// resolution grid (row-major, heights[row*resolution+col]), each value
	// 0..1, scaled to world units by heightScale. The renderer builds a
	// triangulated mesh from this (Terrain.hpp) and rebuilds it whenever a
	// sculpt brush or import/generator edits `heights`, the same "dirty and
	// rebuild" approach isTextMesh entities already use.
	struct TerrainData
	{
		int resolution = 65; // vertices per side - grid is resolution x resolution
		float worldSize = 50.0F; // width/depth in world units
		// Full peak-to-valley world-space range: heights[]=0.5 is the flat
		// baseline (world Y=0 relative to the entity), heights[]=1.0 reaches
		// +heightScale/2, heights[]=0.0 reaches -heightScale/2 (see
		// Terrain.cpp's positionAt).
		float heightScale = 12.0F;
		std::vector<float> heights; // resolution*resolution values, each 0..1
		// Up to 3 paintable ground materials, blended per-vertex by
		// splatWeights below - layers[i] pairs with weight channel i. The
		// splat brush paints these weights, not the textures themselves.
		std::array<TerrainLayerData, 3> layers;
		// resolution*resolution*3 values (R,G,B per texel, same grid as
		// heights, interleaved per texel) - each texel's 3 weights are kept
		// normalized to sum to 1; index i is how much of layers[i] shows at
		// that point. A freshly created terrain defaults to layer 0 at full
		// weight everywhere (see CreateTerrainCommand's handler,
		// EditorScene.cpp).
		std::vector<float> splatWeights;
		// World units per texture repeat, X/Z independently - e.g. (32,32)
		// or (20,128) for a stretched look. One shared scale for all 3
		// layers (there's a single tiled UV space, not a per-layer one -
		// see the terrain shader, ViewportRenderer.cpp). Defaults match
		// this feature's previous hardcoded shader constant, so existing
		// terrains look unchanged until edited.
		glm::vec2 uvScale{8.0F, 8.0F};
	};

	// Data for an entity with isImportedMesh=true (see below). `sourcePath`
	// is a copied-into-project asset under Game/Models/ (relative to
	// projectRoot), imported via Assimp (ModelImport.hpp) - unlike
	// TextMesh/Terrain, an imported mesh has no other parameters; its
	// geometry is a fixed bake of the source file's meshes, flattened
	// through the node hierarchy into one combined mesh. No materials,
	// textures, or skeletal data yet - see Known gaps.
	struct ImportedMeshData
	{
		std::string sourcePath;
	};

	// Data for an entity with isTextMesh=true (see below). The renderer builds
	// real extruded 3D glyph geometry from this (TextMesh.hpp), cached per
	// entity id and rebuilt whenever these fields change - unlike the 6
	// shared primitive meshes, text geometry is unique per entity.
	struct TextMeshData
	{
		std::string content = "Text";
		std::string fontPath; // relative to projectRoot, e.g. "Game/Fonts/Roboto-Regular.ttf"
		float fontSize = 1.0F; // world units per font em
		float depth = 0.2F; // extrusion thickness in world units
	};

	// Data for an entity with isPickupItem=true (see below). Unlike
	// isTerrain/isTextMesh/isImportedMesh (which replace what the entity's
	// geometry IS), this is an additive trait like hasCollider - any
	// primitive, imported mesh, or text mesh can also be a pickup item, its
	// own shape/geometry is unaffected. Purely data (name + inventory icon);
	// the actual pickup interaction is fully automatic/engine-side (see
	// tickPickupInteraction, main.cpp), not something a script configures.
	struct PickupItemData
	{
		std::string itemName = "Item";
		// Relative to projectRoot, e.g.
		// "Game/Models/iconpack1/128/SwordT1.png" - shown in the inventory
		// grid once picked up.
		std::string iconPath;
	};

	// Additive audio source, same shape as the unused AudioSourceComponent
	// stub. Playback goes through core::AudioEngine; this is the authored
	// data that round-trips through the scene file.
	// Per-source DSP. "Echo" is not a separate effect - it is delay with
	// feedback, so one delay node covers both and the panel labels it that way
	// rather than shipping two controls that do the same thing.
	struct AudioEffects
	{
		bool reverb = false;
		float reverbRoomSize = 0.5F;   // 0..1
		float reverbDamping = 0.5F;    // 0..1
		float reverbWet = 0.3F;        // 0..1
		float reverbDry = 0.7F;        // 0..1

		bool delay = false;
		float delaySeconds = 0.25F;    // 0.01..2
		float delayDecay = 0.4F;       // 0..0.99 - feedback; this is what makes it an echo
		float delayWet = 0.35F;        // 0..1
		float delayDry = 1.0F;         // 0..1

		enum class Filter
		{
			None,
			LowPass,   // muffled / behind a wall
			HighPass   // thin / telephone
		};
		Filter filter = Filter::None;
		float cutoffHz = 1000.0F;      // 20..20000

		[[nodiscard]] bool anyEnabled() const noexcept
		{
			return reverb || delay || filter != Filter::None;
		}
	};

	[[nodiscard]] const char* audioFilterName(AudioEffects::Filter filter) noexcept;
	[[nodiscard]] bool audioFilterFromName(const std::string& name, AudioEffects::Filter& outFilter) noexcept;

	struct AudioSourceData
	{
		std::string clipAssetPath;
		float volume = 1.0F;
		float pitch = 1.0F;
		bool loop = false;
		bool playOnAwake = true;
		bool is3D = true;
		float minDistance = 1.0F;
		float maxDistance = 50.0F;
		// Fades are a property of the sound itself, not a DSP node - miniaudio
		// ramps the voice's own volume - so they live here beside volume and
		// pitch rather than in AudioEffects. Putting them in AudioEffects would
		// also make anyEnabled() true and build a node graph for nothing.
		// 0 means no fade: start at full volume, stop instantly.
		float fadeInSeconds = 0.0F;
		float fadeOutSeconds = 0.0F;
		AudioEffects effects;
	};

	// Data for an entity with isCastle=true (see below) - additive, like
	// hasCollider/isPickupItem. Projectile hit-testing uses colliderWorldAabb
	// (mesh bounds for imported models, otherwise position +/- scale).
	struct CastleData
	{
		float hp = 100.0F;
		float maxHp = 100.0F;
	};

	// Data for an entity with isCatapult=true (see below) - additive, like
	// hasCollider/isPickupItem. `yawEntityName`/`armEntityName` are explicit
	// references (not a naming convention) to two other entities in the
	// scene - normally children of this one via parentName/localPosition
	// (see SceneEntity below) - that the aiming system rotates directly:
	// yawEntityName's world Y rotation for left/right aim, armEntityName's
	// world X rotation for the launch angle. See the catapult aiming code
	// in drawGameViewPanel (main.cpp).
	struct CatapultData
	{
		std::string yawEntityName;
		std::string armEntityName;
		float minPitchDegrees = 5.0F;
		float maxPitchDegrees = 70.0F;
		float launchSpeed = 22.0F;
	};

	// Unity-style collider shapes, used only when hasCollider is true.
	// Box = solid AABB, Mesh = triangle mesh (hollow), Convex = solid hull.
	enum class ColliderType
	{
		Box,
		Mesh,
		Convex
	};

	[[nodiscard]] inline const char* colliderTypeToString(const ColliderType type)
	{
		switch (type)
		{
			case ColliderType::Mesh:
				return "mesh";
			case ColliderType::Convex:
				return "convex";
			case ColliderType::Box:
			default:
				return "box";
		}
	}

	[[nodiscard]] inline ColliderType colliderTypeFromString(const std::string& text)
	{
		if (text == "mesh")
		{
			return ColliderType::Mesh;
		}
		if (text == "convex")
		{
			return ColliderType::Convex;
		}
		return ColliderType::Box;
	}

	struct SceneEntity
	{
		int id = 0;
		std::string name;
		// Whether this entity is active in the scene (controls rendering,
		// scripts, and physics).
		bool active = true;
		// In priority order - tags[0] is "primary" (used anywhere a single
		// tag is needed, e.g. the AI Forge scene summary). Empty means
		// untagged; UI code displays "Untagged" for an empty list rather than
		// storing that as a real tag.
		std::vector<std::string> tags;
		PrimitiveType primitive = PrimitiveType::Cube;
		glm::vec3 position{0.0F};
		// Euler-angle rotation in DEGREES. Convention: XYZ order
		// (ImGuizmo::RecomposeMatrixFromComponents, applied by
		// composeEntityPivotFrame in Transform.cpp). Every Euler
		// consumer in the engine (gizmo, animation sampling, parent-
		// constraint solver, Lua entity:rotation get/set) MUST interpret
		// this field as XYZ order; switching to YXZ or ZYX would
		// silently rotate every entity to the wrong orientation. If a
		// future feature needs per-entity rotation order, store it as a
		// separate field rather than re-interpreting this one.
		glm::vec3 rotationEuler{0.0F};
		glm::vec3 scale{1.0F};
		glm::vec3 color{0.8F, 0.8F, 0.8F};
		// Offset (in the primitive's own [-1,1] local space) that Move/Rotate/Scale
		// pivot around, instead of the mesh's geometric center — e.g. a door's
		// hinge edge. Position always refers to this pivot's world location.
		glm::vec3 pivotOffset{0.0F};
		std::vector<std::string> scripts;
		EntityAnimation animation;
		EntityCameraRig cameraRig;
		// Whether self.physics:resolve() (see Collision.hpp / ScriptRuntime)
		// treats this entity as solid. Checking this on a parent also makes
		// descendant meshes solid. Shape is `colliderType` (Unity-style Box /
		// Mesh / Convex) - the Inspector only shows those after this flag.
		bool hasCollider = false;
		// Box: solid AABB (primitive scale box, or imported mesh bounds).
		// Mesh: triangle mesh (hollow rooms/walls). Convex: solid convex hull.
		// Imported models default to Mesh (CreateImportedMeshCommand).
		ColliderType colliderType = ColliderType::Box;
		// Whether this entity can be picked up (see PickupItemData above,
		// and tickPickupInteraction/main.cpp for the actual interaction) -
		// additive, like hasCollider, not a shape replacement.
		bool isPickupItem = false;
		PickupItemData pickupItem;
		bool hasAudioSource = false;
		AudioSourceData audioSource;
		// Whether this entity is a destructible castle (see CastleData
		// above) - additive, like hasCollider. Normally paired with
		// hasCollider=true, since colliderWorldAabb is what a gravity
		// projectile's hit test uses.
		bool isCastle = false;
		CastleData castle;
		// Whether this entity is a catapult's aiming root (see CatapultData
		// above) - additive, like hasCollider.
		bool isCatapult = false;
		CatapultData catapult;
		// When true, this entity is a 3D Text Mesh (see TextMeshData/TextMesh.hpp)
		// instead of one of the shared PrimitiveType meshes - `primitive` is
		// ignored for rendering/picking in that case.
		bool isTextMesh = false;
		TextMeshData textMesh;
		// When true, this entity is a cine-camera (cutscene camera, separate
		// from the player/scripted Game view camera) - renders as a small
		// wireframe icon instead of a solid primitive. Its path is just its
		// own EntityAnimation keyframes (see `animation` above) - no separate
		// waypoint data structure. See CineShot/StoryboardState in main.cpp.
		bool isCineCamera = false;
		// When true, this entity is a real game Light (see LightData above) -
		// renders as a wireframe gizmo instead of a solid primitive, and
		// contributes to shading for every other entity. `primitive` is
		// ignored for rendering/picking.
		bool isLight = false;
		LightData light;
		// When true, this entity is a real game Camera (see CameraData above)
		// - renders as a wireframe frustum instead of a solid primitive.
		// Distinct from isCineCamera; `primitive` is ignored.
		bool isCamera = false;
		CameraData camera;
		// When true, this entity is a screen-space UI element (see
		// UIElementData above) - additive, and it draws in the Game view only
		// while parented under the active camera. It has no 3D geometry at
		// all, so like a Light it is skipped by the mesh pass.
		bool isUIElement = false;
		UIElementData ui;
		// When true, this entity is a real heightmap Terrain (see
		// TerrainData/Terrain.hpp) instead of one of the shared PrimitiveType
		// meshes - `primitive` is ignored for rendering/picking in that case.
		bool isTerrain = false;
		TerrainData terrain;
		// When true, this entity's geometry comes from an imported GLB/glTF/
		// FBX file (see ImportedMeshData/ModelImport.hpp) instead of one of
		// the shared PrimitiveType meshes - `primitive` is ignored for
		// rendering/picking in that case.
		bool isImportedMesh = false;
		ImportedMeshData importedMesh;
		// Optional textured material - applies to any entity (primitives,
		// imported meshes, text meshes; ignored for isTerrain, which has its
		// own per-vertex-painted layers/splatWeights instead, see
		// TerrainData). Reuses TerrainLayerData directly (despite the name,
		// it's just "one diffuse/normal/height texture slot set" - already
		// generic) rather than duplicating an identical struct, since a
		// regular object doesn't have a paintable surface to brush onto like
		// terrain does - materialBlendWeight is one constant RGB mix for the
		// whole object instead of a per-vertex map. All-zero weight (the
		// default) means untextured, same flat `color` rendering as before
		// this existed.
		std::array<TerrainLayerData, 3> materialLayers;
		glm::vec3 materialBlendWeight{0.0F};
		// World units per texture repeat, X/Z independently, for the
		// triplanar projection above - same idea and default as
		// TerrainData::uvScale, just per-object instead of per-terrain
		// (e.g. a wall cube stretched tall wants a different scale than a
		// floor plane).
		glm::vec2 materialUvScale{2.0F, 2.0F};
		// True parent-child hierarchy (Unity-style): while non-empty,
		// this entity is a child of the named parent entity, and
		// `localPosition`/`localRotationEuler`/`localScale` below are
		// authored RELATIVE TO the parent's own transform instead of the
		// world - the same convention Unity uses. `position`/
		// `rotationEuler`/`scale` above remain the WORLD transform
		// (unchanged meaning, still what rendering/physics/picking/
		// scripts all read directly) - they're recomputed every frame
		// from local* + the parent's world transform by
		// applyParentConstraints (main.cpp), which runs regardless of
		// Play state so children visibly follow while authoring too.
		// Recursive to any depth (a child's parent can itself be a
		// child). If the named parent can't be found (typo, deleted) or
		// would form a cycle, this entity just keeps its last resolved
		// world transform, unaffected - not an error.
		std::string parentName;
		glm::vec3 localPosition{0.0F, 1.0F, 0.0F};
		glm::vec3 localRotationEuler{0.0F};
		glm::vec3 localScale{1.0F};
	};

	// The tag anything that only understands "one tag per object" should use
	// - the entity's highest-priority (first) tag, or "Untagged" if it has none.
	[[nodiscard]] inline std::string primaryTag(const SceneEntity& entity)
	{
		return entity.tags.empty() ? "Untagged" : entity.tags.front();
	}

	// True for entities that have no solid geometry to draw: the transform-only
	// Empty, and the four gizmo/overlay kinds (cine camera, light, camera, UI).
	// The mesh pass skips these, and picking uses a small fixed-size box around
	// their origin rather than mesh bounds.
	//
	// Use this rather than testing the flags one at a time - before it existed
	// the renderer only knew about isCineCamera, so every new gizmo kind had to
	// find and update the same scattered checks. Anything added here is skipped
	// by every mesh consumer at once.
	[[nodiscard]] inline bool isGizmoOnlyEntity(const SceneEntity& entity) noexcept
	{
		return entity.isCineCamera || entity.isLight || entity.isCamera || entity.isUIElement
			|| (entity.primitive == PrimitiveType::Empty && !entity.isTerrain && !entity.isTextMesh
				&& !entity.isImportedMesh);
	}

	class EditorScene final
	{
	public:
		explicit EditorScene(std::filesystem::path projectRoot);

		[[nodiscard]] const std::filesystem::path& projectRoot() const noexcept { return projectRoot_; }

		[[nodiscard]] AICommandResult execute(const AIEditorCommand& command);
		[[nodiscard]] const std::vector<SceneEntity>& entities() const noexcept;
		[[nodiscard]] const SceneEntity* findEntity(int id) const noexcept;
		[[nodiscard]] const SceneEntity* findEntity(const std::string& name) const noexcept;

		// Direct, unvalidated mutable access for editor-internal bookkeeping that
		// isn't a scene "property" in the AICommand sense (animation keyframe
		// editing). Transform/tag/color edits should keep going through execute().
		[[nodiscard]] SceneEntity* findEntityMutable(int id) noexcept;

		// Bypasses command validation: used to restore a pre-Play snapshot, not for
		// AI- or user-driven edits. Preserves each entity's existing id.
		void replaceEntities(std::vector<SceneEntity> entities) noexcept;

		// Replaces the scene with freshly-loaded entities (e.g. from
		// SceneSerializer::loadScene), assigning each a new id and advancing the
		// id counter past them - the save format identifies entities by name,
		// not id, so loaded entities never carry one.
		void loadEntities(std::vector<SceneEntity> entities) noexcept;

	private:
		[[nodiscard]] bool nameInUse(const std::string& name) const noexcept;
		[[nodiscard]] std::string makeUniqueName(const std::string& baseName) const;
		[[nodiscard]] SceneEntity* findEntityMutable(const std::string& name) noexcept;

		std::filesystem::path projectRoot_;
		std::vector<SceneEntity> entities_;
		int nextEntityId_ = 1;
	};
}
