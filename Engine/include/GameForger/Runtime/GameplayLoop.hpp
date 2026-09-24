#pragma once

#include <array>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "GameForger/Editor/AICommandBus.hpp"
#include "GameForger/Editor/EditorScene.hpp"
#include "GameForger/Editor/ScriptRuntime.hpp"

namespace gameforger::editor
{
	// Shared by both the catapult aim-preview line (main.cpp) and the real
	// gravity-projectile flight (tickProjectiles below) - kept as one
	// constant so the two integrations can never silently drift apart.
	constexpr float kProjectileGravity = -20.0F;

	// The subset of what used to be the Editor-only PlayModeState that is
	// genuine runtime/session state - not Editor-Play-button bookkeeping
	// (saved pre-Play snapshots, the Editor's own separate Play-mode
	// viewport camera, etc, which stay in the Editor's PlayModeState and
	// have no meaning for GameForgerRuntime). Shared so both the Editor's
	// Play mode and Runtime's standalone game loop tick identical gameplay
	// logic (see tickScripts/tickProjectiles/tickPlayModeAnimations below).
	struct GameplayState
	{
		float playElapsedTime = 0.0F;

		bool cursorCurrentlyLocked = false;

		// Degrees - the player's current mouse-look, copied in by the host
		// (Editor Game view / Runtime main loop) every frame before scripts
		// tick, so scripts can read where the camera actually points
		// (self.camera:getPitch()/getAim()) - e.g. to tilt first-person
		// hands with the view or aim a hitscan weapon.
		float lookPitchDegrees = 0.0F;
		float lookYawDegrees = 0.0F;

		// One inventory slot. Slots are fixed (kInventorySlotCount of them,
		// see ensureInventorySlots) so the grid can show empty slots and
		// keep an item where the player dragged it; count == 0 = empty.
		// A slot stacks several of the same item (same itemName) when the
		// item allows stacking.
		struct InventoryItem
		{
			std::string itemName;
			std::string iconPath;
			int count = 0;
			// Legacy "Is Pickup Item" pickups are deleted on pickup and
			// re-spawned as a cube on drop, so their look is captured here.
			glm::vec3 scale{1.0F};
			glm::vec3 color{0.8F};
			glm::vec3 materialBlendWeight{0.0F};
			std::array<TerrainLayerData, 3> materialLayers;
			glm::vec2 materialUvScale{2.0F, 2.0F};
			// items.lua pickups (see kItemScriptPath) - the real object is
			// kept, just hidden (active = false) while it's in the bag, and
			// shown again in front of the player on drop, so its mesh,
			// children, material and scripts all survive the round trip.
			std::string itemType;  // items.lua item_type, e.g. "weapon", "consumable", "misc"
			std::string weapon;    // items.lua weapon, e.g. "sword" - empty for non-weapons
			bool stackable = true;
			int maxStack = 99;
			std::vector<std::string> storedEntityNames;

			[[nodiscard]] bool empty() const noexcept { return count <= 0; }
		};
		std::vector<InventoryItem> inventoryItems;
		// 0-based index into inventoryItems of the equipped/selected slot.
		// Slots 0..kHotbarSlotCount-1 are the hotbar (keys 1-8).
		int selectedSlot = 0;
		// True while the host shows the inventory grid (I) - the mouse
		// belongs to the UI then (self.inventory:isOpen()).
		bool inventoryOpen = false;

		// Short-lived visual effects spawned by scripts (self.world:
		// spawnBeam/spawnFlash) - tracers, taser arcs, chain lightning,
		// muzzle flashes, impact sparks. Pure visuals: drawn by the host
		// as a screen-space overlay, no collision. Aged by tickEffects.
		struct Beam
		{
			glm::vec3 from{0.0F};
			glm::vec3 to{0.0F};
			glm::vec3 color{1.0F};
			float width = 2.0F;
			float remainingSeconds = 0.1F;
			float totalSeconds = 0.1F;
			// Zig-zag lightning look instead of a straight line; re-rolled
			// every frame from `seed` + time so it crackles.
			bool jagged = false;
			unsigned int seed = 0;
		};
		std::vector<Beam> beams;
		struct Flash
		{
			glm::vec3 position{0.0F};
			glm::vec3 color{1.0F};
			float size = 12.0F; // pixels at full strength
			float remainingSeconds = 0.08F;
			float totalSeconds = 0.08F;
			// A particle (self.world:particle) lives exactly one frame at a
			// fixed `intensity` - scripts that simulate their own particles
			// (effects.lua) re-emit them every frame.
			bool particle = false;
			int framesLeft = 0;
			float intensity = 1.0F;
		};
		std::vector<Flash> flashes;
		// Rising, fading world-space text (damage numbers, "+25 XP",
		// "LEVEL UP") - self.world:spawnText.
		struct FloatingText
		{
			glm::vec3 position{0.0F};
			std::string text;
			glm::vec3 color{1.0F};
			float scale = 1.0F;
			float remainingSeconds = 1.0F;
			float totalSeconds = 1.0F;
		};
		std::vector<FloatingText> floatingTexts;
		// Screen bars stacked in the bottom-left corner (player health, XP,
		// mana...) - self.world:setHudBar(id, ...). Sorted by `order`.
		struct HudBar
		{
			std::string id;
			std::string label;
			float fraction = 1.0F;
			glm::vec3 color{1.0F};
			int order = 0;
		};
		std::vector<HudBar> hudBars;
		// One centered message line ("Picked up Sword", "LEVEL 3 - Fire
		// Caster unlocked!") - self.world:showMessage / pickups.
		std::string messageText;
		float messageSecondsRemaining = 0.0F;

		// Engine-managed projectiles, spawned via a script's self.world:
		// fireProjectile(...) - see ScriptRuntime::ProjectileSpawnCallback -
		// or by the catapult firing (main.cpp). `useGravity` defaults false
		// so existing straight-line callers (enemy_ai/ranged_attacker) are
		// unaffected; the catapult sets it true for an arced flight, and is
		// the only thing that opts into the AABB/HP hit path in
		// tickProjectiles instead of the original sphere/tag-only check.
		struct Projectile
		{
			glm::vec3 position{0.0F};
			glm::vec3 velocity{0.0F};
			std::string hitTag;
			float remainingLifetimeSeconds = 4.0F;
			bool useGravity = false;
		};
		std::vector<Projectile> projectiles;

		// Name of the entity currently parented into the player's hand via
		// the F-key hold/drop interaction (main.cpp), or empty if nothing is
		// held. Read by self.world:isHoldingItem() (ScriptRuntime) so the
		// player controller scripts can cap movement to walk speed while
		// carrying something.
		std::string heldItemEntityName;

		// True while the player is operating a catapult - set/cleared by
		// catapult_controller.lua itself via self.world:
		// setOperatingCatapult(bool) as it enters/leaves aim mode (E/R,
		// read via self.input inside the script, not engine-side input
		// handling anymore). While true, self.world:isAimingCatapult()
		// reports true so the player controller scripts freeze WASD
		// movement, and main.cpp's own camera-look mouse consumption is
		// skipped so it doesn't fight the script's own aim-by-mouse.
		bool playerOperatingCatapult = false;

		// Counts down while no gameOverMessage is set; at <=0 the
		// EnemyCatapult-tagged entity auto-fires at its authored fixed
		// facing and resets this - see the enemy auto-fire tick (main.cpp).
		float enemyCatapultFireTimerSeconds = 8.0F;

		// Non-empty once either castle's hp has reached 0 - drives the
		// win/lose banner and gates further E/R/F input and the enemy
		// auto-fire timer (main.cpp). Set once, never cleared during a Play
		// session.
		std::string gameOverMessage;
	};

	// Resolves every parented entity's world position/rotation/scale from
	// its parent chain's current transform + its own local* fields,
	// recursively, and writes the result back through commandBus - see
	// EditorScene.hpp's SceneEntity::parentName/local* fields. Runs
	// unconditionally (both while editing and during Play/standalone
	// gameplay) so parented objects (e.g. a turret on a tower) stay
	// attached live.
	void applyParentConstraints(const EditorScene& scene, AICommandBus& commandBus);

	// Advances every entity's authored keyframe animation (EntityAnimation)
	// by deltaTime. No-op unless isPlaying (an Editor Viewport preview
	// never runs animations - only Play/standalone gameplay does).
	void tickPlayModeAnimations(
		EditorScene& scene, AICommandBus& commandBus, GameplayState& gameplay, bool isPlaying, float deltaTime);

	// Calls :on_update(deltaTime) on every running script instance. No-op
	// unless isPlaying and the ScriptRuntime is actually initialized.
	void tickScripts(const EditorScene& scene, ScriptRuntime& scriptRuntime, bool isPlaying, float deltaTime);

	// Moves/despawns every active engine-managed projectile - see
	// GameplayState::Projectile. No-op unless isPlaying. Gravity
	// projectiles (useGravity=true) hit-test via AABB against hasCollider
	// entities matching hitTag and, on a hit against an isCastle entity,
	// push a "Castle"/"hp" SetPropertyCommand through commandBus (and set
	// gameplay.gameOverMessage the first time a castle's hp reaches 0) -
	// non-gravity projectiles keep the original sphere/tag-only check with
	// no entity mutation, unchanged.
	void tickProjectiles(
		const EditorScene& scene, AICommandBus& commandBus, GameplayState& gameplay, bool isPlaying,
		float deltaTime);

	// Ages/despawns GameplayState::beams/flashes. No-op unless isPlaying.
	void tickEffects(GameplayState& gameplay, bool isPlaying, float deltaTime);

	// ---- Inventory (shared by the Editor's Play mode and Runtime) ----

	// Any object with this script attached can be picked up with E and
	// stored in the inventory - its item name/icon/type/weapon come from
	// the script's own @property values (per object, see
	// SceneEntity::scriptProperties).
	inline constexpr const char* kItemScriptPath = "Game/Scripts/items.lua";
	inline constexpr int kInventorySlotCount = 24;
	inline constexpr int kHotbarSlotCount = 8;

	// Pads/trims inventoryItems to exactly kInventorySlotCount slots and
	// keeps selectedSlot in range. Call when Play starts.
	void ensureInventorySlots(GameplayState& gameplay);

	// Stacks onto an existing slot with the same itemName (when both allow
	// stacking and the stack isn't full), otherwise fills the first empty
	// slot. Returns the slot index used, or -1 if the bag is full.
	int addInventoryItem(GameplayState& gameplay, GameplayState::InventoryItem item);

	// True if `entity` has an inventory-providing script: the legacy
	// inventory_system.lua, or any running script that sets the field
	// `inventory_enabled = true` (e.g. fps_player.lua).
	[[nodiscard]] bool entityProvidesInventory(const SceneEntity& entity, const ScriptRuntime& scriptRuntime);

	// Nearest pickable object (legacy "Is Pickup Item" or an items.lua
	// object) that is active in the hierarchy, within horizontalRange (X/Z)
	// and heightTolerance (Y) of `origin`, excluding `excludeId` - or null.
	[[nodiscard]] const SceneEntity* findPickupItemCandidate(
		const EditorScene& scene, const glm::vec3& origin, float horizontalRange, float heightTolerance,
		int excludeId);

	// Display name for the "[E] Pick up X" hint.
	[[nodiscard]] std::string pickupDisplayName(const SceneEntity& entity, const ScriptRuntime& scriptRuntime);

	// Moves `candidate` into the inventory. Legacy pickups are deleted (their
	// look captured for re-spawn); items.lua objects are hidden and kept.
	// Returns false (nothing changes) if the bag is full.
	bool pickUpItem(
		EditorScene& scene, AICommandBus& commandBus, GameplayState& gameplay, const ScriptRuntime& scriptRuntime,
		const SceneEntity& candidate);

	// Takes one item out of `slotIndex` and puts it back into the world at
	// `dropPosition` facing `yawDegrees`. Returns false if the slot is empty.
	bool dropInventoryItem(
		EditorScene& scene, AICommandBus& commandBus, GameplayState& gameplay, int slotIndex,
		const glm::vec3& dropPosition, float yawDegrees);
}
