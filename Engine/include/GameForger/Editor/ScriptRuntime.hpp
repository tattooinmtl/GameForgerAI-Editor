#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>

#include "GameForger/Editor/AICommandBus.hpp"
#include "GameForger/Editor/EditorScene.hpp"
#include "GameForger/Editor/InputSource.hpp"

struct lua_State;
struct lua_Debug;

namespace gameforger::editor
{
	struct GameplayState;

	// Embeds a single Lua 5.4 VM and runs attached entity scripts against it for
	// the duration of one Play session. Scripts follow this project's existing
	// convention (see Game/Scripts/player_controller.lua): a chunk that returns
	// a table with on_start()/on_update(delta_time) methods. Before on_start()
	// is called, `self.entity` (position/rotation get/set, getScale, getForward/
	// getRight), `self.input` (isKeyDown/isKeyPressed/getAxis), `self.camera`
	// (setMode/getMode, "fps" or "third_person"), and `self.physics`
	// (resolve(position, halfWidth, height) -> correctedPosition, grounded -
	// simple AABB collision/ground-check against Collider-enabled entities),
	// and `self.world` (findNearestWithTag(tag) -> position|nil, distance,
	// name - nearest OTHER entity carrying that tag anywhere in its tags
	// list, not just primaryTag; findPositionByTag(tag) -> position|nil,
	// same search but position only, for muzzle/spawn-point markers;
	// fireProjectile(fromPosition, toPosition, speed, hitTag) - spawns an
	// engine-managed projectile, see PlayModeState::Projectile/
	// ProjectileSpawnCallback below and tickProjectiles() in main.cpp, NOT
	// a scripted entity of its own) are attached.
	//
	// Sandboxing: only the base, table, string, and math libraries are opened -
	// no io/os/package/debug, so scripts can't touch the filesystem or process
	// through those. The base library's own dofile/loadfile are additionally
	// removed after loading it, since those two read+execute an arbitrary file
	// by path regardless of io/os. startScript() also re-validates its own
	// path (confined to Game/Scripts, .lua only) immediately before loading,
	// independent of whatever validation ran when the script was attached -
	// this is the one choke point every script execution goes through, so a
	// hand-edited/untrusted scene file can't bypass confinement by skipping
	// the normal attach command. `print()` is redirected to the log callback
	// instead of a console-less window's stdout.
	class ScriptRuntime final
	{
	public:
		// (isError, message)
		using LogCallback = std::function<void(bool, const std::string&)>;
		// (fromPosition, toPosition, speed, hitTag) - see self.world:
		// fireProjectile above. The engine owns moving/colliding/despawning
		// the actual projectile (main.cpp's PlayModeState::projectiles/
		// tickProjectiles) - this callback just enqueues one, so
		// ScriptRuntime never needs to know about PlayModeState.
		using ProjectileSpawnCallback =
			std::function<void(const glm::vec3&, const glm::vec3&, float, const std::string&)>;
		// Answers a simple yes/no question about session state the engine
		// owns (main.cpp's GameplayState), for self.world:isHoldingItem()/
		// isAimingCatapult() - same "engine owns the state, ScriptRuntime
		// just asks" shape as ProjectileSpawnCallback, so ScriptRuntime
		// never needs to know about GameplayState itself.
		using BoolQueryCallback = std::function<bool()>;
		// Writes a simple yes/no piece of session state the engine owns,
		// the setter counterpart to BoolQueryCallback above - for
		// self.world:setOperatingCatapult(bool).
		using BoolSetCallback = std::function<void(bool)>;
		// (fromPosition, direction, speed, hitTag) - see self.world:
		// fireGravityProjectile. Direction need not be normalized. Separate
		// from ProjectileSpawnCallback (which stays straight-line/no-
		// gravity, aimed at a TO point rather than a direction) so existing
		// straight-line callers are never affected by this.
		using GravityProjectileSpawnCallback =
			std::function<void(const glm::vec3&, const glm::vec3&, float, const std::string&)>;

		ScriptRuntime() = default;
		~ScriptRuntime();

		ScriptRuntime(const ScriptRuntime&) = delete;
		ScriptRuntime& operator=(const ScriptRuntime&) = delete;

		void initialize(
			EditorScene& scene,
			AICommandBus& commandBus,
			InputSource& inputSource,
			LogCallback logCallback,
			ProjectileSpawnCallback projectileSpawnCallback,
			BoolQueryCallback heldItemQueryCallback,
			BoolQueryCallback aimingCatapultQueryCallback,
			BoolSetCallback operatingCatapultSetCallback,
			GravityProjectileSpawnCallback gravityProjectileSpawnCallback);
		void shutdown() noexcept;
		[[nodiscard]] bool isRunning() const noexcept;

		// Loads and runs `scriptPath` (relative to projectRoot), attaches the
		// entity/input proxies, and calls :on_start() once. Logs and returns
		// false on any Lua error; does not throw.
		bool startScript(int entityId, const std::string& scriptPath, const std::filesystem::path& projectRoot);

		// Calls :on_update(deltaTime) on every instance started for this entity.
		// An instance that errors is logged once and then stops being ticked.
		void updateEntity(int entityId, float deltaTime);

		// Stops and forgets the running instance of `scriptPath` on `entityId`
		// (if any), so it no longer ticks. Call this whenever a script is
		// detached while Play is active - detaching only removes it from
		// SceneEntity::scripts, which this class doesn't otherwise watch.
		void stopScript(int entityId, const std::string& scriptPath);

		// One `-- @property <name> <type> [default]` line of a script, shown
		// in the Inspector and editable per object (SceneEntity::
		// scriptProperties). Types:
		//   number 4.5 | bool true | string any text | vec3 1 2 3
		//   icon Game/Models/iconpack1/128/SwordT1.png   (an image path,
		//        Inspector shows a thumbnail + "Change Icon..." button)
		//   enum a|b|c b   (Inspector shows a dropdown of a/b/c, default b)
		//   image Game/Branding/logo.jpg   (any picture, e.g. a splash logo -
		//        "Change Image..." imports into the default's own folder)
		// The value (default or the object's own) is set on the script's
		// table as self.<name> BEFORE on_start() runs, so a script should
		// read it with e.g. `self.speed = self.speed or 4.5`.
		struct ExposedScriptProperty
		{
			enum class Type { Number, String, Bool, Vec3, Icon, Enum, Image, Slider };
			std::string name;
			Type type = Type::Number;
			float defaultNumber = 0.0F;
			std::string defaultString;
			bool defaultBool = false;
			glm::vec3 defaultVec3{0.0F};
			std::vector<std::string> options; // Enum only
			float sliderMin = 0.0F;           // Slider only: `slider <min>|<max> <default>`
			float sliderMax = 1.0F;

			// The default value in the same text form SceneEntity::
			// scriptProperties stores overrides in.
			[[nodiscard]] std::string defaultAsText() const;
		};

		[[nodiscard]] static std::vector<ExposedScriptProperty> parseScriptProperties(
			const std::filesystem::path& fullScriptPath);
		// parseScriptProperties, re-read only when the file's modification
		// time changes - cheap enough to call every frame from the Inspector.
		[[nodiscard]] static const std::vector<ExposedScriptProperty>& cachedScriptProperties(
			const std::filesystem::path& fullScriptPath);

		// A script's `-- @preset <Preset Name> | <role>` line - scripts that
		// belong together (e.g. the "FPS Demo" set). `role` says where the
		// script goes: "player" scripts all go on the player object, others
		// ("item", "damageable", "manager") on other objects. Empty name =
		// not part of a preset.
		struct ScriptPresetTag
		{
			std::string name;
			std::string role;
		};
		[[nodiscard]] static ScriptPresetTag cachedScriptPreset(const std::filesystem::path& fullScriptPath);

		// The value `entity` has for a script's @property, as text: its own
		// override if it has one, else the script file's annotated default
		// (empty if the script or property doesn't exist). For reading
		// settings without running the script - e.g. GameForgerRuntime reads
		// the Game Manager's splash logo before any script starts.
		[[nodiscard]] static std::string scriptPropertyText(const SceneEntity& entity, const std::string& scriptPath,
			const std::string& propertyName, const std::filesystem::path& projectRoot);

		// Registry refs of every running instance on entityId (Lua bindings
		// use this to call functions on them).
		[[nodiscard]] std::vector<int> instanceRefs(int entityId) const;

		// Session state (inventory, look angles, effects) shared with the
		// host's gameplay loop - backs self.inventory, self.camera:getPitch/
		// getAim and self.world:spawnBeam/spawnFlash. Optional: when null
		// those calls are harmless no-ops (e.g. unit tests). initialize()
		// clears it, so set it after initialize().
		void setGameplayState(GameplayState* gameplayState) noexcept;
		[[nodiscard]] GameplayState* gameplayState() const noexcept;

		// Calls `functionName(self, number, text)` on every running script
		// instance of entityId that defines it (e.g. on_damage(amount,
		// sourceName), on_stun(seconds, sourceName)). Returns true if at
		// least one instance handled it. Errors are logged, not thrown.
		bool invokeHook(int entityId, const char* functionName, float number, const std::string& text);
		[[nodiscard]] bool hasHook(int entityId, const char* functionName) const;

		// Reads a numeric field directly off a running script instance's own
		// table (e.g. `self.pickup_range = 4.0` set in on_start()) - lets a
		// script expose tunable config the engine reads fresh every frame
		// (see inventory_system.lua), without a dedicated Lua API for every
		// such value. Returns defaultValue if entityId has no running
		// instance of scriptPath, or the field isn't a number.
		[[nodiscard]] float getScriptNumberField(
			int entityId, const std::string& scriptPath, const std::string& fieldName, float defaultValue) const;
		void setScriptNumberField(
			int entityId, const std::string& scriptPath, const std::string& fieldName, float value);

		[[nodiscard]] std::string getScriptStringField(
			int entityId, const std::string& scriptPath, const std::string& fieldName, const std::string& defaultValue) const;
		void setScriptStringField(
			int entityId, const std::string& scriptPath, const std::string& fieldName, const std::string& value);

		[[nodiscard]] bool getScriptBoolField(
			int entityId, const std::string& scriptPath, const std::string& fieldName, bool defaultValue) const;
		void setScriptBoolField(
			int entityId, const std::string& scriptPath, const std::string& fieldName, bool value);

		// The most recent entity/mode passed to self.camera:setMode(...), if
		// any. Read by the Game view panel each frame during Play to decide
		// whose eyes (or over-the-shoulder view) to render from instead of the
		// default fixed preview camera.
		[[nodiscard]] bool hasActiveCamera() const noexcept;
		[[nodiscard]] int activeCameraEntityId() const noexcept;
		[[nodiscard]] const std::string& activeCameraMode() const noexcept;
		void setActiveCamera(int entityId, std::string mode);

		// Used internally by the Lua C bindings; public because they're plain
		// C functions, not members.
		[[nodiscard]] EditorScene& scene() const noexcept;
		[[nodiscard]] AICommandBus& commandBus() const noexcept;
		[[nodiscard]] InputSource& inputSource() const noexcept;
		void log(bool isError, const std::string& message) const;
		void spawnProjectile(
			const glm::vec3& fromPosition, const glm::vec3& toPosition, float speed, const std::string& hitTag) const;
		void spawnGravityProjectile(
			const glm::vec3& fromPosition, const glm::vec3& direction, float speed, const std::string& hitTag) const;
		[[nodiscard]] bool isHoldingItem() const;
		[[nodiscard]] bool isAimingCatapult() const;
		void setOperatingCatapult(bool value) const;

	private:
		struct ScriptInstance
		{
			std::string scriptPath;
			int ref = 0;
		};

		lua_State* state_ = nullptr;
		EditorScene* scene_ = nullptr;
		AICommandBus* commandBus_ = nullptr;
		InputSource* inputSource_ = nullptr;
		LogCallback logCallback_;
		ProjectileSpawnCallback projectileSpawnCallback_;
		BoolQueryCallback heldItemQueryCallback_;
		BoolQueryCallback aimingCatapultQueryCallback_;
		BoolSetCallback operatingCatapultSetCallback_;
		GravityProjectileSpawnCallback gravityProjectileSpawnCallback_;
		std::unordered_map<int, std::vector<ScriptInstance>> instancesByEntity_;
		GameplayState* gameplayState_ = nullptr;
		int activeCameraEntityId_ = -1;
		std::string activeCameraMode_ = "fps";
		// Per-call instruction budget consumed by instructionHook(). Reset
		// before each lua_pcall; when it hits zero the hook raises a Lua
		// error that aborts the pcall with LUA_ERRRUN. Stops a `while true`
		// in on_update from freezing the editor indefinitely.
		std::atomic<std::int64_t> instructionsRemaining_{0};

		static void instructionHook(lua_State* L, lua_Debug* ar);
		void installBudgetHook(lua_State* L);
		void resetBudget();
	};
}
