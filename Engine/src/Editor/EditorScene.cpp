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
	// Serialised as names, not enum ordinals - reordering the enum must never
	// silently reinterpret a saved scene. Same rule as BootStep and AudioHook.
	const char* audioFilterName(const AudioEffects::Filter filter) noexcept
	{
		switch (filter)
		{
			case AudioEffects::Filter::LowPass:  return "low_pass";
			case AudioEffects::Filter::HighPass: return "high_pass";
			case AudioEffects::Filter::None:     break;
		}
		return "none";
	}

	bool audioFilterFromName(const std::string& name, AudioEffects::Filter& outFilter) noexcept
	{
		if (name == "none")      { outFilter = AudioEffects::Filter::None;     return true; }
		if (name == "low_pass")  { outFilter = AudioEffects::Filter::LowPass;  return true; }
		if (name == "high_pass") { outFilter = AudioEffects::Filter::HighPass; return true; }
		return false;
	}

	// Same name-not-ordinal rule as audioFilterName above.
	const char* lightTypeName(const LightType type) noexcept
	{
		switch (type)
		{
			case LightType::Point: return "point";
			case LightType::Spot:  return "spot";
			case LightType::Directional: break;
		}
		return "directional";
	}

	bool lightTypeFromName(const std::string& text, LightType& outType) noexcept
	{
		if (text == "directional") { outType = LightType::Directional; return true; }
		if (text == "point")       { outType = LightType::Point;       return true; }
		if (text == "spot")        { outType = LightType::Spot;        return true; }
		// Unity calls a directional light "Sun" in some UI surfaces and glTF
		// spells it "sun"; accept both rather than rejecting a reasonable word.
		if (text == "sun")         { outType = LightType::Directional; return true; }
		return false;
	}

	const char* colorFilterName(const ColorFilter filter) noexcept
	{
		switch (filter)
		{
			case ColorFilter::BlackAndWhite: return "black_and_white";
			case ColorFilter::Sepia:         return "sepia";
			case ColorFilter::Technicolor:   return "technicolor";
			case ColorFilter::Cold:          return "cold";
			case ColorFilter::Warm:          return "warm";
			case ColorFilter::Infrared:      return "infrared";
			case ColorFilter::HeatMap:       return "heat_map";
			case ColorFilter::None: break;
		}
		return "none";
	}

	bool colorFilterFromName(const std::string& text, ColorFilter& outFilter) noexcept
	{
		if (text == "none")            { outFilter = ColorFilter::None;          return true; }
		if (text == "black_and_white") { outFilter = ColorFilter::BlackAndWhite; return true; }
		if (text == "sepia")           { outFilter = ColorFilter::Sepia;         return true; }
		if (text == "technicolor")     { outFilter = ColorFilter::Technicolor;   return true; }
		if (text == "cold")            { outFilter = ColorFilter::Cold;          return true; }
		if (text == "warm")            { outFilter = ColorFilter::Warm;          return true; }
		if (text == "infrared")        { outFilter = ColorFilter::Infrared;      return true; }
		if (text == "heat_map")        { outFilter = ColorFilter::HeatMap;       return true; }
		return false;
	}

	const char* uiElementKindName(const UIElementKind kind) noexcept
	{
		switch (kind)
		{
			case UIElementKind::Image: return "image";
			case UIElementKind::Text:  return "text";
			case UIElementKind::Panel: return "panel";
			case UIElementKind::Crosshair: break;
		}
		return "crosshair";
	}

	bool uiElementKindFromName(const std::string& text, UIElementKind& outKind) noexcept
	{
		if (text == "crosshair") { outKind = UIElementKind::Crosshair; return true; }
		if (text == "image")     { outKind = UIElementKind::Image;     return true; }
		if (text == "text")      { outKind = UIElementKind::Text;      return true; }
		if (text == "panel")     { outKind = UIElementKind::Panel;     return true; }
		return false;
	}

	const char* uiAnchorName(const UIAnchor anchor) noexcept
	{
		switch (anchor)
		{
			case UIAnchor::TopLeft:      return "top_left";
			case UIAnchor::TopCenter:    return "top_center";
			case UIAnchor::TopRight:     return "top_right";
			case UIAnchor::MiddleLeft:   return "middle_left";
			case UIAnchor::MiddleRight:  return "middle_right";
			case UIAnchor::BottomLeft:   return "bottom_left";
			case UIAnchor::BottomCenter: return "bottom_center";
			case UIAnchor::BottomRight:  return "bottom_right";
			case UIAnchor::Center: break;
		}
		return "center";
	}

	bool uiAnchorFromName(const std::string& text, UIAnchor& outAnchor) noexcept
	{
		if (text == "center")        { outAnchor = UIAnchor::Center;       return true; }
		if (text == "top_left")      { outAnchor = UIAnchor::TopLeft;      return true; }
		if (text == "top_center")    { outAnchor = UIAnchor::TopCenter;    return true; }
		if (text == "top_right")     { outAnchor = UIAnchor::TopRight;     return true; }
		if (text == "middle_left")   { outAnchor = UIAnchor::MiddleLeft;   return true; }
		if (text == "middle_right")  { outAnchor = UIAnchor::MiddleRight;  return true; }
		if (text == "bottom_left")   { outAnchor = UIAnchor::BottomLeft;   return true; }
		if (text == "bottom_center") { outAnchor = UIAnchor::BottomCenter; return true; }
		if (text == "bottom_right")  { outAnchor = UIAnchor::BottomRight;  return true; }
		return false;
	}

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
					// Promote former children to root. World TRS was already
					// written by applyParentConstraints, so they stay put.
					for (SceneEntity& child : entities_)
					{
						if (child.parentName == value.entityName)
						{
							child.parentName.clear();
						}
					}
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
					entity->name = value.newName;
					if (value.entityName != value.newName)
					{
						for (SceneEntity& child : entities_)
						{
							if (child.parentName == value.entityName)
							{
								child.parentName = value.newName;
							}
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
					const std::filesystem::path tempPath = scriptPath->string() + ".tmp";
					{
						std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
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
						if (!output)
						{
							return {false, false, "Failed to flush the script file."};
						}
					}
					std::error_code writeError;
					if (exists)
					{
						const std::filesystem::path backupPath = scriptPath->string() + ".bak";
						std::filesystem::copy(
							*scriptPath, backupPath, std::filesystem::copy_options::overwrite_existing, writeError);
					}
					std::filesystem::rename(tempPath, *scriptPath, writeError);
					if (writeError)
					{
						std::filesystem::remove(tempPath, writeError);
						return {false, false, "Could not replace the script file: " + writeError.message()};
					}
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
					entity.colliderType = ColliderType::Mesh;
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
				else if constexpr (std::is_same_v<Command, CreateLightCommand>)
				{
					LightType type = LightType::Directional;
					if (!lightTypeFromName(value.type, type))
					{
						return {false, false, "Light type must be directional, point, or spot."};
					}
					const std::string name = value.name.empty() ? makeUniqueName("Light") : value.name;
					if (nameInUse(name))
					{
						return {false, false, "An entity with this name already exists."};
					}
					SceneEntity entity;
					entity.id = nextEntityId_++;
					entity.name = name;
					entity.position = value.position;
					entity.rotationEuler = value.rotationEuler;
					entity.localPosition = value.position;
					entity.localRotationEuler = value.rotationEuler;
					entity.isLight = true;
					entity.light.type = type;
					entity.light.color = value.color;
					entity.light.intensity = value.intensity > 0.0F ? value.intensity : 1.0F;
					// A point light at the default 25-unit range lighting a
					// room-sized scene reads as washed out; a spot wants a
					// tighter default than a bulb. Only the range differs -
					// cone angles already default sensibly.
					if (type == LightType::Spot)
					{
						entity.light.range = 20.0F;
					}
					entities_.push_back(entity);
					return {true, false, "Light created in the editor scene."};
				}
				else if constexpr (std::is_same_v<Command, CreateCameraCommand>)
				{
					const std::string name = value.name.empty() ? makeUniqueName("Camera") : value.name;
					if (nameInUse(name))
					{
						return {false, false, "An entity with this name already exists."};
					}
					if (!(value.fieldOfViewDegrees > 0.0F) || value.fieldOfViewDegrees >= 180.0F)
					{
						return {false, false, "Field of view must be between 0 and 180 degrees."};
					}
					if (value.makeMain)
					{
						// Enforce the "exactly one main camera" invariant here
						// rather than leaving it to the UI - the AI reaches this
						// path too, and two main cameras would make which one the
						// Game view picks depend on entity order.
						for (SceneEntity& other : entities_)
						{
							if (other.isCamera)
							{
								other.camera.isMainCamera = false;
							}
						}
					}
					SceneEntity entity;
					entity.id = nextEntityId_++;
					entity.name = name;
					entity.position = value.position;
					entity.rotationEuler = value.rotationEuler;
					entity.localPosition = value.position;
					entity.localRotationEuler = value.rotationEuler;
					entity.isCamera = true;
					entity.camera.fieldOfViewDegrees = value.fieldOfViewDegrees;
					entity.camera.isMainCamera = value.makeMain;
					entities_.push_back(entity);
					return {true, false, "Camera created in the editor scene."};
				}
				else if constexpr (std::is_same_v<Command, CreateUIElementCommand>)
				{
					UIElementKind kind = UIElementKind::Crosshair;
					if (!uiElementKindFromName(value.kind, kind))
					{
						return {false, false, "UI kind must be crosshair, image, text, or panel."};
					}
					if (!value.parentName.empty() && findEntity(value.parentName) == nullptr)
					{
						return {false, false, "Parent entity was not found in the editor scene."};
					}
					const std::string name =
						value.name.empty() ? makeUniqueName(uiElementKindName(kind)) : value.name;
					if (nameInUse(name))
					{
						return {false, false, "An entity with this name already exists."};
					}
					SceneEntity entity;
					entity.id = nextEntityId_++;
					entity.name = name;
					entity.isUIElement = true;
					entity.ui.kind = kind;
					entity.parentName = value.parentName;
					// A UI element is positioned by anchor+offsetPixels, never
					// by its transform, so it sits at its parent's origin
					// rather than the (0,1,0) offset a new child normally gets
					// - an offset there would be invisible but would still
					// drag the gizmo somewhere confusing.
					entity.localPosition = glm::vec3(0.0F);
					if (kind == UIElementKind::Text && !value.text.empty())
					{
						entity.ui.text = value.text;
					}
					if (kind == UIElementKind::Image)
					{
						entity.ui.imagePath = value.imagePath;
					}
					if (kind == UIElementKind::Panel)
					{
						// A panel is a background, so it defaults larger and
						// semi-transparent rather than a 24px opaque square.
						entity.ui.sizePixels = glm::vec2(220.0F, 120.0F);
						entity.ui.color = glm::vec3(0.05F, 0.06F, 0.09F);
						entity.ui.opacity = 0.65F;
					}
					entities_.push_back(entity);
					return {true, false, "UI element created in the editor scene."};
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
				else if (value.component == "Collider" && value.property == "enabled")
					{
						if (const auto* enabled = std::get_if<bool>(&value.value))
						{
							entity->hasCollider = *enabled;
							return {true, false, "Entity collider updated."};
						}
					}
				else if (value.component == "Collider" && value.property == "type")
					{
						if (const auto* type = std::get_if<std::string>(&value.value))
						{
							entity->colliderType = colliderTypeFromString(*type);
							return {true, false, "Entity collider type updated."};
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
					else if (value.component == "AudioSource")
					{
						if (value.property == "enabled")
						{
							if (const auto* enabled = std::get_if<bool>(&value.value))
							{
								entity->hasAudioSource = *enabled;
								return {true, false, "Audio source flag updated."};
							}
						}
						if (value.property == "clipAssetPath")
						{
							if (const auto* text = std::get_if<std::string>(&value.value))
							{
								entity->audioSource.clipAssetPath = *text;
								return {true, false, "Audio clip updated."};
							}
						}
						if (value.property == "volume")
						{
							if (const auto* number = std::get_if<float>(&value.value))
							{
								entity->audioSource.volume = *number;
								return {true, false, "Audio volume updated."};
							}
						}
						if (value.property == "pitch")
						{
							if (const auto* number = std::get_if<float>(&value.value))
							{
								entity->audioSource.pitch = *number;
								return {true, false, "Audio pitch updated."};
							}
						}
						if (value.property == "loop")
						{
							if (const auto* enabled = std::get_if<bool>(&value.value))
							{
								entity->audioSource.loop = *enabled;
								return {true, false, "Audio loop updated."};
							}
						}
						if (value.property == "playOnAwake")
						{
							if (const auto* enabled = std::get_if<bool>(&value.value))
							{
								entity->audioSource.playOnAwake = *enabled;
								return {true, false, "Audio playOnAwake updated."};
							}
						}
						if (value.property == "is3D")
						{
							if (const auto* enabled = std::get_if<bool>(&value.value))
							{
								entity->audioSource.is3D = *enabled;
								return {true, false, "Audio 3D flag updated."};
							}
						}
						// --- effects -------------------------------------------
						// Ranges are clamped here rather than trusted from the
						// caller: the panel, the AI and a hand-edited scene file
						// all reach this same handler, and a delay of 0 seconds
						// or a feedback of 1.0 would hang or run away.
						// Fades are on the source, not the effects, so they are
						// handled here rather than in the AudioEffects table below.
						if (value.property == "fadeInSeconds" || value.property == "fadeOutSeconds")
						{
							if (const auto* number = std::get_if<float>(&value.value))
							{
								if (!std::isfinite(*number))
								{
									return {false, false, value.property + " must be a finite number."};
								}
								// 0 means "no fade"; 30s is well past any sane
								// ramp and stops a typo holding a voice alive.
								const float seconds = std::clamp(*number, 0.0F, 30.0F);
								if (value.property == "fadeInSeconds")
								{
									entity->audioSource.fadeInSeconds = seconds;
								}
								else
								{
									entity->audioSource.fadeOutSeconds = seconds;
								}
								return {true, false, value.property + " updated."};
							}
							return {false, false, value.property + " expects a number."};
						}
						if (value.property == "fxReverb")
						{
							if (const auto* enabled = std::get_if<bool>(&value.value))
							{
								entity->audioSource.effects.reverb = *enabled;
								return {true, false, "Reverb toggled."};
							}
						}
						if (value.property == "fxDelay")
						{
							if (const auto* enabled = std::get_if<bool>(&value.value))
							{
								entity->audioSource.effects.delay = *enabled;
								return {true, false, "Delay toggled."};
							}
						}
						if (value.property == "fxFilter")
						{
							if (const auto* text = std::get_if<std::string>(&value.value))
							{
								AudioEffects::Filter parsed = AudioEffects::Filter::None;
								if (!audioFilterFromName(*text, parsed))
								{
									return {false, false,
										"Unknown audio filter '" + *text + "'. Use none, low_pass or high_pass."};
								}
								entity->audioSource.effects.filter = parsed;
								return {true, false, "Audio filter updated."};
							}
						}
						{
							struct NumericEffect
							{
								const char* property;
								float AudioEffects::*field;
								float low;
								float high;
							};
							static constexpr NumericEffect kNumericEffects[] = {
								{"fxReverbRoomSize", &AudioEffects::reverbRoomSize, 0.0F, 1.0F},
								{"fxReverbDamping",  &AudioEffects::reverbDamping,  0.0F, 1.0F},
								{"fxReverbWet",      &AudioEffects::reverbWet,      0.0F, 1.0F},
								{"fxReverbDry",      &AudioEffects::reverbDry,      0.0F, 1.0F},
								{"fxDelaySeconds",   &AudioEffects::delaySeconds,   0.01F, 2.0F},
								// Feedback must stay below 1.0 or the delay line
								// never decays and the sound grows without bound.
								{"fxDelayDecay",     &AudioEffects::delayDecay,     0.0F, 0.99F},
								{"fxDelayWet",       &AudioEffects::delayWet,       0.0F, 1.0F},
								{"fxDelayDry",       &AudioEffects::delayDry,       0.0F, 1.0F},
								{"fxCutoffHz",       &AudioEffects::cutoffHz,       20.0F, 20000.0F},
							};
							for (const NumericEffect& effect : kNumericEffects)
							{
								if (value.property != effect.property)
								{
									continue;
								}
								if (const auto* number = std::get_if<float>(&value.value))
								{
									if (!std::isfinite(*number))
									{
										return {false, false,
											std::string(effect.property) + " must be a finite number."};
									}
									entity->audioSource.effects.*effect.field =
										std::clamp(*number, effect.low, effect.high);
									return {true, false, std::string(effect.property) + " updated."};
								}
								return {false, false, std::string(effect.property) + " expects a number."};
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
						// This component covers BOTH the scripted-camera rig
						// (fpsEyeHeight etc, which any entity can carry) and a
						// real Camera entity's lens settings. The property
						// names don't overlap, so they share one component
						// name rather than making the user learn two.
						if (value.property == "enabled")
						{
							if (const auto* enabled = std::get_if<bool>(&value.value))
							{
								entity->isCamera = *enabled;
								return {true, false, "Camera flag updated."};
							}
						}
						if (value.property == "isMainCamera")
						{
							if (const auto* enabled = std::get_if<bool>(&value.value))
							{
								if (*enabled)
								{
									if (!entity->isCamera)
									{
										return {false, false, "Entity is not a camera."};
									}
									// Same "exactly one main camera" invariant
									// CreateCameraCommand enforces.
									const int keepId = entity->id;
									for (SceneEntity& other : entities_)
									{
										if (other.isCamera && other.id != keepId)
										{
											other.camera.isMainCamera = false;
										}
									}
								}
								entity->camera.isMainCamera = *enabled;
								return {true, false, "Main camera updated."};
							}
						}
						if (value.property == "clearColor")
						{
							if (const auto* color = std::get_if<glm::vec3>(&value.value))
							{
								entity->camera.clearColor = *color;
								return {true, false, "Camera clear color updated."};
							}
						}
						if (const auto* number = std::get_if<float>(&value.value))
						{
							if (value.property == "fieldOfView")
							{
								if (!(*number > 0.0F) || *number >= 180.0F)
								{
									return {false, false, "Field of view must be between 0 and 180 degrees."};
								}
								entity->camera.fieldOfViewDegrees = *number;
								return {true, false, "Camera field of view updated."};
							}
							if (value.property == "nearClip")
							{
								if (!(*number > 0.0F) || *number >= entity->camera.farClip)
								{
									return {false, false, "Near clip must be above 0 and below the far clip."};
								}
								entity->camera.nearClip = *number;
								return {true, false, "Camera near clip updated."};
							}
							if (value.property == "farClip")
							{
								if (*number <= entity->camera.nearClip)
								{
									return {false, false, "Far clip must be above the near clip."};
								}
								entity->camera.farClip = *number;
								return {true, false, "Camera far clip updated."};
							}
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
						// "lockCursor" was handled here. Cursor ownership moved to
						// GameplayState::cursorLockDesired, set by
						// self.gameManager:setCursorLock() - see game_manager.lua.
						// An old scene still carrying the key just falls through
						// to the unknown-property result below rather than failing.
					}
					else if (value.component == "Light")
					{
						if (value.property == "enabled")
						{
							if (const auto* enabled = std::get_if<bool>(&value.value))
							{
								entity->isLight = *enabled;
								return {true, false, "Light flag updated."};
							}
						}
						if (value.property == "type")
						{
							if (const auto* text = std::get_if<std::string>(&value.value))
							{
								LightType parsed = LightType::Directional;
								if (!lightTypeFromName(*text, parsed))
								{
									return {false, false, "Light type must be directional, point, or spot."};
								}
								entity->light.type = parsed;
								return {true, false, "Light type updated."};
							}
						}
						if (value.property == "color")
						{
							if (const auto* color = std::get_if<glm::vec3>(&value.value))
							{
								entity->light.color = *color;
								return {true, false, "Light color updated."};
							}
						}
						if (value.property == "castShadows")
						{
							if (const auto* enabled = std::get_if<bool>(&value.value))
							{
								entity->light.castShadows = *enabled;
								return {true, false, "Light shadow casting updated."};
							}
						}
						if (const auto* number = std::get_if<float>(&value.value))
						{
							if (!std::isfinite(*number))
							{
								return {false, false, "Light values must be finite."};
							}
							if (value.property == "intensity")
							{
								if (*number < 0.0F)
								{
									return {false, false, "Light intensity cannot be negative."};
								}
								entity->light.intensity = *number;
								return {true, false, "Light intensity updated."};
							}
							if (value.property == "range")
							{
								if (!(*number > 0.0F))
								{
									return {false, false, "Light range must be above 0."};
								}
								entity->light.range = *number;
								return {true, false, "Light range updated."};
							}
							if (value.property == "innerCone")
							{
								// Clamped rather than rejected: dragging the
								// inner slider past the outer one is a normal
								// thing to do in a UI, and snapping outer along
								// with it is what Unity does.
								const float clamped = std::clamp(*number, 0.0F, 89.0F);
								entity->light.innerConeDegrees = clamped;
								if (entity->light.outerConeDegrees < clamped)
								{
									entity->light.outerConeDegrees = clamped;
								}
								return {true, false, "Spot inner cone updated."};
							}
							if (value.property == "outerCone")
							{
								const float clamped = std::clamp(*number, 0.0F, 89.0F);
								entity->light.outerConeDegrees = clamped;
								if (entity->light.innerConeDegrees > clamped)
								{
									entity->light.innerConeDegrees = clamped;
								}
								return {true, false, "Spot outer cone updated."};
							}
							if (value.property == "shadowBias")
							{
								entity->light.shadowBias = std::clamp(*number, 0.0F, 0.1F);
								return {true, false, "Light shadow bias updated."};
							}
						}
					}
					else if (value.component == "UI")
					{
						if (value.property == "enabled")
						{
							if (const auto* enabled = std::get_if<bool>(&value.value))
							{
								entity->isUIElement = *enabled;
								return {true, false, "UI element flag updated."};
							}
						}
						if (const auto* text = std::get_if<std::string>(&value.value))
						{
							if (value.property == "kind")
							{
								UIElementKind parsed = UIElementKind::Crosshair;
								if (!uiElementKindFromName(*text, parsed))
								{
									return {false, false, "UI kind must be crosshair, image, text, or panel."};
								}
								entity->ui.kind = parsed;
								return {true, false, "UI kind updated."};
							}
							if (value.property == "anchor")
							{
								UIAnchor parsed = UIAnchor::Center;
								if (!uiAnchorFromName(*text, parsed))
								{
									return {false, false, "Unknown UI anchor."};
								}
								entity->ui.anchor = parsed;
								return {true, false, "UI anchor updated."};
							}
							if (value.property == "text")
							{
								entity->ui.text = *text;
								return {true, false, "UI text updated."};
							}
							if (value.property == "fontPath" || value.property == "imagePath")
							{
								// Empty clears the slot; anything else must
								// resolve inside the project, same rule the
								// text-mesh font path follows.
								if (!text->empty() && !std::filesystem::exists(projectRoot_ / *text))
								{
									return {false, false, "That file does not exist in the project."};
								}
								if (value.property == "fontPath") { entity->ui.fontPath = *text; }
								else { entity->ui.imagePath = *text; }
								return {true, false, "UI asset path updated."};
							}
						}
						if (value.property == "color")
						{
							if (const auto* color = std::get_if<glm::vec3>(&value.value))
							{
								entity->ui.color = *color;
								return {true, false, "UI color updated."};
							}
						}
						if (value.property == "offset" || value.property == "size")
						{
							// vec2-shaped values ride in on a vec3's xy - the
							// EditableValue variant has no vec2, and adding one
							// would touch every command consumer for two fields.
							if (const auto* vec = std::get_if<glm::vec3>(&value.value))
							{
								if (value.property == "offset")
								{
									entity->ui.offsetPixels = glm::vec2(vec->x, vec->y);
									return {true, false, "UI offset updated."};
								}
								if (!(vec->x > 0.0F) || !(vec->y > 0.0F))
								{
									return {false, false, "UI size must be above 0 on both axes."};
								}
								entity->ui.sizePixels = glm::vec2(vec->x, vec->y);
								return {true, false, "UI size updated."};
							}
						}
						if (const auto* number = std::get_if<float>(&value.value))
						{
							if (!std::isfinite(*number))
							{
								return {false, false, "UI values must be finite."};
							}
							if (value.property == "opacity")
							{
								entity->ui.opacity = std::clamp(*number, 0.0F, 1.0F);
								return {true, false, "UI opacity updated."};
							}
							if (value.property == "fontSize")
							{
								if (!(*number > 0.0F))
								{
									return {false, false, "UI font size must be above 0."};
								}
								entity->ui.fontSizePixels = *number;
								return {true, false, "UI font size updated."};
							}
							if (value.property == "thickness")
							{
								entity->ui.thicknessPixels = std::clamp(*number, 1.0F, 32.0F);
								return {true, false, "Crosshair thickness updated."};
							}
							if (value.property == "gap")
							{
								entity->ui.gapPixels = std::clamp(*number, 0.0F, 64.0F);
								return {true, false, "Crosshair gap updated."};
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
