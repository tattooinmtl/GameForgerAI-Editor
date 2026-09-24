#include "GameForger/Runtime/GameplayLoop.hpp"

#include <algorithm>
#include <cmath>
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
			if (!entity.scripts.empty() && scene.isActiveInHierarchy(entity))
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
				// Real point-in-collider test (the same turned box as
				// resolveBoxCollision, ScriptRuntime.cpp - colliderBox),
				// gated on hasCollider, not the original sphere-vs-tag
				// check - a boulder needs to actually land inside the
				// castle's hitbox, not just pass near an entity carrying
				// the tag.
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
					if (colliderBox(other).contains(projectile.position))
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
					if (!other.active ||
						std::find(other.tags.begin(), other.tags.end(), projectile.hitTag) == other.tags.end())
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

	void tickEffects(GameplayState& gameplay, const bool isPlaying, const float deltaTime)
	{
		if (!isPlaying)
		{
			return;
		}
		const float dt = clampDeltaTime(deltaTime);
		for (GameplayState::Beam& beam : gameplay.beams)
		{
			beam.remainingSeconds -= dt;
		}
		for (GameplayState::Flash& flash : gameplay.flashes)
		{
			if (flash.particle)
			{
				// Spawned this frame -> survives this tick, gone the next.
				flash.remainingSeconds = flash.framesLeft-- > 0 ? 1.0F : 0.0F;
			}
			else
			{
				flash.remainingSeconds -= dt;
			}
		}
		std::erase_if(gameplay.beams, [](const GameplayState::Beam& beam) { return beam.remainingSeconds <= 0.0F; });
		std::erase_if(
			gameplay.flashes, [](const GameplayState::Flash& flash) { return flash.remainingSeconds <= 0.0F; });
		for (GameplayState::FloatingText& text : gameplay.floatingTexts)
		{
			text.remainingSeconds -= dt;
			text.position.y += 0.9F * dt; // drift upward
		}
		std::erase_if(gameplay.floatingTexts,
			[](const GameplayState::FloatingText& text) { return text.remainingSeconds <= 0.0F; });
		if (gameplay.messageSecondsRemaining > 0.0F)
		{
			gameplay.messageSecondsRemaining = std::max(0.0F, gameplay.messageSecondsRemaining - dt);
		}
	}

	void ensureInventorySlots(GameplayState& gameplay)
	{
		// Older code/saves kept only non-empty entries - compact those to
		// the front first so nothing is lost, then pad with empty slots.
		std::vector<GameplayState::InventoryItem> slots;
		slots.reserve(kInventorySlotCount);
		for (GameplayState::InventoryItem& item : gameplay.inventoryItems)
		{
			if (!item.empty() && static_cast<int>(slots.size()) < kInventorySlotCount)
			{
				slots.push_back(std::move(item));
			}
		}
		slots.resize(kInventorySlotCount);
		gameplay.inventoryItems = std::move(slots);
		gameplay.selectedSlot = std::clamp(gameplay.selectedSlot, 0, kInventorySlotCount - 1);
	}

	int addInventoryItem(GameplayState& gameplay, GameplayState::InventoryItem item)
	{
		if (static_cast<int>(gameplay.inventoryItems.size()) != kInventorySlotCount)
		{
			ensureInventorySlots(gameplay);
		}
		if (item.count <= 0)
		{
			item.count = 1;
		}
		auto& slots = gameplay.inventoryItems;
		if (item.stackable)
		{
			for (int index = 0; index < static_cast<int>(slots.size()); ++index)
			{
				GameplayState::InventoryItem& slot = slots[static_cast<std::size_t>(index)];
				if (!slot.empty() && slot.stackable && slot.itemName == item.itemName &&
					slot.count + item.count <= std::max(1, slot.maxStack))
				{
					slot.count += item.count;
					for (std::string& stored : item.storedEntityNames)
					{
						slot.storedEntityNames.push_back(std::move(stored));
					}
					return index;
				}
			}
		}
		for (int index = 0; index < static_cast<int>(slots.size()); ++index)
		{
			if (slots[static_cast<std::size_t>(index)].empty())
			{
				slots[static_cast<std::size_t>(index)] = std::move(item);
				return index;
			}
		}
		return -1;
	}

	bool entityProvidesInventory(const SceneEntity& entity, const ScriptRuntime& scriptRuntime)
	{
		for (const std::string& scriptPath : entity.scripts)
		{
			if (scriptPath == "Game/Scripts/inventory_system.lua" ||
				scriptRuntime.getScriptBoolField(entity.id, scriptPath, "inventory_enabled", false))
			{
				return true;
			}
		}
		return false;
	}

	const SceneEntity* findPickupItemCandidate(
		const EditorScene& scene, const glm::vec3& origin, const float horizontalRange, const float heightTolerance,
		const int excludeId)
	{
		const SceneEntity* best = nullptr;
		float bestDistanceSquared = horizontalRange * horizontalRange;
		for (const SceneEntity& other : scene.entities())
		{
			if (other.id == excludeId || !(other.isPickupItem || hasScript(other, kItemScriptPath)))
			{
				continue;
			}
			if (!scene.isActiveInHierarchy(other) || std::abs(other.position.y - origin.y) > heightTolerance)
			{
				continue;
			}
			const float deltaX = other.position.x - origin.x;
			const float deltaZ = other.position.z - origin.z;
			const float distanceSquared = deltaX * deltaX + deltaZ * deltaZ;
			if (distanceSquared > bestDistanceSquared)
			{
				continue;
			}
			best = &other;
			bestDistanceSquared = distanceSquared;
		}
		return best;
	}

	namespace
	{
		// items.lua's live field, falling back to the object's saved
		// override (the script may not be running, e.g. outside Play).
		std::string itemScriptString(
			const SceneEntity& entity, const ScriptRuntime& scriptRuntime, const char* field,
			const std::string& fallback)
		{
			std::string stored = fallback;
			if (const ScriptPropertyOverride* entry = findScriptProperty(entity, kItemScriptPath, field))
			{
				stored = entry->value;
			}
			return scriptRuntime.getScriptStringField(entity.id, kItemScriptPath, field, stored);
		}

		// Only the root's own flag changes - children follow through
		// EditorScene::isActiveInHierarchy and keep their own flags.
		void setEntityActive(const EditorScene& scene, AICommandBus& commandBus, const std::string& name, bool active)
		{
			if (scene.findEntity(name) != nullptr)
			{
				(void)commandBus.execute(SetPropertyCommand{name, "Entity", "active", active});
			}
		}
	}

	std::string pickupDisplayName(const SceneEntity& entity, const ScriptRuntime& scriptRuntime)
	{
		if (hasScript(entity, kItemScriptPath))
		{
			const std::string name = itemScriptString(entity, scriptRuntime, "item_name", "");
			return name.empty() ? entity.name : name;
		}
		return entity.pickupItem.itemName;
	}

	bool pickUpItem(
		EditorScene& scene, AICommandBus& commandBus, GameplayState& gameplay, const ScriptRuntime& scriptRuntime,
		const SceneEntity& candidate)
	{
		GameplayState::InventoryItem item;
		item.count = 1;
		const std::string entityName = candidate.name;
		if (hasScript(candidate, kItemScriptPath))
		{
			item.itemName = pickupDisplayName(candidate, scriptRuntime);
			item.iconPath = itemScriptString(candidate, scriptRuntime, "icon", "");
			item.itemType = itemScriptString(candidate, scriptRuntime, "item_type", "misc");
			item.weapon = itemScriptString(candidate, scriptRuntime, "weapon", "none");
			if (item.weapon == "none")
			{
				item.weapon.clear();
			}
			item.stackable = scriptRuntime.getScriptBoolField(candidate.id, kItemScriptPath, "stackable", false);
			item.maxStack = static_cast<int>(
				scriptRuntime.getScriptNumberField(candidate.id, kItemScriptPath, "max_stack", 99.0F));
			item.storedEntityNames.push_back(entityName);
			if (addInventoryItem(gameplay, std::move(item)) < 0)
			{
				return false;
			}
			// Unparent so it can be dropped anywhere later, then hide.
			if (!candidate.parentName.empty())
			{
				(void)commandBus.execute(SetPropertyCommand{entityName, "Parent", "parentName", std::string()});
			}
			setEntityActive(scene, commandBus, entityName, false);
			return true;
		}

		item.itemName = candidate.pickupItem.itemName;
		item.iconPath = candidate.pickupItem.iconPath;
		item.itemType = "misc";
		item.scale = candidate.scale;
		item.color = candidate.color;
		item.materialBlendWeight = candidate.materialBlendWeight;
		item.materialLayers = candidate.materialLayers;
		item.materialUvScale = candidate.materialUvScale;
		if (addInventoryItem(gameplay, std::move(item)) < 0)
		{
			return false;
		}
		(void)commandBus.execute(DeleteEntityCommand{entityName});
		return true;
	}

	bool dropInventoryItem(
		EditorScene& scene, AICommandBus& commandBus, GameplayState& gameplay, const int slotIndex,
		const glm::vec3& dropPosition, const float yawDegrees)
	{
		if (slotIndex < 0 || slotIndex >= static_cast<int>(gameplay.inventoryItems.size()))
		{
			return false;
		}
		GameplayState::InventoryItem& item = gameplay.inventoryItems[static_cast<std::size_t>(slotIndex)];
		if (item.empty())
		{
			return false;
		}

		if (!item.storedEntityNames.empty())
		{
			const std::string entityName = item.storedEntityNames.back();
			item.storedEntityNames.pop_back();
			if (scene.findEntity(entityName) != nullptr)
			{
				(void)commandBus.execute(SetPropertyCommand{entityName, "Transform", "position", dropPosition});
				(void)commandBus.execute(
					SetPropertyCommand{entityName, "Transform", "rotation", glm::vec3(0.0F, yawDegrees, 0.0F)});
				setEntityActive(scene, commandBus, entityName, true);
			}
		}
		else
		{
			// Legacy pickup: re-spawn a cube carrying the captured look.
			const std::string baseName = item.itemName.empty() ? std::string("Item") : item.itemName;
			std::string spawnedName = baseName;
			for (int suffix = 1; scene.findEntity(spawnedName) != nullptr; ++suffix)
			{
				spawnedName = baseName + " (" + std::to_string(suffix) + ")";
			}
			(void)commandBus.execute(CreateEntityCommand{spawnedName, PrimitiveType::Cube, dropPosition});
			(void)commandBus.execute(SetPropertyCommand{spawnedName, "Transform", "scale", item.scale});
			(void)commandBus.execute(SetPropertyCommand{spawnedName, "Renderer", "color", item.color});
			(void)commandBus.execute(SetPropertyCommand{spawnedName, "PickupItem", "enabled", true});
			(void)commandBus.execute(SetPropertyCommand{spawnedName, "PickupItem", "itemName", item.itemName});
			(void)commandBus.execute(SetPropertyCommand{spawnedName, "PickupItem", "iconPath", item.iconPath});
			if (const SceneEntity* spawned = scene.findEntity(spawnedName))
			{
				if (SceneEntity* mutableSpawned = scene.findEntityMutable(spawned->id))
				{
					mutableSpawned->materialBlendWeight = item.materialBlendWeight;
					mutableSpawned->materialLayers = item.materialLayers;
					mutableSpawned->materialUvScale = item.materialUvScale;
				}
			}
		}

		item.count -= 1;
		if (item.count <= 0)
		{
			item = GameplayState::InventoryItem{};
		}
		return true;
	}
}
