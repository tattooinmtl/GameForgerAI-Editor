#include "GameForger/Editor/ScriptRuntime.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <unordered_map>
#include <optional>
#include <sstream>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include "GameForger/Core/ProjectPaths.hpp"
#include "GameForger/Editor/Terrain.hpp"
#include "GameForger/Runtime/GameCamera.hpp"
#include "GameForger/Runtime/GameplayLoop.hpp"

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
				runtime->commandBus().execute(SetPropertyCommand{entity->name, "Transform", "position", value});
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
				runtime->commandBus().execute(SetPropertyCommand{entity->name, "Transform", "rotation", value});
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

		// The pivot offset in the object's own [-1,1] box (0,0,0 = center,
		// 0,-1,0 = bottom) - with getPosition/getScale/getRotation a script
		// can work out the object's real box (climbable.lua does).
		int luaEntityGetPivot(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const SceneEntity* entity = runtime->scene().findEntity(entityIdFromUpvalue(L));
			pushVec3(L, entity != nullptr ? entity->pivotOffset : glm::vec3(0.0F));
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
			// Screen-right for a camera looking along `forward`. The game camera
			// is glm::lookAt (right-handed), whose right axis is
			// cross(forward, up) - i.e. -X when looking down +Z (+X shows on
			// screen-LEFT). The 2026-08-13 "fix" (audit H-Script-1) switched this
			// to cross(up, forward) = +X on paper, which made D strafe left.
			const glm::vec3 forward(std::sin(yawRadians), 0.0F, std::cos(yawRadians));
			pushVec3(L, glm::normalize(glm::cross(forward, glm::vec3(0.0F, 1.0F, 0.0F))));
			return 1;
		}

		// ---- Shared helpers for the weapon/inventory/effects API ----

		GameplayState* gameplayFrom(lua_State* L)
		{
			return runtimeFrom(L)->gameplayState();
		}

		// Accepts {r=,g=,b=} or {x=,y=,z=} (0..1) - colors read naturally
		// either way in a script.
		glm::vec3 optColor(lua_State* L, const int index, const glm::vec3& fallback)
		{
			if (!lua_istable(L, index))
			{
				return fallback;
			}
			glm::vec3 color = fallback;
			const char* keys[3][2] = {{"r", "x"}, {"g", "y"}, {"b", "z"}};
			for (int channel = 0; channel < 3; ++channel)
			{
				for (const char* key : keys[channel])
				{
					lua_getfield(L, index, key);
					if (lua_isnumber(L, -1))
					{
						color[channel] = static_cast<float>(lua_tonumber(L, -1));
						lua_pop(L, 1);
						break;
					}
					lua_pop(L, 1);
				}
			}
			return color;
		}

		// Calls `functionName(self, number, text)` on each running instance
		// of entityId (see ScriptRuntime::invokeHook). Uses the CALLING
		// thread's L so it's safe from inside a coroutine too.
		bool invokeHookOn(
			lua_State* L, ScriptRuntime& runtime, const std::vector<int>& refs, const char* functionName,
			const float number, const std::string& text)
		{
			bool handled = false;
			for (const int ref : refs)
			{
				lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
				lua_getfield(L, -1, functionName);
				if (!lua_isfunction(L, -1))
				{
					lua_pop(L, 2);
					continue;
				}
				lua_pushvalue(L, -2);
				lua_pushnumber(L, static_cast<lua_Number>(number));
				lua_pushstring(L, text.c_str());
				if (lua_pcall(L, 3, 0, 0) != LUA_OK)
				{
					runtime.log(true, std::string("Error in ") + functionName + "(): " + lua_tostring(L, -1));
					lua_pop(L, 1);
				}
				else
				{
					handled = true;
				}
				lua_pop(L, 1);
			}
			return handled;
		}

		// ---- self.entity additions ----

		int luaEntityGetName(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const SceneEntity* entity = runtime->scene().findEntity(entityIdFromUpvalue(L));
			lua_pushstring(L, entity != nullptr ? entity->name.c_str() : "");
			return 1;
		}

		int luaEntitySetActive(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const bool active = lua_toboolean(L, 2) != 0;
			if (const SceneEntity* entity = runtime->scene().findEntity(entityIdFromUpvalue(L)))
			{
				(void)runtime->commandBus().execute(SetPropertyCommand{entity->name, "Entity", "active", active});
			}
			return 0;
		}

		// ---- self.camera additions ----

		// First-person eye height above the entity's feet (crouching lowers
		// it). A Play-time change, discarded on Stop like any other.
		int luaCameraSetEyeHeight(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const SceneEntity* entity = runtime->scene().findEntity(entityIdFromUpvalue(L));
			const float height = static_cast<float>(luaL_checknumber(L, 2));
			if (entity != nullptr && std::abs(entity->cameraRig.fpsEyeHeight - height) > 1e-4F)
			{
				(void)runtime->commandBus().execute(SetPropertyCommand{entity->name, "Camera", "fpsEyeHeight", height});
			}
			return 0;
		}

		int luaCameraGetEyeHeight(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const SceneEntity* entity = runtime->scene().findEntity(entityIdFromUpvalue(L));
			lua_pushnumber(L, static_cast<lua_Number>(entity != nullptr ? entity->cameraRig.fpsEyeHeight : 0.0F));
			return 1;
		}

		int luaCameraGetPitch(lua_State* L)
		{
			const GameplayState* gameplay = gameplayFrom(L);
			lua_pushnumber(L, static_cast<lua_Number>(gameplay != nullptr ? gameplay->lookPitchDegrees : 0.0F));
			return 1;
		}

		// eye, forward - exactly where the Game view camera is and points
		// right now (first- or third-person), for aiming.
		int luaCameraGetAim(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const int entityId = entityIdFromUpvalue(L);
			const SceneEntity* entity = runtime->scene().findEntity(entityId);
			if (entity == nullptr)
			{
				lua_pushnil(L);
				lua_pushnil(L);
				return 2;
			}
			const GameplayState* gameplay = gameplayFrom(L);
			const float pitch = gameplay != nullptr ? gameplay->lookPitchDegrees : 0.0F;
			const float yaw = gameplay != nullptr ? gameplay->lookYawDegrees : 0.0F;
			const bool isActive = runtime->hasActiveCamera() && runtime->activeCameraEntityId() == entityId;
			const std::string mode = isActive ? runtime->activeCameraMode() : std::string("fps");
			glm::vec3 eye;
			glm::vec3 forward;
			if (mode == "third_person")
			{
				const GameCameraState camera = scriptedPlayCamera(*entity, mode, yaw, pitch);
				eye = camera.target + camera.distance * glm::vec3(
					std::cos(camera.pitch) * std::sin(camera.yaw),
					std::sin(camera.pitch),
					std::cos(camera.pitch) * std::cos(camera.yaw));
				forward = glm::normalize(camera.target - eye);
			}
			else
			{
				eye = entity->position + glm::vec3(0.0F, entity->cameraRig.fpsEyeHeight, 0.0F);
				forward = yawPitchForward(entity->rotationEuler.y, pitch);
			}
			pushVec3(L, eye);
			pushVec3(L, forward);
			return 2;
		}

		// ---- self.world additions ----

		int luaWorldGetEntityPosition(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const SceneEntity* entity = runtime->scene().findEntity(std::string(luaL_checkstring(L, 2)));
			if (entity == nullptr)
			{
				lua_pushnil(L);
				return 1;
			}
			pushVec3(L, entity->position);
			return 1;
		}

		// Moves a DIFFERENT named entity. Writes the local position when the
		// target is parented (same reasoning as setEntityRotation above).
		int luaWorldSetEntityPosition(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const std::string entityName = luaL_checkstring(L, 2);
			const glm::vec3 position = checkVec3(L, 3);
			const SceneEntity* entity = runtime->scene().findEntity(entityName);
			if (entity == nullptr)
			{
				return 0;
			}
			if (entity->parentName.empty())
			{
				(void)runtime->commandBus().execute(SetPropertyCommand{entity->name, "Transform", "position", position});
			}
			else
			{
				(void)runtime->commandBus().execute(SetPropertyCommand{entity->name, "Parent", "localPosition", position});
			}
			return 0;
		}

		int luaWorldSetEntityActive(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const std::string entityName = luaL_checkstring(L, 2);
			const bool active = lua_toboolean(L, 3) != 0;
			const SceneEntity* entity = runtime->scene().findEntity(entityName);
			if (entity != nullptr && entity->active != active)
			{
				(void)runtime->commandBus().execute(SetPropertyCommand{entity->name, "Entity", "active", active});
			}
			return 0;
		}

		int luaWorldIsEntityActive(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const SceneEntity* entity = runtime->scene().findEntity(std::string(luaL_checkstring(L, 2)));
			lua_pushboolean(L, entity != nullptr && runtime->scene().isActiveInHierarchy(*entity) ? 1 : 0);
			return 1;
		}

		// Entities a ray/area query should never see: the caller itself,
		// hidden things (incl. items stored in an inventory), cine
		// cameras, and first-person viewmodel parts (tag "Viewmodel") or
		// anything tagged "NoRaycast".
		bool isQueryable(const EditorScene& scene, const SceneEntity& entity, const int selfEntityId)
		{
			return entity.id != selfEntityId && !entity.isCineCamera && !hasTag(entity, "Viewmodel") &&
				!hasTag(entity, "NoRaycast") && scene.isActiveInHierarchy(entity);
		}

		// hitPoint, distance, name - or nil. Boxes are each entity's
		// world AABB (position +/- scale, the same rotation-blind box the
		// collider uses); terrain is sampled along the ray.
		int luaWorldRaycast(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const glm::vec3 origin = checkVec3(L, 2);
			glm::vec3 direction = checkVec3(L, 3);
			const float maxDistance = static_cast<float>(luaL_optnumber(L, 4, 100.0));
			const float length = glm::length(direction);
			if (length < 0.0001F || maxDistance <= 0.0F)
			{
				lua_pushnil(L);
				return 1;
			}
			direction /= length;
			const int selfEntityId = entityIdFromUpvalue(L);
			const EditorScene& scene = runtime->scene();

			const SceneEntity* bestEntity = nullptr;
			float bestDistance = maxDistance;
			for (const SceneEntity& entity : scene.entities())
			{
				if (!isQueryable(scene, entity, selfEntityId))
				{
					continue;
				}
				if (entity.isTerrain)
				{
					const float half = entity.terrain.worldSize * 0.5F;
					constexpr float kStep = 0.25F;
					for (float t = 0.0F; t <= bestDistance; t += kStep)
					{
						const glm::vec3 point = origin + direction * t;
						const float localX = point.x - entity.position.x;
						const float localZ = point.z - entity.position.z;
						if (localX < -half || localX > half || localZ < -half || localZ > half)
						{
							continue;
						}
						const float groundY = entity.position.y +
							sampleTerrainHeight(
								entity.terrain.resolution, entity.terrain.worldSize, entity.terrain.heightScale,
								entity.terrain.heights, localX, localZ);
						if (point.y <= groundY)
						{
							bestDistance = t;
							bestEntity = &entity;
							break;
						}
					}
					continue;
				}
				const glm::vec3 boxMin = entity.position - glm::abs(entity.scale);
				const glm::vec3 boxMax = entity.position + glm::abs(entity.scale);
				float tNear = 0.0F;
				float tFar = bestDistance;
				bool hit = true;
				for (int axis = 0; axis < 3 && hit; ++axis)
				{
					if (std::abs(direction[axis]) < 1e-6F)
					{
						hit = origin[axis] >= boxMin[axis] && origin[axis] <= boxMax[axis];
						continue;
					}
					float t1 = (boxMin[axis] - origin[axis]) / direction[axis];
					float t2 = (boxMax[axis] - origin[axis]) / direction[axis];
					if (t1 > t2)
					{
						std::swap(t1, t2);
					}
					tNear = std::max(tNear, t1);
					tFar = std::min(tFar, t2);
					hit = tNear <= tFar;
				}
				if (hit && tNear < bestDistance)
				{
					bestDistance = tNear;
					bestEntity = &entity;
				}
			}
			if (bestEntity == nullptr)
			{
				lua_pushnil(L);
				return 1;
			}
			pushVec3(L, origin + direction * bestDistance);
			lua_pushnumber(L, static_cast<lua_Number>(bestDistance));
			lua_pushstring(L, bestEntity->name.c_str());
			return 3;
		}

		void pushTargetList(lua_State* L, std::vector<std::pair<float, const SceneEntity*>>& found)
		{
			std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
			lua_createtable(L, static_cast<int>(found.size()), 0);
			int index = 1;
			for (const auto& [distance, entity] : found)
			{
				lua_createtable(L, 0, 3);
				lua_pushstring(L, entity->name.c_str());
				lua_setfield(L, -2, "name");
				pushVec3(L, entity->position);
				lua_setfield(L, -2, "position");
				lua_pushnumber(L, static_cast<lua_Number>(distance));
				lua_setfield(L, -2, "distance");
				lua_rawseti(L, -2, index++);
			}
		}

		// Every OTHER active entity carrying `tag` within `radius` of
		// `center`, nearest first: { {name=, position=, distance=}, ... }.
		int luaWorldFindAllWithTag(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const std::string tag = luaL_checkstring(L, 2);
			const glm::vec3 center = checkVec3(L, 3);
			const float radius = static_cast<float>(luaL_checknumber(L, 4));
			const EditorScene& scene = runtime->scene();
			std::vector<std::pair<float, const SceneEntity*>> found;
			for (const SceneEntity& entity : scene.entities())
			{
				if (!isQueryable(scene, entity, entityIdFromUpvalue(L)) || !hasTag(entity, tag))
				{
					continue;
				}
				const float distance = glm::length(entity.position - center);
				if (distance <= radius)
				{
					found.emplace_back(distance, &entity);
				}
			}
			pushTargetList(L, found);
			return 1;
		}

		// Same shape as findAllWithTag, but matches anything that can take
		// damage (has a running script defining on_damage, e.g. health.lua)
		// instead of a tag.
		int luaWorldFindDamageable(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const glm::vec3 center = checkVec3(L, 2);
			const float radius = static_cast<float>(luaL_checknumber(L, 3));
			const EditorScene& scene = runtime->scene();
			std::vector<std::pair<float, const SceneEntity*>> found;
			for (const SceneEntity& entity : scene.entities())
			{
				if (!isQueryable(scene, entity, entityIdFromUpvalue(L)) || !runtime->hasHook(entity.id, "on_damage"))
				{
					continue;
				}
				const float distance = glm::length(entity.position - center);
				if (distance <= radius)
				{
					found.emplace_back(distance, &entity);
				}
			}
			pushTargetList(L, found);
			return 1;
		}

		// spawnBeam(from, to, color, seconds, width, jagged) - a tracer /
		// electric arc drawn by the host for `seconds`. Visual only.
		int luaWorldSpawnBeam(lua_State* L)
		{
			GameplayState* gameplay = gameplayFrom(L);
			if (gameplay == nullptr)
			{
				return 0;
			}
			GameplayState::Beam beam;
			beam.from = checkVec3(L, 2);
			beam.to = checkVec3(L, 3);
			beam.color = optColor(L, 4, glm::vec3(1.0F, 0.9F, 0.5F));
			beam.totalSeconds = std::clamp(static_cast<float>(luaL_optnumber(L, 5, 0.08)), 0.01F, 5.0F);
			beam.remainingSeconds = beam.totalSeconds;
			beam.width = std::clamp(static_cast<float>(luaL_optnumber(L, 6, 2.0)), 0.5F, 12.0F);
			beam.jagged = lua_toboolean(L, 7) != 0;
			beam.seed = static_cast<unsigned int>(gameplay->beams.size() * 7919U + gameplay->flashes.size() * 104729U) ^
				static_cast<unsigned int>(gameplay->playElapsedTime * 1000.0F);
			constexpr std::size_t kMaxBeams = 1024;
			if (gameplay->beams.size() < kMaxBeams)
			{
				gameplay->beams.push_back(beam);
			}
			return 0;
		}

		// spawnFlash(position, color, size, seconds) - muzzle flash /
		// impact spark. Visual only.
		int luaWorldSpawnFlash(lua_State* L)
		{
			GameplayState* gameplay = gameplayFrom(L);
			if (gameplay == nullptr)
			{
				return 0;
			}
			GameplayState::Flash flash;
			flash.position = checkVec3(L, 2);
			flash.color = optColor(L, 3, glm::vec3(1.0F, 0.85F, 0.4F));
			flash.size = std::clamp(static_cast<float>(luaL_optnumber(L, 4, 12.0)), 1.0F, 80.0F);
			flash.totalSeconds = std::clamp(static_cast<float>(luaL_optnumber(L, 5, 0.08)), 0.01F, 5.0F);
			flash.remainingSeconds = flash.totalSeconds;
			constexpr std::size_t kMaxFlashes = 2048;
			if (gameplay->flashes.size() < kMaxFlashes)
			{
				gameplay->flashes.push_back(flash);
			}
			return 0;
		}

		// particle(position, color, worldSize, intensity) - one frame of one
		// particle (size in world units, so it shrinks with distance). For
		// scripts running their own particle simulation (effects.lua).
		int luaWorldParticle(lua_State* L)
		{
			GameplayState* gameplay = gameplayFrom(L);
			if (gameplay == nullptr)
			{
				return 0;
			}
			constexpr std::size_t kMaxFlashes = 2048;
			if (gameplay->flashes.size() >= kMaxFlashes)
			{
				return 0;
			}
			GameplayState::Flash flash;
			flash.position = checkVec3(L, 2);
			flash.color = optColor(L, 3, glm::vec3(1.0F));
			flash.size = std::clamp(static_cast<float>(luaL_optnumber(L, 4, 0.05)), 0.001F, 5.0F);
			flash.intensity = std::clamp(static_cast<float>(luaL_optnumber(L, 5, 1.0)), 0.0F, 1.0F);
			flash.particle = true;
			flash.framesLeft = 1;
			flash.totalSeconds = 1.0F;
			flash.remainingSeconds = 1.0F;
			gameplay->flashes.push_back(flash);
			return 0;
		}

		// ---- self.inventory ----

		void pushInventorySlot(lua_State* L, const GameplayState::InventoryItem& item, const int slotIndex)
		{
			lua_createtable(L, 0, 7);
			lua_pushinteger(L, slotIndex + 1);
			lua_setfield(L, -2, "slot");
			lua_pushstring(L, item.itemName.c_str());
			lua_setfield(L, -2, "name");
			lua_pushstring(L, item.iconPath.c_str());
			lua_setfield(L, -2, "icon");
			lua_pushstring(L, item.itemType.c_str());
			lua_setfield(L, -2, "type");
			lua_pushstring(L, item.weapon.c_str());
			lua_setfield(L, -2, "weapon");
			lua_pushinteger(L, item.count);
			lua_setfield(L, -2, "count");
		}

		// The equipped slot's item table, or nil if that slot is empty.
		int luaInventoryGetSelected(lua_State* L)
		{
			const GameplayState* gameplay = gameplayFrom(L);
			if (gameplay == nullptr || gameplay->selectedSlot < 0 ||
				gameplay->selectedSlot >= static_cast<int>(gameplay->inventoryItems.size()) ||
				gameplay->inventoryItems[static_cast<std::size_t>(gameplay->selectedSlot)].empty())
			{
				lua_pushnil(L);
				return 1;
			}
			pushInventorySlot(
				L, gameplay->inventoryItems[static_cast<std::size_t>(gameplay->selectedSlot)], gameplay->selectedSlot);
			return 1;
		}

		int luaInventoryGetSlot(lua_State* L)
		{
			const GameplayState* gameplay = gameplayFrom(L);
			const int slotIndex = static_cast<int>(luaL_checkinteger(L, 2)) - 1;
			if (gameplay == nullptr || slotIndex < 0 || slotIndex >= static_cast<int>(gameplay->inventoryItems.size()) ||
				gameplay->inventoryItems[static_cast<std::size_t>(slotIndex)].empty())
			{
				lua_pushnil(L);
				return 1;
			}
			pushInventorySlot(L, gameplay->inventoryItems[static_cast<std::size_t>(slotIndex)], slotIndex);
			return 1;
		}

		int luaInventoryGetSelectedSlot(lua_State* L)
		{
			const GameplayState* gameplay = gameplayFrom(L);
			lua_pushinteger(L, gameplay != nullptr ? gameplay->selectedSlot + 1 : 1);
			return 1;
		}

		int luaInventorySelect(lua_State* L)
		{
			GameplayState* gameplay = gameplayFrom(L);
			const int slotIndex = static_cast<int>(luaL_checkinteger(L, 2)) - 1;
			if (gameplay != nullptr && slotIndex >= 0 && slotIndex < kInventorySlotCount)
			{
				gameplay->selectedSlot = slotIndex;
			}
			return 0;
		}

		int luaInventoryGetSlotCount(lua_State* L)
		{
			lua_pushinteger(L, kInventorySlotCount);
			return 1;
		}

		int luaInventoryGetHotbarSize(lua_State* L)
		{
			lua_pushinteger(L, kHotbarSlotCount);
			return 1;
		}

		// True while the inventory grid is open - the mouse belongs to the
		// UI then, so a controller should stop looking/attacking.
		int luaInventoryIsOpen(lua_State* L)
		{
			const GameplayState* gameplay = gameplayFrom(L);
			lua_pushboolean(L, gameplay != nullptr && gameplay->inventoryOpen ? 1 : 0);
			return 1;
		}

		// Uses up `count` (default 1) of the equipped item - e.g. drinking
		// a potion. The used-up objects are deleted from the scene.
		int luaInventoryConsumeSelected(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			GameplayState* gameplay = gameplayFrom(L);
			const int count = static_cast<int>(luaL_optinteger(L, 2, 1));
			if (gameplay == nullptr || count <= 0 || gameplay->selectedSlot < 0 ||
				gameplay->selectedSlot >= static_cast<int>(gameplay->inventoryItems.size()))
			{
				lua_pushboolean(L, 0);
				return 1;
			}
			GameplayState::InventoryItem& item = gameplay->inventoryItems[static_cast<std::size_t>(gameplay->selectedSlot)];
			if (item.count < count)
			{
				lua_pushboolean(L, 0);
				return 1;
			}
			for (int used = 0; used < count; ++used)
			{
				if (!item.storedEntityNames.empty())
				{
					(void)runtime->commandBus().execute(DeleteEntityCommand{item.storedEntityNames.back()});
					item.storedEntityNames.pop_back();
				}
			}
			item.count -= count;
			if (item.count <= 0)
			{
				item = GameplayState::InventoryItem{};
			}
			lua_pushboolean(L, 1);
			return 1;
		}

		// Defined further down with the other messaging/combat bindings.
		int luaInventoryAddEntity(lua_State* L);
		int luaInventoryFindWeapon(lua_State* L);

		void pushInventoryProxy(lua_State* L, const int entityId)
		{
			lua_newtable(L);
			const auto addMethod = [L, entityId](const char* name, lua_CFunction function)
			{
				lua_pushinteger(L, entityId);
				lua_pushcclosure(L, function, 1);
				lua_setfield(L, -2, name);
			};
			addMethod("getSelected", luaInventoryGetSelected);
			addMethod("getSlot", luaInventoryGetSlot);
			addMethod("getSelectedSlot", luaInventoryGetSelectedSlot);
			addMethod("select", luaInventorySelect);
			addMethod("getSlotCount", luaInventoryGetSlotCount);
			addMethod("getHotbarSize", luaInventoryGetHotbarSize);
			addMethod("isOpen", luaInventoryIsOpen);
			addMethod("consumeSelected", luaInventoryConsumeSelected);
			addMethod("addEntity", luaInventoryAddEntity);
			addMethod("findWeapon", luaInventoryFindWeapon);
		}

		// ---- input additions ----

		int luaInputIsMouseButtonDown(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			lua_pushboolean(L, runtime->inputSource().isMouseButtonDown(luaL_checkstring(L, 2)));
			return 1;
		}

		int luaInputGetScrollDelta(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			lua_pushnumber(L, static_cast<lua_Number>(runtime->inputSource().getScrollDelta()));
			return 1;
		}

		// ---- @property values ----

		glm::vec3 parseVec3Text(const std::string& text, const glm::vec3& fallback)
		{
			std::string spaced = text;
			std::replace(spaced.begin(), spaced.end(), ',', ' ');
			std::istringstream stream(spaced);
			glm::vec3 value = fallback;
			stream >> value.x >> value.y >> value.z;
			return stream.fail() && !stream.eof() ? fallback : value;
		}

		void pushPropertyValue(
			lua_State* L, const ScriptRuntime::ExposedScriptProperty& property, const std::string& text)
		{
			using Type = ScriptRuntime::ExposedScriptProperty::Type;
			switch (property.type)
			{
				case Type::Number:
				case Type::Slider:
				{
					char* end = nullptr;
					const float value = std::strtof(text.c_str(), &end);
					lua_pushnumber(L, static_cast<lua_Number>(end != text.c_str() ? value : property.defaultNumber));
					break;
				}
				case Type::Bool:
					lua_pushboolean(L, text == "true" || text == "1" ? 1 : 0);
					break;
				case Type::Vec3:
					pushVec3(L, parseVec3Text(text, property.defaultVec3));
					break;
				default:
					lua_pushstring(L, text.c_str());
					break;
			}
		}

		// ---- Script-to-script messaging ----

		// Calls `functionName(self, <the top argCount stack values>)` on every
		// running script instance of entityId that defines it, using the
		// calling thread's L. Pops the arguments and pushes two results:
		// handled (bool) and the first non-nil value any handler returned.
		int callScriptsWithTopArgs(
			lua_State* L, ScriptRuntime& runtime, const int entityId, const char* functionName, const int argCount)
		{
			const int firstArg = lua_gettop(L) - argCount + 1;
			lua_pushnil(L);
			const int resultIndex = lua_gettop(L);
			bool handled = false;
			for (const int ref : runtime.instanceRefs(entityId))
			{
				lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
				lua_getfield(L, -1, functionName);
				if (!lua_isfunction(L, -1))
				{
					lua_pop(L, 2);
					continue;
				}
				lua_pushvalue(L, -2); // self
				for (int arg = 0; arg < argCount; ++arg)
				{
					lua_pushvalue(L, firstArg + arg);
				}
				if (lua_pcall(L, argCount + 1, 1, 0) != LUA_OK)
				{
					runtime.log(true, std::string("Error in ") + functionName + "(): " + lua_tostring(L, -1));
					lua_pop(L, 1);
				}
				else
				{
					handled = true;
					if (lua_isnil(L, resultIndex) && !lua_isnil(L, -1))
					{
						lua_replace(L, resultIndex);
					}
					else
					{
						lua_pop(L, 1);
					}
				}
				lua_pop(L, 1); // instance table
			}
			// Leave: handled, result - and drop the argument copies below them.
			lua_pushboolean(L, handled ? 1 : 0);
			lua_insert(L, resultIndex);
			lua_rotate(L, firstArg, 2);
			lua_settop(L, firstArg + 1);
			return 2;
		}

		// self.entity:send(fn, ...) - call fn on the OTHER scripts attached to
		// this same object (e.g. fps_player.lua -> projectiles.lua). Returns
		// handled, firstResult.
		int luaEntitySend(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const std::string functionName = luaL_checkstring(L, 2);
			const int argCount = lua_gettop(L) - 2;
			return callScriptsWithTopArgs(L, *runtime, entityIdFromUpvalue(L), functionName.c_str(), argCount);
		}

		// self.world:send(entityName, fn, ...) - same, on another object.
		int luaWorldSend(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const std::string targetName = luaL_checkstring(L, 2);
			const std::string functionName = luaL_checkstring(L, 3);
			const int argCount = lua_gettop(L) - 3;
			const SceneEntity* target = runtime->scene().findEntity(targetName);
			if (target == nullptr)
			{
				lua_pushboolean(L, 0);
				lua_pushnil(L);
				return 2;
			}
			return callScriptsWithTopArgs(L, *runtime, target->id, functionName.c_str(), argCount);
		}

		// Shared by damage/stun/heal: fn(self, amount, attackerName, info).
		int callCombatHook(lua_State* L, const char* functionName)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const std::string targetName = luaL_checkstring(L, 2);
			const lua_Number amount = luaL_checknumber(L, 3);
			const SceneEntity* self = runtime->scene().findEntity(entityIdFromUpvalue(L));
			const SceneEntity* target = runtime->scene().findEntity(targetName);
			if (target == nullptr || !runtime->scene().isActiveInHierarchy(*target))
			{
				lua_pushboolean(L, 0);
				lua_pushnil(L);
				return 2;
			}
			lua_pushnumber(L, amount);
			lua_pushstring(L, self != nullptr ? self->name.c_str() : "");
			if (lua_istable(L, 4))
			{
				lua_pushvalue(L, 4);
			}
			else
			{
				lua_newtable(L);
			}
			return callScriptsWithTopArgs(L, *runtime, target->id, functionName, 3);
		}

		// damage(name, amount [, info]) -> handled, result. Calls the target's
		// on_damage(amount, attackerName, info) - info is an optional table
		// (e.g. {crit = true, element = "fire"}); health.lua returns true
		// from on_damage when that hit killed it.
		int luaWorldDamage(lua_State* L)
		{
			return callCombatHook(L, "on_damage");
		}

		// stun(name, seconds [, info]) -> handled. on_stun(seconds, attacker, info).
		int luaWorldStun(lua_State* L)
		{
			return callCombatHook(L, "on_stun");
		}

		// heal(name, amount [, info]) -> handled. on_heal(amount, healer, info).
		int luaWorldHeal(lua_State* L)
		{
			return callCombatHook(L, "on_heal");
		}

		// spawnText(position, text, color, seconds, scale) - floating text.
		int luaWorldSpawnText(lua_State* L)
		{
			GameplayState* gameplay = gameplayFrom(L);
			if (gameplay == nullptr)
			{
				return 0;
			}
			GameplayState::FloatingText text;
			text.position = checkVec3(L, 2);
			text.text = luaL_checkstring(L, 3);
			text.color = optColor(L, 4, glm::vec3(1.0F));
			text.totalSeconds = std::clamp(static_cast<float>(luaL_optnumber(L, 5, 1.0)), 0.1F, 10.0F);
			text.remainingSeconds = text.totalSeconds;
			text.scale = std::clamp(static_cast<float>(luaL_optnumber(L, 6, 1.0)), 0.5F, 3.0F);
			constexpr std::size_t kMaxTexts = 128;
			if (gameplay->floatingTexts.size() < kMaxTexts)
			{
				gameplay->floatingTexts.push_back(std::move(text));
			}
			return 0;
		}

		// setHudBar(id, label, fraction, color, order) - bottom-left bar.
		int luaWorldSetHudBar(lua_State* L)
		{
			GameplayState* gameplay = gameplayFrom(L);
			if (gameplay == nullptr)
			{
				return 0;
			}
			const std::string id = luaL_checkstring(L, 2);
			GameplayState::HudBar bar;
			bar.id = id;
			bar.label = luaL_optstring(L, 3, "");
			bar.fraction = std::clamp(static_cast<float>(luaL_optnumber(L, 4, 1.0)), 0.0F, 1.0F);
			bar.color = optColor(L, 5, glm::vec3(0.3F, 0.8F, 0.3F));
			bar.order = static_cast<int>(luaL_optinteger(L, 6, 0));
			auto& bars = gameplay->hudBars;
			const auto existing = std::find_if(bars.begin(), bars.end(), [&id](const auto& b) { return b.id == id; });
			if (existing != bars.end())
			{
				*existing = std::move(bar);
			}
			else
			{
				bars.push_back(std::move(bar));
				std::stable_sort(bars.begin(), bars.end(), [](const auto& a, const auto& b) { return a.order < b.order; });
			}
			return 0;
		}

		int luaWorldClearHudBar(lua_State* L)
		{
			GameplayState* gameplay = gameplayFrom(L);
			if (gameplay != nullptr)
			{
				const std::string id = luaL_checkstring(L, 2);
				std::erase_if(gameplay->hudBars, [&id](const auto& b) { return b.id == id; });
			}
			return 0;
		}

		// showMessage(text, seconds) - one centered message line.
		int luaWorldShowMessage(lua_State* L)
		{
			GameplayState* gameplay = gameplayFrom(L);
			if (gameplay != nullptr)
			{
				gameplay->messageText = luaL_checkstring(L, 2);
				gameplay->messageSecondsRemaining = std::clamp(static_cast<float>(luaL_optnumber(L, 3, 2.0)), 0.2F, 30.0F);
			}
			return 0;
		}

		// inventory:addEntity(name) -> bool. Puts a scene object carrying
		// items.lua into the inventory (hidden) - e.g. xp_system.lua granting
		// a weapon unlock. Works on hidden objects too.
		int luaInventoryAddEntity(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			GameplayState* gameplay = gameplayFrom(L);
			const SceneEntity* entity = runtime->scene().findEntity(std::string(luaL_checkstring(L, 2)));
			if (gameplay == nullptr || entity == nullptr)
			{
				lua_pushboolean(L, 0);
				return 1;
			}
			const bool added = pickUpItem(runtime->scene(), runtime->commandBus(), *gameplay, *runtime, *entity);
			lua_pushboolean(L, added ? 1 : 0);
			return 1;
		}

		// inventory:findWeapon(weaponId) -> slot (1-based) or nil.
		int luaInventoryFindWeapon(lua_State* L)
		{
			const GameplayState* gameplay = gameplayFrom(L);
			const std::string weapon = luaL_checkstring(L, 2);
			if (gameplay != nullptr)
			{
				for (std::size_t index = 0; index < gameplay->inventoryItems.size(); ++index)
				{
					const auto& item = gameplay->inventoryItems[index];
					if (!item.empty() && item.weapon == weapon)
					{
						lua_pushinteger(L, static_cast<lua_Integer>(index) + 1);
						return 1;
					}
				}
			}
			lua_pushnil(L);
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
			addMethod("getPivot", luaEntityGetPivot);
			addMethod("getForward", luaEntityGetForward);
			addMethod("getRight", luaEntityGetRight);
			addMethod("getName", luaEntityGetName);
			addMethod("setActive", luaEntitySetActive);
			addMethod("send", luaEntitySend);
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
			addMethod("getPitch", luaCameraGetPitch);
			addMethod("setEyeHeight", luaCameraSetEyeHeight);
			addMethod("getEyeHeight", luaCameraGetEyeHeight);
			addMethod("getAim", luaCameraGetAim);
		}

		struct BoxCollisionResult
		{
			glm::vec3 position;
			bool grounded = false;
		};

		// Treats the moving entity as an axis-aligned box (halfWidth in X/Z,
		// height in Y, `position` is its feet/base per this project's
		// convention) and pushes it out of any overlapping Collider-enabled
		// entity's world-space AABB, along whichever axis has the least
		// penetration. A push along Y means "resting on top" (grounded) or
		// "bumped a ceiling"; a push along X or Z means a wall/object blocked
		// movement. This is a deliberately simple approximation - no rotation-
		// aware colliders, no swept collision - not a general physics engine.
		BoxCollisionResult resolveBoxCollision(
			const EditorScene& scene,
			const int selfEntityId,
			const glm::vec3& startPosition,
			const float halfWidth,
			const float height)
		{
			BoxCollisionResult result{startPosition, false};

			// A few passes so resolving one collider doesn't reintroduce an
			// overlap with another that was already resolved this frame.
			for (int pass = 0; pass < 3; ++pass)
			{
				bool resolvedAny = false;
				for (const SceneEntity& other : scene.entities())
				{
					if (other.id == selfEntityId || !other.hasCollider || !scene.isActiveInHierarchy(other))
					{
						continue;
					}

					if (other.isTerrain)
					{
						// A terrain's real footprint/shape comes from
						// worldSize/heightScale/heights, not the generic
						// Transform position+/-scale box every other
						// collider below uses - scale is usually left at
						// its default (1,1,1) for a Terrain entity, which
						// would otherwise collapse the whole heightmap down
						// to a practically useless 2x2x2 box centered on the
						// terrain's origin. Sample the actual heightmap at
						// the mover's XZ instead (rotation-blind, like every
						// other collider here - see the class doc comment)
						// so the mover rests on hills/canyons at their real
						// height, not just wherever the terrain entity
						// happens to sit near world Y=0.
						const float half = other.terrain.worldSize * 0.5F;
						const float localX = result.position.x - other.position.x;
						const float localZ = result.position.z - other.position.z;
						if (localX < -half || localX > half || localZ < -half || localZ > half)
						{
							continue;
						}
						const float groundY = other.position.y +
							sampleTerrainHeight(
								other.terrain.resolution,
								other.terrain.worldSize,
								other.terrain.heightScale,
								other.terrain.heights,
								localX,
								localZ);
						if (result.position.y <= groundY)
						{
							result.position.y = groundY;
							result.grounded = true;
							resolvedAny = true;
						}
						continue;
					}

					const glm::vec3 otherMin = other.position - other.scale;
					const glm::vec3 otherMax = other.position + other.scale;
					const glm::vec3 selfMin(
						result.position.x - halfWidth, result.position.y, result.position.z - halfWidth);
					const glm::vec3 selfMax(
						result.position.x + halfWidth,
						result.position.y + height,
						result.position.z + halfWidth);

					const float overlapX = std::min(selfMax.x, otherMax.x) - std::max(selfMin.x, otherMin.x);
					const float overlapY = std::min(selfMax.y, otherMax.y) - std::max(selfMin.y, otherMin.y);
					const float overlapZ = std::min(selfMax.z, otherMax.z) - std::max(selfMin.z, otherMin.z);
					if (overlapX <= 0.0F || overlapY <= 0.0F || overlapZ <= 0.0F)
					{
						continue;
					}

					resolvedAny = true;
					const float selfCenterY = result.position.y + height * 0.5F;
					// If the mover's entire X/Z footprint sits inside the
					// other collider's footprint, this can only be a vertical
					// overlap (a wall could only ever clip one edge, never
					// fully surround it) - so prefer resolving Y even when
					// its raw overlap isn't the smallest. Without this, a wide
					// or thick platform (e.g. a default-scaled cube used as
					// ground, not yet flattened) reads as having a smaller
					// horizontal overlap than vertical purely because the
					// horizontal overlap is capped at the mover's own width,
					// so it gets shoved sideways every frame instead of
					// standing on top - grounded never becomes true, gravity
					// piles up unbounded, and both movement and jumping look
					// like they've stopped working entirely.
					const bool fullyContainedHorizontally =
						overlapX >= (2.0F * halfWidth - 0.001F) && overlapZ >= (2.0F * halfWidth - 0.001F);
					if (fullyContainedHorizontally || (overlapY <= overlapX && overlapY <= overlapZ))
					{
						if (selfCenterY > other.position.y)
						{
							result.position.y = otherMax.y;
							result.grounded = true;
						}
						else
						{
							result.position.y = otherMin.y - height;
						}
					}
					else if (overlapX <= overlapZ)
					{
						result.position.x =
							result.position.x > other.position.x ? otherMax.x + halfWidth : otherMin.x - halfWidth;
					}
					else
					{
						result.position.z =
							result.position.z > other.position.z ? otherMax.z + halfWidth : otherMin.z - halfWidth;
					}
				}
				if (!resolvedAny)
				{
					break;
				}
			}

			return result;
		}

		int luaPhysicsResolve(lua_State* L)
		{
			ScriptRuntime* runtime = runtimeFrom(L);
			const glm::vec3 position = checkVec3(L, 2);
			const float halfWidth = static_cast<float>(luaL_checknumber(L, 3));
			const float height = static_cast<float>(luaL_checknumber(L, 4));
			const BoxCollisionResult result =
				resolveBoxCollision(runtime->scene(), entityIdFromUpvalue(L), position, halfWidth, height);
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
				runtime->commandBus().execute(SetPropertyCommand{entity->name, "Transform", "rotation", rotation});
			}
			else
			{
				runtime->commandBus().execute(SetPropertyCommand{entity->name, "Parent", "localRotation", rotation});
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
			addMethod("getEntityPosition", luaWorldGetEntityPosition);
			addMethod("setEntityPosition", luaWorldSetEntityPosition);
			addMethod("setEntityActive", luaWorldSetEntityActive);
			addMethod("isEntityActive", luaWorldIsEntityActive);
			addMethod("raycast", luaWorldRaycast);
			addMethod("findAllWithTag", luaWorldFindAllWithTag);
			addMethod("findDamageable", luaWorldFindDamageable);
			addMethod("damage", luaWorldDamage);
			addMethod("stun", luaWorldStun);
			addMethod("spawnBeam", luaWorldSpawnBeam);
			addMethod("spawnFlash", luaWorldSpawnFlash);
			addMethod("particle", luaWorldParticle);
			addMethod("heal", luaWorldHeal);
			addMethod("send", luaWorldSend);
			addMethod("spawnText", luaWorldSpawnText);
			addMethod("setHudBar", luaWorldSetHudBar);
			addMethod("clearHudBar", luaWorldClearHudBar);
			addMethod("showMessage", luaWorldShowMessage);
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

		state_ = luaL_newstate();
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
		lua_pushcfunction(state_, luaInputIsMouseButtonDown);
		lua_setfield(state_, -2, "isMouseButtonDown");
		lua_pushcfunction(state_, luaInputGetScrollDelta);
		lua_setfield(state_, -2, "getScrollDelta");
		lua_setglobal(state_, "input");
	}

	void ScriptRuntime::shutdown() noexcept
	{
		if (state_ != nullptr)
		{
			lua_close(state_);
			state_ = nullptr;
		}
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
		gameplayState_ = nullptr;
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
		pushInventoryProxy(state_, entityId);
		lua_setfield(state_, -2, "inventory");

		// @property values (this object's own, else the annotated
		// default) - set before on_start() so the script sees them.
		{
			const SceneEntity* owner = scene_ != nullptr ? scene_->findEntity(entityId) : nullptr;
			for (const ExposedScriptProperty& property : cachedScriptProperties(*fullPath))
			{
				const ScriptPropertyOverride* entry =
					owner != nullptr ? findScriptProperty(*owner, scriptPath, property.name) : nullptr;
				pushPropertyValue(state_, property, entry != nullptr ? entry->value : property.defaultAsText());
				lua_setfield(state_, -2, property.name.c_str());
			}
		}

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
			else if (typeStr == "string" || typeStr == "icon" || typeStr == "image")
			{
				prop.type = typeStr == "icon" ? ExposedScriptProperty::Type::Icon
					: typeStr == "image"      ? ExposedScriptProperty::Type::Image
											  : ExposedScriptProperty::Type::String;
				std::string rest;
				std::getline(iss >> std::ws, rest);
				while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.back())) != 0)
				{
					rest.pop_back();
				}
				if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"')
				{
					rest = rest.substr(1, rest.size() - 2);
				}
				prop.defaultString = rest;
			}
			else if (typeStr == "enum")
			{
				// -- @property weapon enum none|sword|axe sword
				prop.type = ExposedScriptProperty::Type::Enum;
				std::string optionList;
				iss >> optionList;
				std::size_t start = 0;
				while (start <= optionList.size())
				{
					const std::size_t bar = optionList.find('|', start);
					const std::string option =
						optionList.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
					if (!option.empty())
					{
						prop.options.push_back(option);
					}
					if (bar == std::string::npos)
					{
						break;
					}
					start = bar + 1;
				}
				if (prop.options.empty())
				{
					continue;
				}
				if (!(iss >> prop.defaultString) ||
					std::find(prop.options.begin(), prop.options.end(), prop.defaultString) == prop.options.end())
				{
					prop.defaultString = prop.options.front();
				}
			}
			else if (typeStr == "slider")
			{
				// -- @property climb_angle slider 0|360 0   (a number shown as a slider)
				prop.type = ExposedScriptProperty::Type::Slider;
				std::string range;
				iss >> range;
				const std::size_t bar = range.find('|');
				if (bar == std::string::npos)
				{
					continue;
				}
				prop.sliderMin = std::strtof(range.substr(0, bar).c_str(), nullptr);
				prop.sliderMax = std::strtof(range.substr(bar + 1).c_str(), nullptr);
				if (prop.sliderMax <= prop.sliderMin)
				{
					continue;
				}
				prop.defaultNumber = prop.sliderMin;
				iss >> prop.defaultNumber;
				prop.defaultNumber = std::clamp(prop.defaultNumber, prop.sliderMin, prop.sliderMax);
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

	std::string ScriptRuntime::ExposedScriptProperty::defaultAsText() const
	{
		switch (type)
		{
			case Type::Number:
			case Type::Slider:
			{
				std::ostringstream stream;
				stream << defaultNumber;
				return stream.str();
			}
			case Type::Bool:
				return defaultBool ? "true" : "false";
			case Type::Vec3:
			{
				std::ostringstream stream;
				stream << defaultVec3.x << ' ' << defaultVec3.y << ' ' << defaultVec3.z;
				return stream.str();
			}
			default:
				return defaultString;
		}
	}

	const std::vector<ScriptRuntime::ExposedScriptProperty>& ScriptRuntime::cachedScriptProperties(
		const std::filesystem::path& fullScriptPath)
	{
		struct CacheEntry
		{
			std::filesystem::file_time_type writeTime{};
			std::vector<ExposedScriptProperty> properties;
		};
		static std::unordered_map<std::string, CacheEntry> cache;
		std::error_code timeError;
		const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(fullScriptPath, timeError);
		const std::string key = fullScriptPath.generic_string();
		const auto found = cache.find(key);
		if (found != cache.end() && !timeError && found->second.writeTime == writeTime)
		{
			return found->second.properties;
		}
		CacheEntry& entry = cache[key];
		entry.writeTime = timeError ? std::filesystem::file_time_type{} : writeTime;
		entry.properties = parseScriptProperties(fullScriptPath);
		return entry.properties;
	}

	ScriptRuntime::ScriptPresetTag ScriptRuntime::cachedScriptPreset(const std::filesystem::path& fullScriptPath)
	{
		struct CacheEntry
		{
			std::filesystem::file_time_type writeTime{};
			ScriptPresetTag tag;
		};
		static std::unordered_map<std::string, CacheEntry> cache;
		std::error_code timeError;
		const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(fullScriptPath, timeError);
		const std::string key = fullScriptPath.generic_string();
		const auto found = cache.find(key);
		if (found != cache.end() && !timeError && found->second.writeTime == writeTime)
		{
			return found->second.tag;
		}
		ScriptPresetTag tag;
		std::ifstream file(fullScriptPath);
		std::string line;
		while (std::getline(file, line))
		{
			const std::size_t tagPos = line.find("@preset");
			if (tagPos == std::string::npos)
			{
				continue;
			}
			// -- @preset FPS Demo | player
			std::string rest = line.substr(tagPos + 7);
			const std::size_t bar = rest.find('|');
			const auto trim = [](std::string text)
			{
				while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
				{
					text.erase(text.begin());
				}
				while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
				{
					text.pop_back();
				}
				return text;
			};
			tag.name = trim(bar == std::string::npos ? rest : rest.substr(0, bar));
			tag.role = bar == std::string::npos ? std::string("player") : trim(rest.substr(bar + 1));
			break;
		}
		CacheEntry& entry = cache[key];
		entry.writeTime = timeError ? std::filesystem::file_time_type{} : writeTime;
		entry.tag = tag;
		return tag;
	}

	std::string ScriptRuntime::scriptPropertyText(const SceneEntity& entity, const std::string& scriptPath,
		const std::string& propertyName, const std::filesystem::path& projectRoot)
	{
		if (const ScriptPropertyOverride* entry = findScriptProperty(entity, scriptPath, propertyName))
		{
			return entry->value;
		}
		const std::optional<std::filesystem::path> fullPath =
			core::resolveProjectFile(projectRoot, scriptPath, "Game/Scripts", {".lua"});
		if (!fullPath.has_value())
		{
			return {};
		}
		for (const ExposedScriptProperty& property : cachedScriptProperties(*fullPath))
		{
			if (property.name == propertyName)
			{
				return property.defaultAsText();
			}
		}
		return {};
	}

	std::vector<int> ScriptRuntime::instanceRefs(const int entityId) const
	{
		std::vector<int> refs;
		const auto found = instancesByEntity_.find(entityId);
		if (found != instancesByEntity_.end())
		{
			for (const ScriptInstance& instance : found->second)
			{
				refs.push_back(instance.ref);
			}
		}
		return refs;
	}

	void ScriptRuntime::setGameplayState(GameplayState* gameplayState) noexcept
	{
		gameplayState_ = gameplayState;
	}

	GameplayState* ScriptRuntime::gameplayState() const noexcept
	{
		return gameplayState_;
	}

	bool ScriptRuntime::invokeHook(
		const int entityId, const char* functionName, const float number, const std::string& text)
	{
		if (state_ == nullptr)
		{
			return false;
		}
		const auto found = instancesByEntity_.find(entityId);
		if (found == instancesByEntity_.end())
		{
			return false;
		}
		// Copy the refs first so the loop never walks a vector a hook
		// could change underneath it.
		std::vector<int> refs;
		refs.reserve(found->second.size());
		for (const ScriptInstance& instance : found->second)
		{
			refs.push_back(instance.ref);
		}
		return invokeHookOn(state_, *this, refs, functionName, number, text);
	}

	bool ScriptRuntime::hasHook(const int entityId, const char* functionName) const
	{
		if (state_ == nullptr)
		{
			return false;
		}
		const auto found = instancesByEntity_.find(entityId);
		if (found == instancesByEntity_.end())
		{
			return false;
		}
		for (const ScriptInstance& instance : found->second)
		{
			lua_rawgeti(state_, LUA_REGISTRYINDEX, instance.ref);
			lua_getfield(state_, -1, functionName);
			const bool isFunction = lua_isfunction(state_, -1);
			lua_pop(state_, 2);
			if (isFunction)
			{
				return true;
			}
		}
		return false;
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
