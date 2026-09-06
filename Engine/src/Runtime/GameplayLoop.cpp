#include "GameForger/Runtime/GameplayLoop.hpp"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#include <imgui.h>
#include <ImGuizmo.h>

#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "GameForger/Editor/Animation.hpp"
#include "GameForger/Editor/Collision.hpp"
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

	void executeOrLog(AICommandBus& commandBus, const AIEditorCommand& command)
	{
		const AICommandResult result = commandBus.execute(command);
		if (!result.success)
		{
			std::fprintf(stderr, "GameplayLoop command failed: %s\n", result.message.c_str());
		}
	}

	void applyParentConstraints(const EditorScene& scene, AICommandBus& commandBus)
	{
		std::unordered_map<int, glm::mat4> worldMatrixCache;
		std::unordered_set<int> resolving;
		constexpr int kMaxParentDepth = 64;

		const std::function<glm::mat4(const SceneEntity&, int)> resolveWorldMatrix =
			[&](const SceneEntity& entity, const int depth) -> glm::mat4
		{
			const auto cached = worldMatrixCache.find(entity.id);
			if (cached != worldMatrixCache.end())
			{
				return cached->second;
			}

			// A cycle (A's parent chain loops back to A) is treated the
			// same as "no parent" - resolves to this entity's own last
			// known world transform instead of recursing forever.
			const SceneEntity* parent =
				(entity.parentName.empty() || resolving.count(entity.id) > 0 || depth >= kMaxParentDepth)
				? nullptr
				: scene.findEntity(entity.parentName);
			if (parent == nullptr)
			{
				const glm::mat4 world = composeEntityPivotFrame(entity);
				worldMatrixCache[entity.id] = world;
				return world;
			}

			resolving.insert(entity.id);
			const glm::mat4 parentWorld = resolveWorldMatrix(*parent, depth + 1);
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
			const glm::mat4 world = resolveWorldMatrix(entity, 0);
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
				executeOrLog(commandBus, SetPropertyCommand{entity.name, "Transform", "position", newPosition});
				executeOrLog(commandBus, SetPropertyCommand{entity.name, "Transform", "rotation", newRotation});
				executeOrLog(commandBus, SetPropertyCommand{entity.name, "Transform", "scale", newScale});
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
				executeOrLog(commandBus, SetPropertyCommand{entity.name, "Transform", "position", pose.position});
				executeOrLog(commandBus, SetPropertyCommand{entity.name, "Transform", "rotation", pose.rotationEuler});
				executeOrLog(commandBus, SetPropertyCommand{entity.name, "Transform", "scale", pose.scale});
			}
			else
			{
				executeOrLog(commandBus, SetPropertyCommand{entity.name, "Parent", "localPosition", pose.position});
				executeOrLog(commandBus, SetPropertyCommand{entity.name, "Parent", "localRotation", pose.rotationEuler});
				executeOrLog(commandBus, SetPropertyCommand{entity.name, "Parent", "localScale", pose.scale});
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
		gameplay.projectilesHitThisTick = 0;
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
				// AABB-vs-collider test (colliderWorldAabb, Collision.cpp),
				// gated on hasCollider. Imported castles use the mesh bounds,
				// not the Transform Scale 2x2x2 box.
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
					const MeshCollisionGeometry* mesh =
						other.isImportedMesh ? importedMeshCollision(scene, other) : nullptr;
					const ColliderAabb box = colliderWorldAabb(other, mesh);
					if (projectile.position.x >= box.min.x && projectile.position.x <= box.max.x &&
						projectile.position.y >= box.min.y && projectile.position.y <= box.max.y &&
						projectile.position.z >= box.min.z && projectile.position.z <= box.max.z)
					{
						hit = true;
						if (other.isCastle && gameplay.gameOverMessage.empty())
						{
							const float newHp = std::max(0.0F, other.castle.hp - 25.0F);
							executeOrLog(commandBus, SetPropertyCommand{other.name, "Castle", "hp", newHp});
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
			else
			{
				++gameplay.projectilesHitThisTick;
			}
		}
		gameplay.projectiles = std::move(stillActive);
	}

	// ------------------------------------------------------------------------
	// Boot sequence (ProjectSettings::bootSequence)
	// ------------------------------------------------------------------------
	namespace
	{
		// How long a play_animation step waits: the target entity's own last
		// keyframe time. An entity with no animation finishes immediately
		// rather than stalling the whole sequence forever.
		float animationDurationSeconds(const EditorScene& scene, const std::string& entityName)
		{
			const SceneEntity* entity = scene.findEntity(entityName);
			if (entity == nullptr || entity->animation.keyframes.empty())
			{
				return 0.0F;
			}
			return entity->animation.keyframes.back().time;
		}
	}

	void resetBootSequence(GameplayState& gameplay, const std::vector<BootStep>& steps)
	{
		gameplay.bootSequence = GameplayState::BootSequenceState{};
		if (steps.empty())
		{
			// No sequence authored: behave exactly as before this feature
			// existed - the player has control from the first frame.
			return;
		}
		gameplay.bootSequence.running = true;
		gameplay.bootSequence.playerInputLocked = true;
	}

	bool bootSequenceBlocksInput(const GameplayState& gameplay) noexcept
	{
		return gameplay.bootSequence.running && gameplay.bootSequence.playerInputLocked;
	}

	void tickBootSequence(
		const std::vector<BootStep>& steps,
		const EditorScene& scene,
		GameplayState& gameplay,
		const bool isPlaying,
		const float deltaTime)
	{
		GameplayState::BootSequenceState& boot = gameplay.bootSequence;
		if (!isPlaying || !boot.running)
		{
			return;
		}
		// The sequence can outlive the settings it was armed from (the user
		// deleting steps mid-Play), so re-check the bound every tick rather
		// than trusting stepIndex.
		if (boot.stepIndex >= steps.size())
		{
			boot.running = false;
			boot.playerInputLocked = false;
			return;
		}

		const BootStep& step = steps[boot.stepIndex];
		boot.stepElapsedSeconds += deltaTime;

		bool stepComplete = false;
		switch (step.kind)
		{
			case BootStep::Kind::WaitSeconds:
				stepComplete = boot.stepElapsedSeconds >= step.seconds;
				break;

			case BootStep::Kind::PlayAnimation:
				// The animation itself is driven by tickPlayModeAnimations,
				// which runs regardless of the input lock - this step only
				// holds the sequence for as long as the clip lasts.
				stepComplete = boot.stepElapsedSeconds >= animationDurationSeconds(scene, step.targetEntity);
				break;

			case BootStep::Kind::PlayCutscene:
				// Hand the request to the host on the first tick of this step
				// and wait for it to report back, so a shot of any length
				// gates correctly instead of guessing a duration.
				if (boot.requestedCutsceneShot.empty() && !boot.hostStepFinished)
				{
					boot.requestedCutsceneShot = step.shotName;
				}
				stepComplete = boot.hostStepFinished;
				break;

			case BootStep::Kind::PlayAudio:
				if (boot.requestedAudioClip.empty() && !boot.hostStepFinished)
				{
					boot.requestedAudioClip = step.clipPath;
				}
				stepComplete = boot.hostStepFinished;
				break;

			case BootStep::Kind::LockPlayerInput:
				boot.playerInputLocked = true;
				stepComplete = true;
				break;

			case BootStep::Kind::UnlockPlayerInput:
				// Hands control back early - the rest of the sequence keeps
				// running (a logo can finish while the player already walks).
				boot.playerInputLocked = false;
				stepComplete = true;
				break;
		}

		if (!stepComplete)
		{
			return;
		}

		boot.stepElapsedSeconds = 0.0F;
		boot.requestedCutsceneShot.clear();
		boot.requestedAudioClip.clear();
		boot.hostStepFinished = false;
		++boot.stepIndex;

		if (boot.stepIndex >= steps.size())
		{
			boot.running = false;
			boot.playerInputLocked = false;
		}
	}
}
