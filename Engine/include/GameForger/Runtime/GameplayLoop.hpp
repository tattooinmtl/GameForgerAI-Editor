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

		// One entry per distinct item name, with a count rather than one
		// entry per pickup - see the Editor's original PlayModeState
		// comment (now here) for why.
		struct InventoryItem
		{
			std::string itemName;
			std::string iconPath;
			int count = 0;
			glm::vec3 scale{1.0F};
			glm::vec3 color{0.8F};
			glm::vec3 materialBlendWeight{0.0F};
			std::array<TerrainLayerData, 3> materialLayers;
			glm::vec2 materialUvScale{2.0F, 2.0F};
		};
		std::vector<InventoryItem> inventoryItems;

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
	// projectiles (useGravity=true) hit-test via collider AABB against hasCollider
	// entities matching hitTag and, on a hit against an isCastle entity,
	// push a "Castle"/"hp" SetPropertyCommand through commandBus (and set
	// gameplay.gameOverMessage the first time a castle's hp reaches 0) -
	// non-gravity projectiles keep the original sphere/tag-only check with
	// no entity mutation, unchanged.
	void tickProjectiles(
		const EditorScene& scene, AICommandBus& commandBus, GameplayState& gameplay, bool isPlaying,
		float deltaTime);
}
