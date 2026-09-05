#include "GameForger/Editor/ScriptRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include "GameForger/Core/ProjectPaths.hpp"
#include "GameForger/Editor/Collision.hpp"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <atomic>

namespace gameforger::editor
{
	namespace
	{
		// Default per-call instruction budget for a single Lua invocation
		// (startScript / on_start / on_update). Picked to be wide enough for
		// real game logic but tight enough that a `while true do end` aborts
		// in well under a second on modern hardware. Hook granularity (the
		// `count` argument to lua_sethook) is the divisor for overhead.
		constexpr std::int64_t kInstructionBudget = 5'000'000;
		constexpr int kHookCountInterval = 1024;

		ScriptRuntime* runtimeFrom(lua_State* L)
		{
			return *static_cast<ScriptRuntime**>(lua_getextraspace(L));
		}

		void executeOrLog(ScriptRuntime* runtime, const AIEditorCommand& command)
		{
			const AICommandResult result = runtime->commandBus().execute(command);
			if (!result.success)
			{
				runtime->log(true, "Script command failed: " + result.message);
			}
		}
	}

	void ScriptRuntime::instructionHook(lua_State* L, lua_Debug* ar)
	{
		(void)ar;
		ScriptRuntime* runtime = runtimeFrom(L);
		if (runtime == nullptr)
		{
			return;
		}
		// Subtract one batch of kHookCountInterval VM instructions per
		// hook invocation. Doing this as a relaxed load/subtract/store is
		// fine because the runtime is single-threaded (the editor pumps
		// one frame at a time on the main thread; the Runtime does the same).
		const std::int64_t remaining =
			runtime->instructionsRemaining_.fetch_sub(kHookCountInterval) - kHookCountInterval;
		if (remaining <= 0)
		{
			// luaL_error longjmp's through the current pcall, surfacing as
			// LUA_ERRRUN at the lua_pcall call site. The script is stopped
			// by the caller the same way any other runtime error would.
			luaL_error(L, "Script exceeded its %lld-instruction budget.",
				static_cast<long long>(kInstructionBudget));
		}
	}

	void ScriptRuntime::installBudgetHook(lua_State* L)
	{
		instructionsRemaining_.store(kInstructionBudget);
		lua_sethook(L, instructionHook, LUA_MASKCOUNT, kHookCountInterval);
	}

	void ScriptRuntime::resetBudget()
	{
		instructionsRemaining_.store(kInstructionBudget);
	}

	void* ScriptRuntime::luaAlloc(void* userData, void* pointer, std::size_t oldSize, std::size_t newSize)
	{
		auto* runtime = static_cast<ScriptRuntime*>(userData);
		if (runtime == nullptr)
		{
			if (newSize == 0)
			{
				std::free(pointer);
				return nullptr;
			}
			return std::realloc(pointer, newSize);
		}
		if (newSize == 0)
		{
			if (pointer != nullptr)
			{
				const std::size_t used = runtime->luaBytesUsed_.load();
				runtime->luaBytesUsed_.store(used >= oldSize ? used - oldSize : 0);
				std::free(pointer);
			}
			return nullptr;
		}
		const std::size_t accountedOld = pointer != nullptr ? oldSize : 0;
		const std::size_t used = runtime->luaBytesUsed_.load();
		const std::size_t next = used - accountedOld + newSize;
		if (next > kLuaMemoryBudgetBytes)
		{
			return nullptr;
		}
		void* resized = std::realloc(pointer, newSize);
		if (resized != nullptr)
		{
			runtime->luaBytesUsed_.store(next);
		}
		return resized;
	}

	namespace
	{

		void pushVec3(lua_State* L, const glm::vec3& value)
		{
			lua_newtable(L);
			lua_pushnumber(L, static_cast<lua_Number>(value.x));
			lua_setfield(L, -2, "x");
			lua_pushnumber(L, static_cast<lua_Number>(value.y));
			lua_setfield(L, -2, "y");
			lua_pushnumber(L, static_cast<lua_Number>(value.z));
			lua_setfield(L, -2, "z");
		}

		glm::vec3 checkVec3(lua_State* L, const int index)
		{
			luaL_checktype(L, index, LUA_TTABLE);
			glm::vec3 value(0.0F);
			lua_getfield(L, index, "x");
			value.x = static_cast<float>(luaL_optnumber(L, -1, 0.0));
			lua_pop(L, 1);
			lua_getfield(L, index, "y");
			value.y = static_cast<float>(luaL_optnumber(L, -1, 0.0));
			lua_pop(L, 1);
			lua_getfield(L, index, "z");
			value.z = static_cast<float>(luaL_optnumber(L, -1, 0.0));
			lua_pop(L, 1);
			return value;
		}

		int entityIdFromUpvalue(lua_State* L)
		{
			return static_cast<int>(lua_tointeger(L, lua_upvalueindex(1)));
		}

		int luaEntityGetPosition(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const SceneEntity* entity = runtime->scene().findEntity(entityIdFromUpvalue(L));
			pushVec3(L, entity != nullptr ? entity->position : glm::vec3(0.0F));
			return 1;
		}

		int luaEntitySetPosition(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const glm::vec3 value = checkVec3(L, 2);
			const int entityId = entityIdFromUpvalue(L);
			if (const SceneEntity* entity = runtime->scene().findEntity(entityId))
			{
				executeOrLog(runtime, SetPropertyCommand{entity->name, "Transform", "position", value});
			}
			else
			{
				// Entity was deleted while Play was running - this is a normal
				// outcome, not an error, but the previous silent no-op
				// confused script authors. Note it once at debug level so
				// they can see the script is still trying to drive a
				// missing entity.
				runtime->log(false, "entity:setPosition ignored: entity " + std::to_string(entityId) + " no longer exists.");
			}
			return 0;
		}

		int luaEntityGetRotation(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const SceneEntity* entity = runtime->scene().findEntity(entityIdFromUpvalue(L));
			pushVec3(L, entity != nullptr ? entity->rotationEuler : glm::vec3(0.0F));
			return 1;
		}

		int luaEntitySetRotation(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const glm::vec3 value = checkVec3(L, 2);
			const int entityId = entityIdFromUpvalue(L);
			if (const SceneEntity* entity = runtime->scene().findEntity(entityId))
			{
				executeOrLog(runtime, SetPropertyCommand{entity->name, "Transform", "rotation", value});
			}
			else
			{
				runtime->log(false, "entity:setRotation ignored: entity " + std::to_string(entityId) + " no longer exists.");
			}
			return 0;
		}

		int luaEntityGetScale(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const SceneEntity* entity = runtime->scene().findEntity(entityIdFromUpvalue(L));
			pushVec3(L, entity != nullptr ? entity->scale : glm::vec3(1.0F));
			return 1;
		}

		int luaEntityGetForward(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const SceneEntity* entity = runtime->scene().findEntity(entityIdFromUpvalue(L));
			const float yawRadians = glm::radians(entity != nullptr ? entity->rotationEuler.y : 0.0F);
			pushVec3(L, glm::vec3(std::sin(yawRadians), 0.0F, std::cos(yawRadians)));
			return 1;
		}

		int luaEntityGetRight(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const SceneEntity* entity = runtime->scene().findEntity(entityIdFromUpvalue(L));
			const float yawRadians = glm::radians(entity != nullptr ? entity->rotationEuler.y : 0.0F);
			// DO NOT CHANGE to cross(+Y, forward). That is world +X at yaw 0
			// but GLM lookAtRH screen-right is cross(forward, +Y) = -X, which
			// fps_controller.lua uses for D. Locked in by testGetRightMatchesFpsCamera.
			const glm::vec3 forward(std::sin(yawRadians), 0.0F, std::cos(yawRadians));
			pushVec3(L, glm::normalize(glm::cross(forward, glm::vec3(0.0F, 1.0F, 0.0F))));
			return 1;
		}

		void pushEntityProxy(lua_State* L, const int entityId)
		{
			lua_newtable(L);

			const auto addMethod = [L, entityId](const char* name, lua_CFunction function)
			{
				lua_pushinteger(L, entityId);
				lua_pushcclosure(L, function, 1);
				lua_setfield(L, -2, name);
			};
			addMethod("getPosition", luaEntityGetPosition);
			addMethod("setPosition", luaEntitySetPosition);
			addMethod("getRotation", luaEntityGetRotation);
			addMethod("setRotation", luaEntitySetRotation);
			addMethod("getScale", luaEntityGetScale);
			addMethod("getForward", luaEntityGetForward);
			addMethod("getRight", luaEntityGetRight);
		}

		int luaCameraSetMode(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const std::string mode = luaL_checkstring(L, 2);
			if (mode != "fps" && mode != "third_person")
			{
				return luaL_error(L, "camera:setMode expects \"fps\" or \"third_person\", got \"%s\"", mode.c_str());
			}
			runtime->setActiveCamera(entityIdFromUpvalue(L), mode);
			return 0;
		}

		int luaCameraGetMode(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const bool isActive =
				runtime->hasActiveCamera() && runtime->activeCameraEntityId() == entityIdFromUpvalue(L);
			lua_pushstring(L, isActive ? runtime->activeCameraMode().c_str() : "fps");
			return 1;
		}

		void pushCameraProxy(lua_State* L, const int entityId)
		{
			lua_newtable(L);

			const auto addMethod = [L, entityId](const char* name, lua_CFunction function)
			{
				lua_pushinteger(L, entityId);
				lua_pushcclosure(L, function, 1);
				lua_setfield(L, -2, name);
			};
			addMethod("setMode", luaCameraSetMode);
			addMethod("getMode", luaCameraGetMode);
		}

		int luaPhysicsResolve(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const glm::vec3 position = checkVec3(L, 2);
			const float halfWidth = static_cast<float>(luaL_checknumber(L, 3));
			const float height = static_cast<float>(luaL_checknumber(L, 4));
			const BoxCollisionResult result = resolveBoxCollision(
				runtime->scene(), entityIdFromUpvalue(L), position, halfWidth, height);
			pushVec3(L, result.position);
			lua_pushboolean(L, result.grounded);
			return 2;
		}

		void pushPhysicsProxy(lua_State* L, const int entityId)
		{
			lua_newtable(L);

			const auto addMethod = [L, entityId](const char* name, lua_CFunction function)
			{
				lua_pushinteger(L, entityId);
				lua_pushcclosure(L, function, 1);
				lua_setfield(L, -2, name);
			};
			addMethod("resolve", luaPhysicsResolve);
		}

		// Nearest OTHER entity carrying `tag` anywhere in its tags list (not
		// just primaryTag) - used by enemy_ai.lua (find the player) and
		// ranged_attacker.lua (find something to shoot at). Returns
		// position, distance, name - or three nils if nothing matches, so a
		// script can just check `if position then ... end`.
		int luaWorldFindNearestWithTag(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const std::string tag = luaL_checkstring(L, 2);
			const int selfEntityId = entityIdFromUpvalue(L);
			const SceneEntity* self = runtime->scene().findEntity(selfEntityId);
			const glm::vec3 origin = self != nullptr ? self->position : glm::vec3(0.0F);

			const SceneEntity* best = nullptr;
			float bestDistanceSquared = 0.0F;
			for (const SceneEntity& other : runtime->scene().entities())
			{
				if (other.id == selfEntityId ||
					std::find(other.tags.begin(), other.tags.end(), tag) == other.tags.end())
				{
					continue;
				}
				const glm::vec3 delta = other.position - origin;
				const float distanceSquared = glm::dot(delta, delta);
				if (best == nullptr || distanceSquared < bestDistanceSquared)
				{
					best = &other;
					bestDistanceSquared = distanceSquared;
				}
			}
			if (best == nullptr)
			{
				lua_pushnil(L);
				lua_pushnil(L);
				lua_pushnil(L);
				return 3;
			}
			pushVec3(L, best->position);
			lua_pushnumber(L, static_cast<lua_Number>(std::sqrt(bestDistanceSquared)));
			lua_pushstring(L, best->name.c_str());
			return 3;
		}

		// Position of the first entity carrying `tag` (any entity, not
		// excluding self) - or nil. Used to find a named spawn/muzzle point
		// (e.g. tag "player_gun_muzzle") without a dedicated component -
		// this project's existing free-text tag system already covers it.
		int luaWorldFindPositionByTag(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const std::string tag = luaL_checkstring(L, 2);
			for (const SceneEntity& other : runtime->scene().entities())
			{
				if (std::find(other.tags.begin(), other.tags.end(), tag) != other.tags.end())
				{
					pushVec3(L, other.position);
					return 1;
				}
			}
			lua_pushnil(L);
			return 1;
		}

		// Spawns an engine-managed projectile (see PlayModeState::Projectile,
		// tickProjectiles() in main.cpp) traveling in a straight line from
		// `fromPosition` toward `toPosition` at `speed` world units/second,
		// despawning on reaching an entity tagged `hitTag` or after a fixed
		// lifetime - not a scripted entity of its own, so this takes no
		// further arguments (no per-projectile behavior to configure).
		int luaWorldFireProjectile(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const glm::vec3 from = checkVec3(L, 2);
			const glm::vec3 to = checkVec3(L, 3);
			const float speed = static_cast<float>(luaL_checknumber(L, 4));
			const std::string hitTag = luaL_checkstring(L, 5);
			runtime->spawnProjectile(from, to, speed, hitTag);
			return 0;
		}

		// True while the player is currently carrying a held weapon prop (F
		// key, main.cpp) - read by fps_controller.lua/third_person_
		// controller.lua to cap movement to walk speed regardless of
		// sprint. Answered by ScriptRuntime::isHoldingItem(), which just
		// forwards to whatever GameplayState-backed callback the engine
		// wired at initialize() - ScriptRuntime itself has no notion of
		// "held item."
		int luaWorldIsHoldingItem(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			lua_pushboolean(L, runtime->isHoldingItem() ? 1 : 0);
			return 1;
		}

		// True while the player is currently operating a catapult
		// (catapult_controller.lua calls setOperatingCatapult below when it
		// enters/leaves aim mode) - read by the controller scripts to
		// freeze WASD movement while it's in use. Same callback-forwarding
		// shape as isHoldingItem above.
		int luaWorldIsAimingCatapult(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			lua_pushboolean(L, runtime->isAimingCatapult() ? 1 : 0);
			return 1;
		}

		// Companion setter for isAimingCatapult above - catapult_controller.
		// lua calls this true on entering aim mode, false on firing/
		// cancelling. Forwards to whatever GameplayState-backed callback
		// the engine wired at initialize(), same pattern as every other
		// self.world function that touches session state it doesn't own.
		int luaWorldSetOperatingCatapult(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			luaL_checktype(L, 2, LUA_TBOOLEAN);
			runtime->setOperatingCatapult(lua_toboolean(L, 2) != 0);
			return 0;
		}

		// Rotates a DIFFERENT named entity, not the calling script's own
		// self.entity - needed because a catapult's throwing arm has to
		// swing independently of its stationary base+wheels, and a script
		// can otherwise only ever touch the one entity it's attached to
		// (self.entity is closure-bound to that id, see pushEntityProxy).
		// If the target entity is parented (has a non-empty parentName),
		// writes its LOCAL rotation instead of world rotation - identical
		// reasoning to main.cpp's setEntityOwnRotation helper this mirrors:
		// applyParentConstraints (GameplayLoop.cpp) recomputes a parented
		// entity's WORLD rotation from its own local* fields every frame
		// unconditionally, so writing world rotation on a parented entity
		// would just get silently overwritten the very next frame.
		int luaWorldSetEntityRotation(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const std::string entityName = luaL_checkstring(L, 2);
			const glm::vec3 rotation = checkVec3(L, 3);
			const SceneEntity* entity = runtime->scene().findEntity(entityName);
			if (entity == nullptr)
			{
				runtime->log(false, "world:setEntityRotation ignored: entity \"" + entityName + "\" not found.");
				return 0;
			}
			if (entity->parentName.empty())
			{
				executeOrLog(runtime, SetPropertyCommand{entity->name, "Transform", "rotation", rotation});
			}
			else
			{
				executeOrLog(runtime, SetPropertyCommand{entity->name, "Parent", "localRotation", rotation});
			}
			return 0;
		}

		// Spawns a gravity-affected projectile (see GameplayState::
		// Projectile::useGravity, tickProjectiles() in GameplayLoop.cpp) -
		// `direction` need not be normalized, only its direction is used.
		// Separate from fireProjectile above (which stays straight-line,
		// unchanged, still used by enemy_ai.lua/ranged_attacker.lua) rather
		// than overloading its signature, since "aim toward a point" and
		// "launch along a direction at a speed" are genuinely different
		// shapes of call.
		int luaWorldFireGravityProjectile(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const glm::vec3 from = checkVec3(L, 2);
			const glm::vec3 direction = checkVec3(L, 3);
			const float speed = static_cast<float>(luaL_checknumber(L, 4));
			const std::string hitTag = luaL_checkstring(L, 5);
			runtime->spawnGravityProjectile(from, direction, speed, hitTag);
			return 0;
		}

		void pushWorldProxy(lua_State* L, const int entityId)
		{
			lua_newtable(L);

			const auto addMethod = [L, entityId](const char* name, lua_CFunction function)
			{
				lua_pushinteger(L, entityId);
				lua_pushcclosure(L, function, 1);
				lua_setfield(L, -2, name);
			};
			addMethod("findNearestWithTag", luaWorldFindNearestWithTag);
			addMethod("findPositionByTag", luaWorldFindPositionByTag);
			addMethod("fireProjectile", luaWorldFireProjectile);
			addMethod("isHoldingItem", luaWorldIsHoldingItem);
			addMethod("isAimingCatapult", luaWorldIsAimingCatapult);
			addMethod("setOperatingCatapult", luaWorldSetOperatingCatapult);
			addMethod("setEntityRotation", luaWorldSetEntityRotation);
			addMethod("fireGravityProjectile", luaWorldFireGravityProjectile);
		}

		// Answered by whichever InputSource the host (Editor or Runtime)
		// passed to ScriptRuntime::initialize() - see InputSource.hpp. This
		// keeps ScriptRuntime itself agnostic to whether keyboard state
		// comes from ImGui (Editor, no active ImGui context in Runtime) or
		// raw GLFW (Runtime, no ImGui context at all).
		int luaInputIsKeyDown(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			lua_pushboolean(L, runtime->inputSource().isKeyDown(luaL_checkstring(L, 2)));
			return 1;
		}

		int luaInputIsKeyPressed(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			lua_pushboolean(L, runtime->inputSource().isKeyPressed(luaL_checkstring(L, 2)));
			return 1;
		}

		int luaInputGetAxis(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const std::string positiveName = luaL_checkstring(L, 2);
			const std::string negativeName = luaL_checkstring(L, 3);
			lua_pushnumber(L, static_cast<lua_Number>(runtime->inputSource().getAxis(positiveName, negativeName)));
			return 1;
		}

		// Mouse movement since last frame, in pixels - for continuous aiming
		// (catapult_controller.lua) rather than the discrete key checks
		// above. See InputSource::getMouseDeltaX/Y's own doc comment.
		int luaInputGetMouseDeltaX(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			lua_pushnumber(L, static_cast<lua_Number>(runtime->inputSource().getMouseDeltaX()));
			return 1;
		}

		int luaInputGetMouseDeltaY(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			lua_pushnumber(L, static_cast<lua_Number>(runtime->inputSource().getMouseDeltaY()));
			return 1;
		}

		int luaPrint(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const int argCount = lua_gettop(L);
			std::string message;
			for (int index = 1; index <= argCount; ++index)
			{
				if (index > 1)
				{
					message += '\t';
				}
				message += luaL_tolstring(L, index, nullptr);
				lua_pop(L, 1);
			}
			if (runtime != nullptr)
			{
				runtime->log(false, message);
			}
			return 0;
		}
	}

	ScriptRuntime::~ScriptRuntime()
	{
		shutdown();
	}

	void ScriptRuntime::initialize(
		EditorScene& scene, AICommandBus& commandBus, InputSource& inputSource, LogCallback logCallback,
		ProjectileSpawnCallback projectileSpawnCallback, BoolQueryCallback heldItemQueryCallback,
		BoolQueryCallback aimingCatapultQueryCallback, BoolSetCallback operatingCatapultSetCallback,
		GravityProjectileSpawnCallback gravityProjectileSpawnCallback)
	{
		shutdown();

		luaBytesUsed_.store(0);
		state_ = lua_newstate(&ScriptRuntime::luaAlloc, this);
		if (state_ == nullptr)
		{
			return;
		}
		scene_ = &scene;
		commandBus_ = &commandBus;
		inputSource_ = &inputSource;
		logCallback_ = std::move(logCallback);
		projectileSpawnCallback_ = std::move(projectileSpawnCallback);
		heldItemQueryCallback_ = std::move(heldItemQueryCallback);
		aimingCatapultQueryCallback_ = std::move(aimingCatapultQueryCallback);
		operatingCatapultSetCallback_ = std::move(operatingCatapultSetCallback);
		gravityProjectileSpawnCallback_ = std::move(gravityProjectileSpawnCallback);
		*static_cast<ScriptRuntime**>(lua_getextraspace(state_)) = this;

		luaL_requiref(state_, LUA_GNAME, luaopen_base, 1);
		lua_pop(state_, 1);
		luaL_requiref(state_, LUA_TABLIBNAME, luaopen_table, 1);
		lua_pop(state_, 1);
		luaL_requiref(state_, LUA_STRLIBNAME, luaopen_string, 1);
		lua_pop(state_, 1);
		luaL_requiref(state_, LUA_MATHLIBNAME, luaopen_math, 1);
		lua_pop(state_, 1);

		// The base library (just required above) still defines dofile/loadfile/load/collectgarbage,
		// which read/execute arbitrary code/files - remove them to strictly confine scripts.
		lua_pushnil(state_);
		lua_setglobal(state_, "dofile");
		lua_pushnil(state_);
		lua_setglobal(state_, "loadfile");
		lua_pushnil(state_);
		lua_setglobal(state_, "load");
		lua_pushnil(state_);
		lua_setglobal(state_, "collectgarbage");

		lua_pushcfunction(state_, luaPrint);
		lua_setglobal(state_, "print");

		// Install the instruction-count hook once; per-call resets happen at
		// the lua_pcall call sites (startScript, updateEntity, on_start).
		// The hook itself reads the per-call counter out of this runtime.
		installBudgetHook(state_);

		lua_newtable(state_);
		lua_pushcfunction(state_, luaInputIsKeyDown);
		lua_setfield(state_, -2, "isKeyDown");
		lua_pushcfunction(state_, luaInputIsKeyPressed);
		lua_setfield(state_, -2, "isKeyPressed");
		lua_pushcfunction(state_, luaInputGetAxis);
		lua_setfield(state_, -2, "getAxis");
		lua_pushcfunction(state_, luaInputGetMouseDeltaX);
		lua_setfield(state_, -2, "getMouseDeltaX");
		lua_pushcfunction(state_, luaInputGetMouseDeltaY);
		lua_setfield(state_, -2, "getMouseDeltaY");
		lua_setglobal(state_, "input");
	}

	void ScriptRuntime::shutdown() noexcept
	{
		if (state_ != nullptr)
		{
			lua_close(state_);
			state_ = nullptr;
		}
		luaBytesUsed_.store(0);
		instancesByEntity_.clear();
		activeCameraEntityId_ = -1;
		activeCameraMode_ = "fps";
		scene_ = nullptr;
		commandBus_ = nullptr;
		inputSource_ = nullptr;
		logCallback_ = nullptr;
		projectileSpawnCallback_ = nullptr;
		heldItemQueryCallback_ = nullptr;
		aimingCatapultQueryCallback_ = nullptr;
		operatingCatapultSetCallback_ = nullptr;
		gravityProjectileSpawnCallback_ = nullptr;
	}

	bool ScriptRuntime::isRunning() const noexcept
	{
		return state_ != nullptr;
	}

	bool ScriptRuntime::startScript(
		const int entityId, const std::string& scriptPath, const std::filesystem::path& projectRoot)
	{
		if (state_ == nullptr)
		{
			return false;
		}

		// Re-validate here, not just at attach time: this is the one choke
		// point every script execution goes through, whether it arrived via
		// AttachScriptCommand (already validated) or via a hand-edited/
		// untrusted scene file loaded straight into SceneEntity::scripts with
		// no validation at all (SceneSerializer doesn't check script paths on
		// load - see EditorScene's AttachScriptCommand handler for the
		// equivalent check on the authoring side).
		const std::optional<std::filesystem::path> fullPath =
			core::resolveProjectFile(projectRoot, scriptPath, "Game/Scripts", {".lua"});
		if (!fullPath.has_value())
		{
			log(true, "Refusing to run " + scriptPath + ": not a .lua file inside Game/Scripts.");
			return false;
		}
		if (luaL_loadfile(state_, fullPath->string().c_str()) != LUA_OK)
		{
			log(true, "Failed to load " + scriptPath + ": " + lua_tostring(state_, -1));
			lua_pop(state_, 1);
			return false;
		}

		// Per-script environment isolation (T1-6):
		// Bind a dedicated _ENV table with __index = _G so non-local assignments
		// do not pollute the global table or leak into other entity scripts.
		lua_newtable(state_);
		lua_newtable(state_);
		lua_pushglobaltable(state_);
		lua_setfield(state_, -2, "__index");
		lua_setmetatable(state_, -2);
		lua_setupvalue(state_, -2, 1);

		resetBudget();
		if (lua_pcall(state_, 0, 1, 0) != LUA_OK)
		{
			log(true, "Error running " + scriptPath + ": " + lua_tostring(state_, -1));
			lua_pop(state_, 1);
			return false;
		}
		if (!lua_istable(state_, -1))
		{
			log(true, scriptPath + " did not return a table (expected e.g. `return MyController`).");
			lua_pop(state_, 1);
			return false;
		}

		pushEntityProxy(state_, entityId);
		lua_setfield(state_, -2, "entity");
		lua_getglobal(state_, "input");
		lua_setfield(state_, -2, "input");
		pushCameraProxy(state_, entityId);
		lua_setfield(state_, -2, "camera");
		pushPhysicsProxy(state_, entityId);
		lua_setfield(state_, -2, "physics");
		pushWorldProxy(state_, entityId);
		lua_setfield(state_, -2, "world");

		lua_getfield(state_, -1, "on_start");
		if (lua_isfunction(state_, -1))
		{
			lua_pushvalue(state_, -2);
			resetBudget();
			if (lua_pcall(state_, 1, 0, 0) != LUA_OK)
			{
				log(true, "Error in " + scriptPath + ":on_start(): " + lua_tostring(state_, -1));
				lua_pop(state_, 1);
				lua_pop(state_, 1);
				return false;
			}
		}
		else
		{
			lua_pop(state_, 1);
		}

		const int ref = luaL_ref(state_, LUA_REGISTRYINDEX);
		// Defend against double-registration: if a caller (or a retry path,
		// or a hand-edited scene loaded straight into SceneEntity::scripts)
		// invokes startScript a second time for the same entity+scriptPath,
		// both `on_update` loops would run in parallel and double-mutate
		// the entity each frame. Stop any existing instance for this
		// (entity, scriptPath) pair first, so a duplicate call replaces
		// rather than stacks.
		std::vector<ScriptInstance>& bucket = instancesByEntity_[entityId];
		for (auto it = bucket.begin(); it != bucket.end();)
		{
			if (it->scriptPath == scriptPath)
			{
				luaL_unref(state_, LUA_REGISTRYINDEX, it->ref);
				it = bucket.erase(it);
			}
			else
			{
				++it;
			}
		}
		bucket.push_back(ScriptInstance{scriptPath, ref});
		log(false, "Started " + scriptPath + " on entity " + std::to_string(entityId) + ".");
		return true;
	}

	void ScriptRuntime::updateEntity(const int entityId, const float deltaTime)
	{
		if (state_ == nullptr)
		{
			return;
		}
		const auto found = instancesByEntity_.find(entityId);
		if (found == instancesByEntity_.end())
		{
			return;
		}

		std::vector<ScriptInstance>& instances = found->second;
		std::vector<ScriptInstance> stillRunning;
		stillRunning.reserve(instances.size());
		for (const ScriptInstance& instance : instances)
		{
			lua_rawgeti(state_, LUA_REGISTRYINDEX, instance.ref);
			lua_getfield(state_, -1, "on_update");
			if (!lua_isfunction(state_, -1))
			{
				lua_pop(state_, 2);
				stillRunning.push_back(instance);
				continue;
			}
			lua_pushvalue(state_, -2);
			lua_pushnumber(state_, static_cast<lua_Number>(deltaTime));
			resetBudget();
			if (lua_pcall(state_, 2, 0, 0) != LUA_OK)
			{
				log(true, "Error in on_update(), script stopped: " + std::string(lua_tostring(state_, -1)));
				lua_pop(state_, 1);
				lua_pop(state_, 1);
				luaL_unref(state_, LUA_REGISTRYINDEX, instance.ref);
				continue;
			}
			lua_pop(state_, 1);
			stillRunning.push_back(instance);
		}
		instances = std::move(stillRunning);
	}

	void ScriptRuntime::stopScript(const int entityId, const std::string& scriptPath)
	{
		if (state_ == nullptr)
		{
			return;
		}
		const auto found = instancesByEntity_.find(entityId);
		if (found == instancesByEntity_.end())
		{
			return;
		}

		std::vector<ScriptInstance>& instances = found->second;
		std::vector<ScriptInstance> remaining;
		remaining.reserve(instances.size());
		for (const ScriptInstance& instance : instances)
		{
			if (instance.scriptPath == scriptPath)
			{
				luaL_unref(state_, LUA_REGISTRYINDEX, instance.ref);
				log(false, "Stopped " + scriptPath + " on entity " + std::to_string(entityId) + ".");
			}
			else
			{
				remaining.push_back(instance);
			}
		}
		instances = std::move(remaining);
	}

	std::vector<ScriptRuntime::ExposedScriptProperty> ScriptRuntime::parseScriptProperties(
		const std::filesystem::path& fullScriptPath)
	{
		std::vector<ExposedScriptProperty> properties;
		std::ifstream file(fullScriptPath);
		if (!file)
		{
			return properties;
		}

		std::string line;
		while (std::getline(file, line))
		{
			const std::size_t tagPos = line.find("@property");
			if (tagPos == std::string::npos)
			{
				continue;
			}

			// Format: -- @property <name> <type> [default]
			std::istringstream iss(line.substr(tagPos + 9));
			std::string name;
			std::string typeStr;
			if (!(iss >> name >> typeStr))
			{
				continue;
			}

			ExposedScriptProperty prop;
			prop.name = name;
			if (typeStr == "number" || typeStr == "float")
			{
				prop.type = ExposedScriptProperty::Type::Number;
				iss >> prop.defaultNumber;
			}
			else if (typeStr == "bool" || typeStr == "boolean")
			{
				prop.type = ExposedScriptProperty::Type::Bool;
				std::string boolStr;
				if (iss >> boolStr)
				{
					prop.defaultBool = (boolStr == "true" || boolStr == "1");
				}
			}
			else if (typeStr == "string")
			{
				prop.type = ExposedScriptProperty::Type::String;
				std::string rest;
				std::getline(iss >> std::ws, rest);
				prop.defaultString = rest;
			}
			else if (typeStr == "vec3" || typeStr == "Vector3")
			{
				prop.type = ExposedScriptProperty::Type::Vec3;
				iss >> prop.defaultVec3.x >> prop.defaultVec3.y >> prop.defaultVec3.z;
			}
			properties.push_back(std::move(prop));
		}
		return properties;
	}

	float ScriptRuntime::getScriptNumberField(
		const int entityId, const std::string& scriptPath, const std::string& fieldName,
		const float defaultValue) const
	{
		if (state_ == nullptr)
		{
			return defaultValue;
		}
		const auto found = instancesByEntity_.find(entityId);
		if (found == instancesByEntity_.end())
		{
			return defaultValue;
		}
		for (const ScriptInstance& instance : found->second)
		{
			if (instance.scriptPath != scriptPath)
			{
				continue;
			}
			lua_rawgeti(state_, LUA_REGISTRYINDEX, instance.ref);
			lua_getfield(state_, -1, fieldName.c_str());
			const float result = lua_isnumber(state_, -1) ? static_cast<float>(lua_tonumber(state_, -1)) : defaultValue;
			lua_pop(state_, 2);
			return result;
		}
		return defaultValue;
	}

	void ScriptRuntime::setScriptNumberField(
		const int entityId, const std::string& scriptPath, const std::string& fieldName, const float value)
	{
		if (state_ == nullptr) return;
		const auto found = instancesByEntity_.find(entityId);
		if (found == instancesByEntity_.end()) return;
		for (const ScriptInstance& instance : found->second)
		{
			if (instance.scriptPath != scriptPath) continue;
			lua_rawgeti(state_, LUA_REGISTRYINDEX, instance.ref);
			lua_pushnumber(state_, static_cast<lua_Number>(value));
			lua_setfield(state_, -2, fieldName.c_str());
			lua_pop(state_, 1);
			return;
		}
	}

	std::string ScriptRuntime::getScriptStringField(
		const int entityId, const std::string& scriptPath, const std::string& fieldName, const std::string& defaultValue) const
	{
		if (state_ == nullptr) return defaultValue;
		const auto found = instancesByEntity_.find(entityId);
		if (found == instancesByEntity_.end()) return defaultValue;
		for (const ScriptInstance& instance : found->second)
		{
			if (instance.scriptPath != scriptPath) continue;
			lua_rawgeti(state_, LUA_REGISTRYINDEX, instance.ref);
			lua_getfield(state_, -1, fieldName.c_str());
			const std::string result = lua_isstring(state_, -1) ? lua_tostring(state_, -1) : defaultValue;
			lua_pop(state_, 2);
			return result;
		}
		return defaultValue;
	}

	void ScriptRuntime::setScriptStringField(
		const int entityId, const std::string& scriptPath, const std::string& fieldName, const std::string& value)
	{
		if (state_ == nullptr) return;
		const auto found = instancesByEntity_.find(entityId);
		if (found == instancesByEntity_.end()) return;
		for (const ScriptInstance& instance : found->second)
		{
			if (instance.scriptPath != scriptPath) continue;
			lua_rawgeti(state_, LUA_REGISTRYINDEX, instance.ref);
			lua_pushstring(state_, value.c_str());
			lua_setfield(state_, -2, fieldName.c_str());
			lua_pop(state_, 1);
			return;
		}
	}

	bool ScriptRuntime::getScriptBoolField(
		const int entityId, const std::string& scriptPath, const std::string& fieldName, const bool defaultValue) const
	{
		if (state_ == nullptr) return defaultValue;
		const auto found = instancesByEntity_.find(entityId);
		if (found == instancesByEntity_.end()) return defaultValue;
		for (const ScriptInstance& instance : found->second)
		{
			if (instance.scriptPath != scriptPath) continue;
			lua_rawgeti(state_, LUA_REGISTRYINDEX, instance.ref);
			lua_getfield(state_, -1, fieldName.c_str());
			const bool result = lua_isboolean(state_, -1) ? (lua_toboolean(state_, -1) != 0) : defaultValue;
			lua_pop(state_, 2);
			return result;
		}
		return defaultValue;
	}

	void ScriptRuntime::setScriptBoolField(
		const int entityId, const std::string& scriptPath, const std::string& fieldName, const bool value)
	{
		if (state_ == nullptr) return;
		const auto found = instancesByEntity_.find(entityId);
		if (found == instancesByEntity_.end()) return;
		for (const ScriptInstance& instance : found->second)
		{
			if (instance.scriptPath != scriptPath) continue;
			lua_rawgeti(state_, LUA_REGISTRYINDEX, instance.ref);
			lua_pushboolean(state_, value ? 1 : 0);
			lua_setfield(state_, -2, fieldName.c_str());
			lua_pop(state_, 1);
			return;
		}
	}

	bool ScriptRuntime::hasActiveCamera() const noexcept
	{
		return activeCameraEntityId_ != -1;
	}

	int ScriptRuntime::activeCameraEntityId() const noexcept
	{
		return activeCameraEntityId_;
	}

	const std::string& ScriptRuntime::activeCameraMode() const noexcept
	{
		return activeCameraMode_;
	}

	void ScriptRuntime::setActiveCamera(const int entityId, std::string mode)
	{
		activeCameraEntityId_ = entityId;
		activeCameraMode_ = std::move(mode);
	}

	EditorScene& ScriptRuntime::scene() const noexcept
	{
		return *scene_;
	}

	AICommandBus& ScriptRuntime::commandBus() const noexcept
	{
		return *commandBus_;
	}

	InputSource& ScriptRuntime::inputSource() const noexcept
	{
		return *inputSource_;
	}

	void ScriptRuntime::log(const bool isError, const std::string& message) const
	{
		if (logCallback_)
		{
			logCallback_(isError, message);
		}
	}

	void ScriptRuntime::spawnProjectile(
		const glm::vec3& fromPosition, const glm::vec3& toPosition, const float speed, const std::string& hitTag) const
	{
		if (projectileSpawnCallback_)
		{
			projectileSpawnCallback_(fromPosition, toPosition, speed, hitTag);
		}
	}

	void ScriptRuntime::spawnGravityProjectile(
		const glm::vec3& fromPosition, const glm::vec3& direction, const float speed, const std::string& hitTag) const
	{
		if (gravityProjectileSpawnCallback_)
		{
			gravityProjectileSpawnCallback_(fromPosition, direction, speed, hitTag);
		}
	}

	bool ScriptRuntime::isHoldingItem() const
	{
		return heldItemQueryCallback_ ? heldItemQueryCallback_() : false;
	}

	bool ScriptRuntime::isAimingCatapult() const
	{
		return aimingCatapultQueryCallback_ ? aimingCatapultQueryCallback_() : false;
	}

	void ScriptRuntime::setOperatingCatapult(const bool value) const
	{
		if (operatingCatapultSetCallback_)
		{
			operatingCatapultSetCallback_(value);
		}
	}
}
