#pragma once

#include <atomic>
#include <cstddef>
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
	// Embeds a single Lua 5.4 VM and runs attached entity scripts against it for
	// the duration of one Play session. Scripts follow this project's existing
	// convention (see Game/Scripts/player_controller.lua): a chunk that returns
	// a table with on_start()/on_update(delta_time) methods. Before on_start()
	// is called, `self.entity` (position/rotation get/set, getScale, getForward/
	// getRight), `self.input` (isKeyDown/isKeyPressed/getAxis), `self.camera`
	// (setMode/getMode, "fps" or "third_person"), and `self.physics`
	// (resolve(position, halfWidth, height) -> correctedPosition, grounded -
	// box vs Collider-enabled primitives, imported-mesh triangles, and
	// children of a Collider-enabled parent; see Collision.hpp),
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

		// Asks the host to lock or release the mouse cursor. Cursor ownership
		// moved off the per-entity Inspector checkbox and onto whichever
		// script declares itself the Game Manager - see game_manager.lua.
		using CursorLockSetCallback = std::function<void(bool)>;

		// (clipPath, volume, loop) for play; an empty clipPath means "stop
		// everything". Routed through the host for the same reason
		// ProjectileSpawnCallback is: ScriptRuntime never links AudioEngine.
		using AudioCommandCallback =
			std::function<void(const std::string&, float, bool)>;

		// Grouping what used to be nine positional parameters on initialize().
		// Adding a capability meant touching every call site and risking a
		// silent mis-ordering between two same-typed callbacks (there are
		// already two BoolQueryCallbacks next to each other); a struct with
		// named fields cannot be mis-ordered.
		struct Config
		{
			LogCallback logCallback;
			ProjectileSpawnCallback projectileSpawnCallback;
			BoolQueryCallback heldItemQueryCallback;
			BoolQueryCallback aimingCatapultQueryCallback;
			BoolSetCallback operatingCatapultSetCallback;
			GravityProjectileSpawnCallback gravityProjectileSpawnCallback;
			CursorLockSetCallback cursorLockSetCallback;
			AudioCommandCallback audioCommandCallback;
		};

		ScriptRuntime() = default;
		~ScriptRuntime();

		ScriptRuntime(const ScriptRuntime&) = delete;
		ScriptRuntime& operator=(const ScriptRuntime&) = delete;

		void initialize(
			EditorScene& scene,
			AICommandBus& commandBus,
			InputSource& inputSource,
			Config config);
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

		struct ExposedScriptProperty
		{
			enum class Type { Number, String, Bool, Vec3 };
			std::string name;
			Type type = Type::Number;
			float defaultNumber = 0.0F;
			std::string defaultString;
			bool defaultBool = false;
			glm::vec3 defaultVec3{0.0F};
		};

		// Cursor lock, driven by self.gameManager:setCursorLock(). Forwards to
		// the host through Config::cursorLockSetCallback.
		void setCursorLock(bool locked);

		// self.audio. A clipPath of "@master" sets master volume instead of
		// playing; an empty path in stopAudio means "stop everything".
		void playAudio(const std::string& clipPath, float volume, bool loop);
		void stopAudio();

		// Manager registry, driven by the self.managers proxy. registerManager
		// is idempotent; unregisterManager on an unknown name is a no-op.
		void registerManager(const std::string& name);
		void unregisterManager(const std::string& name);
		[[nodiscard]] bool hasManager(const std::string& name) const;
		[[nodiscard]] const std::vector<std::string>& listManagers() const noexcept { return registeredManagers_; }

		[[nodiscard]] static std::vector<ExposedScriptProperty> parseScriptProperties(
			const std::filesystem::path& fullScriptPath);

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
		Config config_;
		// Names registered via self.managers:register(). A controller
		// registers itself on start and is auto-unregistered on on_end, so
		// "is anything driving the player?" is answerable without the host
		// tracking script lifetimes itself.
		std::vector<std::string> registeredManagers_;
		std::unordered_map<int, std::vector<ScriptInstance>> instancesByEntity_;
		int activeCameraEntityId_ = -1;
		std::string activeCameraMode_ = "fps";
		// Per-call instruction budget consumed by instructionHook(). Reset
		// before each lua_pcall; when it hits zero the hook raises a Lua
		// error that aborts the pcall with LUA_ERRRUN. Stops a `while true`
		// in on_update from freezing the editor indefinitely.
		std::atomic<std::int64_t> instructionsRemaining_{0};
		std::atomic<std::size_t> luaBytesUsed_{0};
		static constexpr std::size_t kLuaMemoryBudgetBytes = 8 * 1024 * 1024;

		// on_end() dispatch - one instance, or every live instance during
		// shutdown. Private: lifecycle is the runtime's business, not a
		// caller's.
		void dispatchOnEnd(const ScriptInstance& instance);
		void dispatchOnEndForAll();

		static void instructionHook(lua_State* L, lua_Debug* ar);
		void installBudgetHook(lua_State* L);
		void resetBudget();
		static void* luaAlloc(void* userData, void* pointer, std::size_t oldSize, std::size_t newSize);
	};
}
