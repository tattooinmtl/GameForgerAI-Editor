#include "GameForger/Runtime/GameplayLoop.hpp"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#include <imgui.h>
#include <ImGuizmo.h>

#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "GameForger/Editor/Animation.hpp"
#include "GameForger/Editor/Transform.hpp"

namespace gameforger::editor
{
	// Upper bound on a single frame's deltaTime. A tab-out, breakpoint, or
	// window-drag can produce multi-second `glfwGetTime` deltas that would
	// otherwise tunnel physics objects (gravity = -18 m/s^2, projectile
	// speed = 22 u/s) through castle colliders in a single tick, or
	// overshoot an animation clip's duration. 100ms is enough to absorb a
	// hitch while still clamping pathological cases.
	constexpr float kMaxDeltaSeconds = 0.1F;

	float clampDeltaTime(const float deltaTime) noexcept
	{
		return std::isfinite(deltaTime) ? std::min(deltaTime, kMaxDeltaSeconds) : 0.0F;
	}

	void applyParentConstraints(const EditorScene& scene, AICommandBus& commandBus)
	{
		std::unordered_map<int, glm::mat4> worldMatrixCache;
		std::unordered_set<int> resolving;

		const std::function<glm::mat4(const SceneEntity&)> resolveWorldMatrix =
			[&](const SceneEntity& entity) -> glm::mat4
		{
			const auto cached = worldMatrixCache.find(entity.id);
			if (cached != worldMatrixCache.end())
			{
				return cached->second;
			}

			// A cycle (A's parent chain loops back to A) is treated the
			// same as "no parent" - resolves to this entity's own last
			// known world transform instead of recursing forever.
			const SceneEntity* parent = (entity.parentName.empty() || resolving.count(entity.id) > 0)
				? nullptr
				: scene.findEntity(entity.parentName);
			if (parent == nullptr)
			{
				const glm::mat4 world = composeEntityPivotFrame(entity);
				worldMatrixCache[entity.id] = world;
				return world;
			}

			resolving.insert(entity.id);
			const glm::mat4 parentWorld = resolveWorldMatrix(*parent);
			resolving.erase(entity.id);

			SceneEntity localFrame{};
			localFrame.position = entity.localPosition;
			localFrame.rotationEuler = entity.localRotationEuler;
			localFrame.scale = entity.localScale;
			const glm::mat4 world = parentWorld * composeEntityPivotFrame(localFrame);
			worldMatrixCache[entity.id] = world;
			return world;
		};

		for (const SceneEntity& entity : scene.entities())
		{
			if (entity.parentName.empty())
			{
				continue;
			}
			const glm::mat4 world = resolveWorldMatrix(entity);
			float translation[3];
			float rotation[3];
			float scale[3];
			ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(world), translation, rotation, scale);
			const glm::vec3 newPosition(translation[0], translation[1], translation[2]);
			const glm::vec3 newRotation(rotation[0], rotation[1], rotation[2]);
			const glm::vec3 newScale(scale[0], scale[1], scale[2]);
			if (glm::length(newPosition - entity.position) > 0.0001F ||
				glm::length(newRotation - entity.rotationEuler) > 0.0001F ||
				glm::length(newScale - entity.scale) > 0.0001F)
			{
				commandBus.execute(SetPropertyCommand{entity.name, "Transform", "position", newPosition});
				commandBus.execute(SetPropertyCommand{entity.name, "Transform", "rotation", newRotation});
				commandBus.execute(SetPropertyCommand{entity.name, "Transform", "scale", newScale});
			}
		}
	}

	void tickPlayModeAnimations(
		EditorScene& scene, AICommandBus& commandBus, GameplayState& gameplay, const bool isPlaying,
		const float deltaTime)
	{
		if (!isPlaying)
		{
			return;
		}
		const float dt = clampDeltaTime(deltaTime);
		gameplay.playElapsedTime += dt;

		for (const SceneEntity& entity : scene.entities())
		{
			if (!entity.active || !entity.animation.enabled || entity.animation.keyframes.empty())
			{
				continue;
			}
			const AnimatedPose pose = sampleAnimation(entity.animation, gameplay.playElapsedTime);
			// Parented entities animate in their LOCAL frame - applyParentConstraints
			// (called earlier this frame) composes the world transform from the
			// parent's world * the child's local. Writing the pose directly to
			// the world frame would clobber the parent-derived transform and
			// detach the child visually (turret arms snapping off castles, etc.).
			if (entity.parentName.empty())
			{
				commandBus.execute(SetPropertyCommand{entity.name, "Transform", "position", pose.position});
				commandBus.execute(SetPropertyCommand{entity.name, "Transform", "rotation", pose.rotationEuler});
				commandBus.execute(SetPropertyCommand{entity.name, "Transform", "scale", pose.scale});
			}
			else
			{
				commandBus.execute(SetPropertyCommand{entity.name, "Parent", "localPosition", pose.position});
				commandBus.execute(SetPropertyCommand{entity.name, "Parent", "localRotation", pose.rotationEuler});
				commandBus.execute(SetPropertyCommand{entity.name, "Parent", "localScale", pose.scale});
			}
		}
	}

	void tickScripts(const EditorScene& scene, ScriptRuntime& scriptRuntime, const bool isPlaying, const float deltaTime)
	{
		if (!isPlaying || !scriptRuntime.isRunning())
		{
			return;
		}
		const float dt = clampDeltaTime(deltaTime);
		for (const SceneEntity& entity : scene.entities())
		{
			if (entity.active && !entity.scripts.empty())
			{
				scriptRuntime.updateEntity(entity.id, dt);
			}
		}
	}

	void tickProjectiles(
		const EditorScene& scene, AICommandBus& commandBus, GameplayState& gameplay, const bool isPlaying,
		const float deltaTime)
	{
		if (!isPlaying)
		{
			return;
		}
		const float dt = clampDeltaTime(deltaTime);
		constexpr float kHitRadius = 0.6F;
		std::vector<GameplayState::Projectile> stillActive;
		stillActive.reserve(gameplay.projectiles.size());
		for (GameplayState::Projectile& projectile : gameplay.projectiles)
		{
			if (projectile.useGravity)
			{
				// Same semi-implicit Euler order as the catapult's aim-
				// preview line (main.cpp) - gravity applied before the
				// position step - so the real flight matches the drawn arc.
				projectile.velocity.y += kProjectileGravity * dt;
			}
			projectile.position += projectile.velocity * dt;
			projectile.remainingLifetimeSeconds -= dt;
			if (projectile.remainingLifetimeSeconds <= 0.0F)
			{
				continue;
			}

			bool hit = false;
			if (projectile.useGravity)
			{
				// Real AABB-vs-collider test (same min/max formula as
				// resolveBoxCollision, ScriptRuntime.cpp), gated on
				// hasCollider, not the original sphere-vs-tag check - a
				// boulder needs to actually land inside the castle's
				// hitbox, not just pass near an entity carrying the tag.
				for (const SceneEntity& other : scene.entities())
				{
					if (!other.active || !other.hasCollider)
					{
						continue;
					}
					if (std::find(other.tags.begin(), other.tags.end(), projectile.hitTag) == other.tags.end())
					{
						continue;
					}
					const glm::vec3 boxMin = other.position - other.scale;
					const glm::vec3 boxMax = other.position + other.scale;
					if (projectile.position.x >= boxMin.x && projectile.position.x <= boxMax.x &&
						projectile.position.y >= boxMin.y && projectile.position.y <= boxMax.y &&
						projectile.position.z >= boxMin.z && projectile.position.z <= boxMax.z)
					{
						hit = true;
						if (other.isCastle && gameplay.gameOverMessage.empty())
						{
							const float newHp = std::max(0.0F, other.castle.hp - 25.0F);
							commandBus.execute(SetPropertyCommand{other.name, "Castle", "hp", newHp});
							if (newHp <= 0.0F)
							{
								const bool playerCastleLost =
									std::find(other.tags.begin(), other.tags.end(), "PlayerCastle") !=
									other.tags.end();
								gameplay.gameOverMessage = playerCastleLost
									? "You lose - your castle was destroyed."
									: "You win - enemy castle destroyed.";
							}
						}
						break;
					}
				}
			}
			else
			{
				// Original behavior, unchanged: nearest-entity-carrying-tag
				// sphere check, purely visual despawn, no HP/collider
				// involvement - still used by self.world:fireProjectile
				// (enemy_ai/ranged_attacker).
				for (const SceneEntity& other : scene.entities())
				{
					if (std::find(other.tags.begin(), other.tags.end(), projectile.hitTag) == other.tags.end())
					{
						continue;
					}
					if (glm::length(other.position - projectile.position) <= kHitRadius)
					{
						hit = true;
						break;
					}
				}
			}

			if (!hit)
			{
				stillActive.push_back(projectile);
			}
		}
		gameplay.projectiles = std::move(stillActive);
	}
}
