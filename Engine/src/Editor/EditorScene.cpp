#include "GameForger/Editor/EditorScene.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <system_error>
#include <type_traits>

#include "GameForger/Core/ProjectPaths.hpp"

namespace gameforger::editor
{
	EditorScene::EditorScene(std::filesystem::path projectRoot)
		: projectRoot_(std::filesystem::weakly_canonical(std::move(projectRoot)))
	{
	}

	AICommandResult EditorScene::execute(const AIEditorCommand& command)
	{
		return std::visit(
			[this](const auto& value) -> AICommandResult
			{
				using Command = std::decay_t<decltype(value)>;
				if constexpr (std::is_same_v<Command, CreateEntityCommand>)
				{
					const std::string name = value.name.empty() ? makeUniqueName("Entity") : value.name;
					if (nameInUse(name))
					{
						return {false, false, "An entity with this name already exists."};
					}
					SceneEntity entity;
					entity.id = nextEntityId_++;
					entity.name = name;
					entity.primitive = value.primitive;
					entity.position = value.position;
					entities_.push_back(entity);
					return {true, false, "Entity created in the editor scene."};
				}
				else if constexpr (std::is_same_v<Command, DeleteEntityCommand>)
				{
					const auto iterator = std::find_if(
						entities_.begin(),
						entities_.end(),
						[&value](const SceneEntity& entity) { return entity.name == value.entityName; });
					if (iterator == entities_.end())
					{
						return {false, false, "Entity was not found in the editor scene."};
					}
					entities_.erase(iterator);
					return {true, false, "Entity deleted from the editor scene."};
				}
				else if constexpr (std::is_same_v<Command, RenameEntityCommand>)
				{
					if (value.newName.empty())
					{
						return {false, false, "The new name cannot be empty."};
					}
					if (value.entityName != value.newName && nameInUse(value.newName))
					{
						return {false, false, "An entity with this name already exists."};
					}
					SceneEntity* entity = findEntityMutable(value.entityName);
					if (entity == nullptr)
					{
						return {false, false, "Entity was not found in the editor scene."};
					}
					const std::string oldName = entity->name;
					entity->name = value.newName;
					// Parent links and catapult references are by name -
					// carry them over so renaming a parent doesn't orphan
					// its children.
					for (SceneEntity& other : entities_)
					{
						if (other.parentName == oldName)
						{
							other.parentName = value.newName;
						}
						if (other.catapult.yawEntityName == oldName)
						{
							other.catapult.yawEntityName = value.newName;
						}
						if (other.catapult.armEntityName == oldName)
						{
							other.catapult.armEntityName = value.newName;
						}
					}
					return {true, false, "Entity renamed."};
				}
				else if constexpr (std::is_same_v<Command, DuplicateEntityCommand>)
				{
					const SceneEntity* source = findEntity(value.entityName);
					if (source == nullptr)
					{
						return {false, false, "Entity was not found in the editor scene."};
					}
					SceneEntity copy = *source;
					copy.id = nextEntityId_++;
					copy.name = makeUniqueName(source->name);
					copy.position += glm::vec3(0.5F, 0.0F, 0.5F);
					entities_.push_back(copy);
					return {true, false, "Entity duplicated."};
				}
				else if constexpr (std::is_same_v<Command, SetPositionCommand>)
				{
					SceneEntity* entity = findEntityMutable(value.entityName);
					if (entity == nullptr)
					{
						return {false, false, "Entity was not found in the editor scene."};
					}
					entity->position = value.position;
					return {true, false, "Entity position updated."};
				}
				else if constexpr (std::is_same_v<Command, SetAnimationCommand>)
				{
					SceneEntity* entity = findEntityMutable(value.entityName);
					if (entity == nullptr)
					{
						return {false, false, "Entity was not found in the editor scene."};
					}
					entity->animation.enabled = value.enabled;
					entity->animation.looping = value.looping;
					entity->animation.keyframes = value.keyframes;
					return {true, false, "Entity animation updated."};
				}
				else if constexpr (std::is_same_v<Command, CreateScriptCommand>)
				{
					const std::optional<std::filesystem::path> scriptPath = core::resolveProjectFile(
						projectRoot_, value.path, "Game/Scripts", {".lua"});
					if (!scriptPath.has_value())
					{
						return {false, false, "Script path must be a .lua file inside Game/Scripts."};
					}
					std::error_code error;
					std::filesystem::create_directories(scriptPath->parent_path(), error);
					if (error)
					{
						return {false, false, "Could not create the script directory."};
					}
					std::error_code existenceError;
					const bool exists = std::filesystem::exists(*scriptPath, existenceError);
					if (existenceError)
					{
						return {false, false, "Could not check whether the script already exists."};
					}
					if (exists && !value.overwrite)
					{
						return {false, false,
							"Script already exists at " + scriptPath->string() +
								" - set overwrite=true to replace it."};
					}
					std::ofstream output(*scriptPath, std::ios::binary | std::ios::trunc);
					if (!output)
					{
						return {false, false, "Could not write the script file."};
					}
					output << value.content;
					if (!output)
					{
						return {false, false, "Failed while writing the script file."};
					}
					output.flush();
					return {true, false, "Lua script created in the project."};
				}
				else if constexpr (std::is_same_v<Command, AttachScriptCommand>)
				{
					SceneEntity* entity = findEntityMutable(value.entityName);
					if (entity == nullptr)
					{
						return {false, false, "Entity was not found in the editor scene."};
					}
					const std::optional<std::filesystem::path> scriptPath = core::resolveProjectFile(
						projectRoot_, value.scriptPath, "Game/Scripts", {".lua"});
					if (!scriptPath.has_value())
					{
						return {false, false, "Script path must be a .lua file inside Game/Scripts."};
					}
					if (!std::filesystem::exists(*scriptPath))
					{
						return {false, false, "Script file does not exist."};
					}
					if (std::find(entity->scripts.begin(), entity->scripts.end(), value.scriptPath) !=
						entity->scripts.end())
					{
						return {false, false, "This script is already attached."};
					}
					entity->scripts.push_back(value.scriptPath);
					return {true, false, "Script attached to entity."};
				}
				else if constexpr (std::is_same_v<Command, DetachScriptCommand>)
				{
					SceneEntity* entity = findEntityMutable(value.entityName);
					if (entity == nullptr)
					{
						return {false, false, "Entity was not found in the editor scene."};
					}
					const auto iterator = std::find(entity->scripts.begin(), entity->scripts.end(), value.scriptPath);
					if (iterator == entity->scripts.end())
					{
						return {false, false, "Script is not attached to this entity."};
					}
					entity->scripts.erase(iterator);
					return {true, false, "Script detached from entity."};
				}
				else if constexpr (std::is_same_v<Command, AddTagCommand>)
				{
					SceneEntity* entity = findEntityMutable(value.entityName);
					if (entity == nullptr)
					{
						return {false, false, "Entity was not found in the editor scene."};
					}
					if (value.tag.empty())
					{
						return {false, false, "Tag cannot be empty."};
					}
					if (std::find(entity->tags.begin(), entity->tags.end(), value.tag) != entity->tags.end())
					{
						return {false, false, "This tag is already on the entity."};
					}
					entity->tags.push_back(value.tag);
					if (value.tag == "Ground")
					{
						// Ground/stairs/platforms should collide with
						// self.physics:resolve() scripts without a separate
						// manual step.
						entity->hasCollider = true;
					}
					return {true, false, "Tag added to entity."};
				}
				else if constexpr (std::is_same_v<Command, RemoveTagCommand>)
				{
					SceneEntity* entity = findEntityMutable(value.entityName);
					if (entity == nullptr)
					{
						return {false, false, "Entity was not found in the editor scene."};
					}
					const auto iterator = std::find(entity->tags.begin(), entity->tags.end(), value.tag);
					if (iterator == entity->tags.end())
					{
						return {false, false, "Entity does not have this tag."};
					}
					entity->tags.erase(iterator);
					return {true, false, "Tag removed from entity."};
				}
				else if constexpr (std::is_same_v<Command, CreateTerrainCommand>)
				{
					if (value.resolution < 2)
					{
						return {false, false, "Terrain resolution must be at least 2."};
					}
					if (value.worldSize <= 0.0F)
					{
						return {false, false, "Terrain world size must be positive."};
					}
					const std::string name = value.name.empty() ? makeUniqueName("Terrain") : value.name;
					if (nameInUse(name))
					{
						return {false, false, "An entity with this name already exists."};
					}
					SceneEntity entity;
					entity.id = nextEntityId_++;
					entity.name = name;
					entity.position = value.position;
					entity.isTerrain = true;
					entity.terrain.resolution = value.resolution;
					entity.terrain.worldSize = value.worldSize;
					entity.terrain.heightScale = value.heightScale > 0.0F ? value.heightScale : 12.0F;
					// 0.5, not 0.0 - heights[] treats 0.5 as the flat baseline
					// (see Terrain.cpp's positionAt), so a fresh terrain has
					// equal room to sculpt hills up or canyons/lakes down.
					entity.terrain.heights.assign(
						static_cast<std::size_t>(value.resolution) * static_cast<std::size_t>(value.resolution),
						0.5F);
					if (value.shape == "mountain")
					{
						// Radial peak centered in the worldSize x worldSize
						// footprint, cresting at heights[]=1.0 (world
						// +heightScale/2) at the center and smoothstep-tapering
						// to the 0.5 baseline at the inscribed circle's edge -
						// i.e. an actual sculpted landform, not a primitive
						// cone standing in for one.
						const int resolution = value.resolution;
						const float half = entity.terrain.worldSize * 0.5F;
						const float radius = half;
						for (int row = 0; row < resolution; ++row)
						{
							for (int col = 0; col < resolution; ++col)
							{
								const float worldX = (static_cast<float>(col) /
										static_cast<float>(resolution - 1)) *
										entity.terrain.worldSize -
									half;
								const float worldZ = (static_cast<float>(row) /
										static_cast<float>(resolution - 1)) *
										entity.terrain.worldSize -
									half;
								const float distance = std::sqrt(worldX * worldX + worldZ * worldZ);
								const float t = std::clamp(1.0F - distance / radius, 0.0F, 1.0F);
								const float smooth = t * t * (3.0F - 2.0F * t);
								entity.terrain.heights[static_cast<std::size_t>(row) *
										static_cast<std::size_t>(resolution) +
									static_cast<std::size_t>(col)] = 0.5F + smooth * 0.5F;
							}
						}
					}
					// Default splat layers: base ground, a secondary blend, and a
					// paintable path detail on top - matches the built-in
					// TEX_PACK_01 asset set. Falls back to the entity's own flat
					// `color` wherever a diffuse texture doesn't actually resolve
					// (same graceful-degradation behavior the old single
					// texturePath field had), so this is safe even if the pack
					// is ever removed/renamed.
					entity.terrain.layers[0].diffusePath = "Game/Textures/TEX_PACK_01/SAND_GROUND.jpg";
					entity.terrain.layers[1].diffusePath = "Game/Textures/TEX_PACK_01/rock5.png";
					entity.terrain.layers[2].diffusePath = "Game/Textures/TEX_PACK_01/EARTH_PATH.png";
					entity.terrain.splatWeights.assign(
						static_cast<std::size_t>(value.resolution) * static_cast<std::size_t>(value.resolution) * 3,
						0.0F);
					for (std::size_t texel = 0; texel < entity.terrain.splatWeights.size(); texel += 3)
					{
						entity.terrain.splatWeights[texel] = 1.0F; // full weight on layer 0 everywhere
					}
					entities_.push_back(entity);
					return {true, false, "Terrain created in the editor scene."};
				}
				else if constexpr (std::is_same_v<Command, CreateImportedMeshCommand>)
				{
					if (value.sourcePath.empty())
					{
						return {false, false, "Model source path cannot be empty."};
					}
					const std::filesystem::path modelPath = projectRoot_ / value.sourcePath;
					if (!std::filesystem::exists(modelPath))
					{
						return {false, false, "Model file does not exist."};
					}
					const std::string name = value.name.empty() ? makeUniqueName("Model") : value.name;
					if (nameInUse(name))
					{
						return {false, false, "An entity with this name already exists."};
					}
					SceneEntity entity;
					entity.id = nextEntityId_++;
					entity.name = name;
					entity.position = value.position;
					entity.isImportedMesh = true;
					entity.importedMesh.sourcePath = value.sourcePath;
					entities_.push_back(entity);
					return {true, false, "Model imported into the editor scene."};
				}
				else if constexpr (std::is_same_v<Command, CreateTextMeshCommand>)
				{
					if (value.content.empty())
					{
						return {false, false, "Text content cannot be empty."};
					}
					const std::filesystem::path fontPath = projectRoot_ / value.fontPath;
					if (!std::filesystem::exists(fontPath))
					{
						return {false, false, "Font file does not exist."};
					}
					const std::string name = value.name.empty() ? makeUniqueName("Text") : value.name;
					if (nameInUse(name))
					{
						return {false, false, "An entity with this name already exists."};
					}
					SceneEntity entity;
					entity.id = nextEntityId_++;
					entity.name = name;
					entity.position = value.position;
					entity.isTextMesh = true;
					entity.textMesh.content = value.content;
					entity.textMesh.fontPath = value.fontPath;
					entity.textMesh.fontSize = value.fontSize > 0.0F ? value.fontSize : 1.0F;
					entity.textMesh.depth = value.depth > 0.0F ? value.depth : 0.2F;
					entities_.push_back(entity);
					return {true, false, "Text mesh created in the editor scene."};
				}
				else if constexpr (std::is_same_v<Command, SetPropertyCommand>)
				{
					SceneEntity* entity = findEntityMutable(value.entityName);
					if (entity == nullptr)
					{
						return {false, false, "Entity was not found in the editor scene."};
					}
					if (value.component == "Transform" && value.property == "position")
					{
						if (const auto* position = std::get_if<glm::vec3>(&value.value))
						{
							entity->position = *position;
							return {true, false, "Entity position updated."};
						}
					}
					else if (value.component == "Transform" && value.property == "rotation")
					{
						if (const auto* rotation = std::get_if<glm::vec3>(&value.value))
						{
							entity->rotationEuler = *rotation;
							return {true, false, "Entity rotation updated."};
						}
					}
					else if (value.component == "Transform" && value.property == "scale")
					{
						if (const auto* scale = std::get_if<glm::vec3>(&value.value))
						{
							entity->scale = *scale;
							return {true, false, "Entity scale updated."};
						}
					}
					else if (value.component == "Renderer" && value.property == "color")
					{
						if (const auto* color = std::get_if<glm::vec3>(&value.value))
						{
							entity->color = *color;
							return {true, false, "Entity color updated."};
						}
					}
					else if (value.component == "Transform" && value.property == "pivot")
					{
						if (const auto* pivot = std::get_if<glm::vec3>(&value.value))
						{
							entity->pivotOffset = *pivot;
							return {true, false, "Entity pivot updated."};
						}
					}
					else if (value.component == "Parent" && value.property == "parentName")
				{
					if (const auto* text = std::get_if<std::string>(&value.value))
					{
						entity->parentName = *text;
						return {true, false, "Parent updated."};
					}
				}
				else if (value.component == "Parent" && value.property == "localPosition")
				{
					if (const auto* localPosition = std::get_if<glm::vec3>(&value.value))
					{
						entity->localPosition = *localPosition;
						return {true, false, "Local position updated."};
					}
				}
				else if (value.component == "Parent" && value.property == "localRotation")
				{
					if (const auto* localRotation = std::get_if<glm::vec3>(&value.value))
					{
						entity->localRotationEuler = *localRotation;
						return {true, false, "Local rotation updated."};
					}
				}
				else if (value.component == "Parent" && value.property == "localScale")
				{
					if (const auto* localScale = std::get_if<glm::vec3>(&value.value))
					{
						entity->localScale = *localScale;
						return {true, false, "Local scale updated."};
					}
				}
				else if (value.component == "Entity" && value.property == "active")
				{
					if (const auto* enabled = std::get_if<bool>(&value.value))
					{
						entity->active = *enabled;
						return {true, false, "Entity active flag updated."};
					}
				}
				else if (value.component == "ScriptProperty" || value.component == "ScriptPropertyReset")
				{
					// property = "<scriptPath>#<propertyName>"
					const std::size_t separator = value.property.rfind('#');
					if (separator == std::string::npos || separator == 0 ||
						separator + 1 >= value.property.size())
					{
						return {false, false, "Script property must be named \"<script>#<property>\"."};
					}
					const std::string scriptPath = value.property.substr(0, separator);
					const std::string propertyName = value.property.substr(separator + 1);
					auto& overrides = entity->scriptProperties;
					const auto existing = std::find_if(
						overrides.begin(), overrides.end(),
						[&](const ScriptPropertyOverride& entry)
						{ return entry.scriptPath == scriptPath && entry.name == propertyName; });
					if (value.component == "ScriptPropertyReset")
					{
						if (existing != overrides.end())
						{
							overrides.erase(existing);
						}
						return {true, false, "Script property reset to its default."};
					}
					const auto* text = std::get_if<std::string>(&value.value);
					if (text == nullptr)
					{
						return {false, false, "Script property values are stored as text."};
					}
					if (existing != overrides.end())
					{
						existing->value = *text;
					}
					else
					{
						overrides.push_back(ScriptPropertyOverride{scriptPath, propertyName, *text});
					}
					return {true, false, "Script property updated."};
				}
				else if (value.component == "Collider" && value.property == "enabled")
					{
						if (const auto* enabled = std::get_if<bool>(&value.value))
						{
							entity->hasCollider = *enabled;
							return {true, false, "Entity collider updated."};
						}
					}
					else if (value.component == "CineCamera" && value.property == "enabled")
					{
						if (const auto* enabled = std::get_if<bool>(&value.value))
						{
							entity->isCineCamera = *enabled;
							return {true, false, "Cine camera flag updated."};
						}
					}
					else if (value.component == "PickupItem")
					{
						if (value.property == "enabled")
						{
							if (const auto* enabled = std::get_if<bool>(&value.value))
							{
								entity->isPickupItem = *enabled;
								return {true, false, "Pickup item flag updated."};
							}
						}
						if (value.property == "itemName")
						{
							if (const auto* text = std::get_if<std::string>(&value.value))
							{
								entity->pickupItem.itemName = *text;
								return {true, false, "Pickup item name updated."};
							}
						}
						if (value.property == "iconPath")
						{
							if (const auto* text = std::get_if<std::string>(&value.value))
							{
								entity->pickupItem.iconPath = *text;
								return {true, false, "Pickup item icon updated."};
							}
						}
					}
					else if (value.component == "Castle")
					{
						if (value.property == "enabled")
						{
							if (const auto* enabled = std::get_if<bool>(&value.value))
							{
								entity->isCastle = *enabled;
								return {true, false, "Castle flag updated."};
							}
						}
						if (value.property == "hp")
						{
							if (const auto* number = std::get_if<float>(&value.value))
							{
								entity->castle.hp = *number;
								return {true, false, "Castle HP updated."};
							}
						}
						if (value.property == "maxHp")
						{
							if (const auto* number = std::get_if<float>(&value.value))
							{
								entity->castle.maxHp = *number;
								return {true, false, "Castle max HP updated."};
							}
						}
					}
					else if (value.component == "Catapult")
					{
						if (value.property == "enabled")
						{
							if (const auto* enabled = std::get_if<bool>(&value.value))
							{
								entity->isCatapult = *enabled;
								return {true, false, "Catapult flag updated."};
							}
						}
						if (value.property == "yawEntityName")
						{
							if (const auto* text = std::get_if<std::string>(&value.value))
							{
								entity->catapult.yawEntityName = *text;
								return {true, false, "Catapult yaw entity updated."};
							}
						}
						if (value.property == "armEntityName")
						{
							if (const auto* text = std::get_if<std::string>(&value.value))
							{
								entity->catapult.armEntityName = *text;
								return {true, false, "Catapult arm entity updated."};
							}
						}
						if (value.property == "minPitchDegrees")
						{
							if (const auto* number = std::get_if<float>(&value.value))
							{
								entity->catapult.minPitchDegrees = *number;
								return {true, false, "Catapult min pitch updated."};
							}
						}
						if (value.property == "maxPitchDegrees")
						{
							if (const auto* number = std::get_if<float>(&value.value))
							{
								entity->catapult.maxPitchDegrees = *number;
								return {true, false, "Catapult max pitch updated."};
							}
						}
						if (value.property == "launchSpeed")
						{
							if (const auto* number = std::get_if<float>(&value.value))
							{
								entity->catapult.launchSpeed = *number;
								return {true, false, "Catapult launch speed updated."};
							}
						}
					}
					else if (value.component == "Camera")
					{
						if (const auto* number = std::get_if<float>(&value.value))
						{
							if (value.property == "fpsEyeHeight")
							{
								entity->cameraRig.fpsEyeHeight = *number;
								return {true, false, "Camera rig updated."};
							}
							if (value.property == "thirdPersonDistance")
							{
								entity->cameraRig.thirdPersonDistance = *number;
								return {true, false, "Camera rig updated."};
							}
							if (value.property == "thirdPersonHeight")
							{
								entity->cameraRig.thirdPersonHeight = *number;
								return {true, false, "Camera rig updated."};
							}
							if (value.property == "thirdPersonAimHeight")
							{
								entity->cameraRig.thirdPersonAimHeight = *number;
								return {true, false, "Camera rig updated."};
							}
							if (value.property == "thirdPersonYawOffsetDegrees")
							{
								entity->cameraRig.thirdPersonYawOffsetDegrees = *number;
								return {true, false, "Camera rig updated."};
							}
						}
						if (value.property == "lockCursor")
						{
							if (const auto* enabled = std::get_if<bool>(&value.value))
							{
								entity->cameraRig.lockCursor = *enabled;
								return {true, false, "Camera rig updated."};
							}
						}
					}
					else if (value.component == "TextMesh")
					{
						if (value.property == "content")
						{
							if (const auto* text = std::get_if<std::string>(&value.value))
							{
								if (text->empty())
								{
									return {false, false, "Text content cannot be empty."};
								}
								entity->textMesh.content = *text;
								return {true, false, "Text mesh content updated."};
							}
						}
						else if (value.property == "fontPath")
						{
							if (const auto* path = std::get_if<std::string>(&value.value))
							{
								const std::filesystem::path fontPath = projectRoot_ / *path;
								if (!std::filesystem::exists(fontPath))
								{
									return {false, false, "Font file does not exist."};
								}
								entity->textMesh.fontPath = *path;
								return {true, false, "Text mesh font updated."};
							}
						}
						else if (value.property == "fontSize")
						{
							if (const auto* number = std::get_if<float>(&value.value))
							{
								entity->textMesh.fontSize = *number > 0.0F ? *number : entity->textMesh.fontSize;
								return {true, false, "Text mesh font size updated."};
							}
						}
						else if (value.property == "depth")
						{
							if (const auto* number = std::get_if<float>(&value.value))
							{
								entity->textMesh.depth = *number > 0.0F ? *number : entity->textMesh.depth;
								return {true, false, "Text mesh depth updated."};
							}
						}
					}
					return {false, false, "This scene property is not implemented yet."};
				}
				else
				{
					return {false, false, "Animation execution is not connected yet."};
				}
			},
			command);
	}

	const std::vector<SceneEntity>& EditorScene::entities() const noexcept
	{
		return entities_;
	}

	void EditorScene::replaceEntities(std::vector<SceneEntity> entities) noexcept
	{
		entities_ = std::move(entities);
		// Bump nextEntityId_ past every preserved id. The header explicitly
		// says replaceEntities preserves ids (so undo/snapshot restoration
		// keeps stable handles), but the next CreateEntityCommand allocates
		// nextEntityId_++ which would otherwise collide with a preserved id
		// and findEntity(int) would return whichever entity happens to sit
		// first in the vector - the wrong one for any id-bound caller
		// (e.g. ScriptRuntime closure upvalues).
		std::int32_t maxId = 0;
		for (const SceneEntity& entity : entities_)
		{
			if (entity.id > maxId)
			{
				maxId = entity.id;
			}
		}
		nextEntityId_ = maxId + 1;
	}

	void EditorScene::loadEntities(std::vector<SceneEntity> entities) noexcept
	{
		for (SceneEntity& entity : entities)
		{
			entity.id = nextEntityId_++;
		}
		entities_ = std::move(entities);
		// nextEntityId_ is now correctly max(loaded ids) + 1 because the
		// loop above ran nextEntityId_++ once per entity. No second bump
		// needed.
	}

	const SceneEntity* EditorScene::findEntity(int id) const noexcept
	{
		const auto iterator = std::find_if(
			entities_.begin(),
			entities_.end(),
			[id](const SceneEntity& entity) { return entity.id == id; });
		return iterator == entities_.end() ? nullptr : &(*iterator);
	}

	const SceneEntity* EditorScene::findEntity(const std::string& name) const noexcept
	{
		const auto iterator = std::find_if(
			entities_.begin(),
			entities_.end(),
			[&name](const SceneEntity& entity) { return entity.name == name; });
		return iterator == entities_.end() ? nullptr : &(*iterator);
	}

	bool EditorScene::isActiveInHierarchy(const SceneEntity& entity) const noexcept
	{
		const SceneEntity* current = &entity;
		// Depth cap doubles as the cycle guard (a parent loop just stops).
		for (std::size_t depth = 0; current != nullptr && depth <= entities_.size(); ++depth)
		{
			if (!current->active)
			{
				return false;
			}
			if (current->parentName.empty())
			{
				return true;
			}
			current = findEntity(current->parentName);
		}
		return true;
	}

	SceneEntity* EditorScene::findEntityMutable(int id) noexcept
	{
		const auto iterator = std::find_if(
			entities_.begin(),
			entities_.end(),
			[id](const SceneEntity& entity) { return entity.id == id; });
		return iterator == entities_.end() ? nullptr : &(*iterator);
	}

	SceneEntity* EditorScene::findEntityMutable(const std::string& name) noexcept
	{
		const auto iterator = std::find_if(
			entities_.begin(),
			entities_.end(),
			[&name](const SceneEntity& entity) { return entity.name == name; });
		return iterator == entities_.end() ? nullptr : &(*iterator);
	}

	bool EditorScene::nameInUse(const std::string& name) const noexcept
	{
		// Empty string is reserved as "no match"; findEntity("") returning
		// non-null would mean some entity actually has no name (a bug, not
		// a feature), and silently treating it as "in use" makes the
		// caller's loop behave as if every candidate was already taken.
		if (name.empty())
		{
			return true;
		}
		return findEntity(name) != nullptr;
	}

	std::string EditorScene::makeUniqueName(const std::string& baseName) const
	{
		if (!nameInUse(baseName))
		{
			return baseName;
		}
		// Bounded suffix search. Without an upper limit, a hostile or
		// pathological baseName near PATH_MAX could grow without bound
		// (and a baseName already containing " (1)" through " (N)" for
		// every N would loop forever). 10000 is far above any realistic
		// scene; if every slot is genuinely taken, fall back to a
		// timestamp-suffixed name to keep moving instead of looping.
		constexpr int kMaxSuffix = 10000;
		int suffix = 1;
		std::string candidate;
		do
		{
			candidate = baseName + " (" + std::to_string(suffix) + ")";
			++suffix;
			if (suffix > kMaxSuffix)
			{
				const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now().time_since_epoch()).count();
				candidate = baseName + " (" + std::to_string(stamp) + ")";
				break;
			}
		} while (nameInUse(candidate));
		return candidate;
	}
}
