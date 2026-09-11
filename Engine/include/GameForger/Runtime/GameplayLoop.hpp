#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "GameForger/Editor/AICommandBus.hpp"
#include "GameForger/Editor/EditorScene.hpp"
#include "GameForger/Editor/ProjectSettings.hpp"
#include "GameForger/Editor/ScriptRuntime.hpp"

namespace gameforger::core
{
	// Forward-declared rather than including AudioEngine.hpp: only
	// bindSharedScriptCallbacks needs it, and it takes a reference, so pulling
	// miniaudio's header into everything that includes GameplayLoop.hpp would
	// be a real compile-time cost for one signature.
	class AudioEngine;
}

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

		// What the Game Manager script last asked for via
		// self.gameManager:setCursorLock(). Replaces the per-entity
		// EntityCameraRig::lockCursor checkbox: cursor ownership belongs to
		// whichever script is driving the player, not to every entity in the
		// scene. Shared so Runtime and Editor Play agree.
		bool cursorLockDesired = false;

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

		// Set by tickProjectiles when a projectile despawns from a hit this
		// frame. The host fires OnProjectileHit audio hooks from this, so
		// GameplayLoop does not have to know about AudioEngine.
		int projectilesHitThisTick = 0;
		int projectilesFiredThisTick = 0;

		// Runtime state for ProjectSettings::bootSequence - the steps that run
		// once when Play starts, before the player gets control. Lives here
		// rather than in the Editor's PlayModeState because Runtime must play
		// the identical sequence standalone.
		struct BootSequenceState
		{
			// True from Play start until the last step finishes. Never
			// restarts mid-session.
			bool running = false;
			std::size_t stepIndex = 0;
			float stepElapsedSeconds = 0.0F;

			// While true the host skips tickScripts, which is what actually
			// stops the player moving. Animations still tick, so a logo or
			// intro camera move plays over a frozen player.
			bool playerInputLocked = false;

			// Set when a play_cutscene / play_audio step begins, and cleared
			// by the host once it has acted on it. The sequence does not
			// advance past such a step until the host reports it finished, so
			// a cutscene of unknown length still gates correctly.
			std::string requestedCutsceneShot;
			std::string requestedAudioClip;
			// Host sets this to signal "the thing you asked for is done".
			bool hostStepFinished = false;
		};
		BootSequenceState bootSequence;
	};

	// Per-frame reset of the counters that mean "this happened since the last
	// tick". Must run BEFORE scripts, because scripts are what increment them.
	//
	// This exists because the Editor reset projectilesFiredThisTick in its own
	// tick and the Runtime never did - so in a shipped game the counter
	// accumulated forever. Putting it in Engine, called by both hosts, is what
	// stops the next per-frame counter repeating the mistake.
	void beginGameplayFrame(GameplayState& gameplay) noexcept;

	// Binds every ScriptRuntime callback that is backed purely by
	// GameplayState, and every one backed purely by the AudioEngine.
	//
	// WHY THIS EXISTS. Both hosts used to bind these by hand, and the bodies
	// were meant to be identical. They were not: the Runtime stubbed four of
	// them - returning false, discarding the value, doing nothing - so
	// catapult firing, catapult aiming and held-item state all worked on Play
	// and were silently inert in the shipped game (MissingFunctions.md 1b).
	// Nothing caught it, because both hosts compiled and both passed tests.
	//
	// Binding them once, here, makes that class of defect structurally
	// impossible rather than merely tested for: a callback added to
	// ScriptRuntime::Config and bound here is bound in both hosts by
	// construction. Only logCallback stays host-specific, because it genuinely
	// differs - the Editor routes to its Console panel, the Runtime to stderr.
	void bindSharedScriptCallbacks(
		ScriptRuntime::Config& config,
		GameplayState& gameplay,
		core::AudioEngine& audioEngine,
		const std::filesystem::path& projectRoot);


	// Call once when Play starts. Arms the sequence and locks player input if
	// there is anything to run; with no steps the player has control
	// immediately, exactly as before this feature existed.
	void resetBootSequence(GameplayState& gameplay, const std::vector<BootStep>& steps);

	// Advances the sequence. No-op unless isPlaying and the sequence is still
	// running. `scene` is used to drive play_animation steps, which wait for
	// the target entity's own authored animation to finish.
	void tickBootSequence(
		const std::vector<BootStep>& steps,
		const EditorScene& scene,
		GameplayState& gameplay,
		bool isPlaying,
		float deltaTime);

	// True while the boot sequence is holding the player still. The host uses
	// this to gate tickScripts - see BootSequenceState::playerInputLocked.
	[[nodiscard]] bool bootSequenceBlocksInput(const GameplayState& gameplay) noexcept;

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
