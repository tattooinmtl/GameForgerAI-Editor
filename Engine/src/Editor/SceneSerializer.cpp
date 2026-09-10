#include "GameForger/Editor/SceneSerializer.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>

#include "GameForger/Editor/Json.hpp"

namespace gameforger::editor
{
	namespace
	{
		PrimitiveType primitiveTypeFromString(const std::string& text)
		{
			if (text == "sphere") return PrimitiveType::Sphere;
			if (text == "cylinder") return PrimitiveType::Cylinder;
			if (text == "cone") return PrimitiveType::Cone;
			if (text == "plane") return PrimitiveType::Plane;
			if (text == "capsule") return PrimitiveType::Capsule;
			if (text == "empty") return PrimitiveType::Empty;
			return PrimitiveType::Cube;
		}

		std::string readString(const json::Value& obj, const char* key, const std::string& fallback = {})
		{
			const json::Value* field = obj.find(key);
			return field == nullptr ? fallback : field->asString().value_or(fallback);
		}

		float readFloat(const json::Value& obj, const char* key, const float fallback)
		{
			const json::Value* field = obj.find(key);
			return field == nullptr ? fallback : static_cast<float>(field->asNumber().value_or(fallback));
		}

		bool readBool(const json::Value& obj, const char* key, const bool fallback)
		{
			const json::Value* field = obj.find(key);
			return field == nullptr ? fallback : field->asBool().value_or(fallback);
		}

		glm::vec3 readVec3(const json::Value& obj, const char* key, const glm::vec3& fallback)
		{
			const json::Value* field = obj.find(key);
			if (field == nullptr)
			{
				return fallback;
			}
			const std::optional<std::vector<double>> numbers = field->asNumberArray();
			if (!numbers.has_value() || numbers->size() != 3)
			{
				return fallback;
			}
			return glm::vec3(
				static_cast<float>((*numbers)[0]),
				static_cast<float>((*numbers)[1]),
				static_cast<float>((*numbers)[2]));
		}

		glm::vec2 readVec2(const json::Value& obj, const char* key, const glm::vec2& fallback)
		{
			const json::Value* field = obj.find(key);
			if (field == nullptr)
			{
				return fallback;
			}
			const std::optional<std::vector<double>> numbers = field->asNumberArray();
			if (!numbers.has_value() || numbers->size() != 2)
			{
				return fallback;
			}
			return glm::vec2(static_cast<float>((*numbers)[0]), static_cast<float>((*numbers)[1]));
		}

		std::optional<SceneEntity> parseEntity(const json::Value& obj)
		{
			if (obj.type != json::Value::Type::Object)
			{
				return std::nullopt;
			}
			SceneEntity entity;
			entity.name = readString(obj, "name");
			if (entity.name.empty())
			{
				return std::nullopt;
			}
			if (const json::Value* tags = obj.find("tags"))
			{
				if (tags->type == json::Value::Type::Array)
				{
					for (const json::Value& tagValue : tags->arrayValue)
					{
						if (const std::optional<std::string> tag = tagValue.asString())
						{
							entity.tags.push_back(*tag);
						}
					}
				}
			}
			else
			{
				// Pre-0.46 scenes stored a single "tag" string instead of a
				// "tags" array - fold it into a one-element list so older
				// saves still load with their tag intact. "Untagged" was that
				// format's placeholder for "no tag", which is now just an
				// empty list, so it deliberately isn't carried over as a
				// real tag.
				const std::string legacyTag = readString(obj, "tag", "Untagged");
				if (legacyTag != "Untagged" && !legacyTag.empty())
				{
					entity.tags.push_back(legacyTag);
				}
			}
			entity.active = readBool(obj, "active", true);
			entity.primitive = primitiveTypeFromString(readString(obj, "primitive", "cube"));
			entity.position = readVec3(obj, "position", glm::vec3(0.0F));
			entity.rotationEuler = readVec3(obj, "rotation", glm::vec3(0.0F));
			entity.scale = readVec3(obj, "scale", glm::vec3(1.0F));
			entity.color = readVec3(obj, "color", glm::vec3(0.8F));
			entity.pivotOffset = readVec3(obj, "pivot", glm::vec3(0.0F));
			entity.materialBlendWeight = readVec3(obj, "materialBlendWeight", glm::vec3(0.0F));
			entity.materialUvScale = readVec2(obj, "materialUvScale", glm::vec2(2.0F, 2.0F));
			entity.parentName = readString(obj, "parentName");
			entity.localPosition = readVec3(obj, "localPosition", glm::vec3(0.0F, 1.0F, 0.0F));
			entity.localRotationEuler = readVec3(obj, "localRotationEuler", glm::vec3(0.0F));
			entity.localScale = readVec3(obj, "localScale", glm::vec3(1.0F));
			if (const json::Value* materialLayers = obj.find("materialLayers"))
			{
				if (materialLayers->type == json::Value::Type::Array)
				{
					for (std::size_t index = 0;
						index < materialLayers->arrayValue.size() && index < entity.materialLayers.size();
						++index)
					{
						const json::Value& layer = materialLayers->arrayValue[index];
						entity.materialLayers[index].diffusePath = readString(layer, "diffusePath");
						entity.materialLayers[index].normalPath = readString(layer, "normalPath");
						entity.materialLayers[index].heightPath = readString(layer, "heightPath");
					}
				}
			}
			entity.hasCollider = readBool(obj, "hasCollider", false);
			entity.isTextMesh = readBool(obj, "isTextMesh", false);
			entity.isCineCamera = readBool(obj, "isCineCamera", false);
			entity.isTerrain = readBool(obj, "isTerrain", false);
			if (const json::Value* terrain = obj.find("terrain"))
			{
				entity.terrain.resolution = static_cast<int>(readFloat(*terrain, "resolution", 65.0F));
				entity.terrain.worldSize = readFloat(*terrain, "worldSize", entity.terrain.worldSize);
				entity.terrain.heightScale = readFloat(*terrain, "heightScale", entity.terrain.heightScale);
				entity.terrain.uvScale = readVec2(*terrain, "uvScale", glm::vec2(8.0F, 8.0F));
				if (const json::Value* heights = terrain->find("heights"))
				{
					if (heights->type == json::Value::Type::Array)
					{
						entity.terrain.heights.reserve(heights->arrayValue.size());
						for (const json::Value& heightValue : heights->arrayValue)
						{
							entity.terrain.heights.push_back(
								static_cast<float>(heightValue.asNumber().value_or(0.0)));
						}
					}
				}
				if (const json::Value* layers = terrain->find("layers"))
				{
					if (layers->type == json::Value::Type::Array)
					{
						for (std::size_t index = 0;
							index < layers->arrayValue.size() && index < entity.terrain.layers.size();
							++index)
						{
							const json::Value& layer = layers->arrayValue[index];
							entity.terrain.layers[index].diffusePath = readString(layer, "diffusePath");
							entity.terrain.layers[index].normalPath = readString(layer, "normalPath");
							entity.terrain.layers[index].heightPath = readString(layer, "heightPath");
						}
					}
				}
				if (const json::Value* splatWeights = terrain->find("splatWeights"))
				{
					if (splatWeights->type == json::Value::Type::Array)
					{
						entity.terrain.splatWeights.reserve(splatWeights->arrayValue.size());
						for (const json::Value& weightValue : splatWeights->arrayValue)
						{
							entity.terrain.splatWeights.push_back(
								static_cast<float>(weightValue.asNumber().value_or(0.0)));
						}
					}
				}
			}
			entity.isImportedMesh = readBool(obj, "isImportedMesh", false);
			if (const json::Value* importedMesh = obj.find("importedMesh"))
			{
				entity.importedMesh.sourcePath =
					readString(*importedMesh, "sourcePath", entity.importedMesh.sourcePath);
			}
			if (obj.find("colliderType") != nullptr)
			{
				entity.colliderType = colliderTypeFromString(readString(obj, "colliderType", "box"));
			}
			else
			{
				// Pre-type scenes: imported colliders were triangle meshes.
				entity.colliderType = entity.isImportedMesh ? ColliderType::Mesh : ColliderType::Box;
			}
			if (const json::Value* textMesh = obj.find("textMesh"))
			{
				entity.textMesh.content = readString(*textMesh, "content", entity.textMesh.content);
				entity.textMesh.fontPath = readString(*textMesh, "fontPath", entity.textMesh.fontPath);
				entity.textMesh.fontSize = readFloat(*textMesh, "fontSize", entity.textMesh.fontSize);
				entity.textMesh.depth = readFloat(*textMesh, "depth", entity.textMesh.depth);
			}

			// Light / Camera / UI are additive like every block around them:
			// an older scene has none of these keys, the flags default false,
			// and the nested defaults come from the structs. Enums round-trip
			// by NAME, so reordering LightType can never silently reinterpret
			// a saved scene (same rule as audioFilter/BootStep).
			entity.isLight = readBool(obj, "isLight", false);
			if (const json::Value* light = obj.find("light"))
			{
				LightType parsedType = entity.light.type;
				if (lightTypeFromName(readString(*light, "type", lightTypeName(entity.light.type)), parsedType))
				{
					entity.light.type = parsedType;
				}
				entity.light.color = readVec3(*light, "color", entity.light.color);
				entity.light.intensity = readFloat(*light, "intensity", entity.light.intensity);
				entity.light.range = readFloat(*light, "range", entity.light.range);
				entity.light.innerConeDegrees = readFloat(*light, "innerCone", entity.light.innerConeDegrees);
				entity.light.outerConeDegrees = readFloat(*light, "outerCone", entity.light.outerConeDegrees);
				entity.light.castShadows = readBool(*light, "castShadows", entity.light.castShadows);
				entity.light.shadowBias = readFloat(*light, "shadowBias", entity.light.shadowBias);
			}

			entity.isCamera = readBool(obj, "isCamera", false);
			if (const json::Value* camera = obj.find("camera"))
			{
				entity.camera.fieldOfViewDegrees =
					readFloat(*camera, "fieldOfView", entity.camera.fieldOfViewDegrees);
				entity.camera.nearClip = readFloat(*camera, "nearClip", entity.camera.nearClip);
				entity.camera.farClip = readFloat(*camera, "farClip", entity.camera.farClip);
				entity.camera.clearColor = readVec3(*camera, "clearColor", entity.camera.clearColor);
				entity.camera.isMainCamera = readBool(*camera, "isMainCamera", entity.camera.isMainCamera);
			}

			entity.isUIElement = readBool(obj, "isUIElement", false);
			if (const json::Value* ui = obj.find("ui"))
			{
				UIElementKind parsedKind = entity.ui.kind;
				if (uiElementKindFromName(readString(*ui, "kind", uiElementKindName(entity.ui.kind)), parsedKind))
				{
					entity.ui.kind = parsedKind;
				}
				UIAnchor parsedAnchor = entity.ui.anchor;
				if (uiAnchorFromName(readString(*ui, "anchor", uiAnchorName(entity.ui.anchor)), parsedAnchor))
				{
					entity.ui.anchor = parsedAnchor;
				}
				entity.ui.offsetPixels = readVec2(*ui, "offset", entity.ui.offsetPixels);
				entity.ui.sizePixels = readVec2(*ui, "size", entity.ui.sizePixels);
				entity.ui.color = readVec3(*ui, "color", entity.ui.color);
				entity.ui.opacity = readFloat(*ui, "opacity", entity.ui.opacity);
				entity.ui.text = readString(*ui, "text", entity.ui.text);
				entity.ui.fontPath = readString(*ui, "fontPath", entity.ui.fontPath);
				entity.ui.fontSizePixels = readFloat(*ui, "fontSize", entity.ui.fontSizePixels);
				entity.ui.imagePath = readString(*ui, "imagePath", entity.ui.imagePath);
				entity.ui.thicknessPixels = readFloat(*ui, "thickness", entity.ui.thicknessPixels);
				entity.ui.gapPixels = readFloat(*ui, "gap", entity.ui.gapPixels);
			}

			entity.isPickupItem = readBool(obj, "isPickupItem", false);
			if (const json::Value* pickupItem = obj.find("pickupItem"))
			{
				entity.pickupItem.itemName = readString(*pickupItem, "itemName", entity.pickupItem.itemName);
				entity.pickupItem.iconPath = readString(*pickupItem, "iconPath", entity.pickupItem.iconPath);
			}

			entity.hasAudioSource = readBool(obj, "hasAudioSource", false);
			if (const json::Value* audioSource = obj.find("audioSource"))
			{
				entity.audioSource.clipAssetPath =
					readString(*audioSource, "clipAssetPath", entity.audioSource.clipAssetPath);
				entity.audioSource.volume = readFloat(*audioSource, "volume", entity.audioSource.volume);
				entity.audioSource.pitch = readFloat(*audioSource, "pitch", entity.audioSource.pitch);
				entity.audioSource.loop = readBool(*audioSource, "loop", entity.audioSource.loop);
				entity.audioSource.playOnAwake = readBool(*audioSource, "playOnAwake", entity.audioSource.playOnAwake);
				entity.audioSource.is3D = readBool(*audioSource, "is3D", entity.audioSource.is3D);
				entity.audioSource.minDistance = readFloat(*audioSource, "minDistance", entity.audioSource.minDistance);
				entity.audioSource.maxDistance = readFloat(*audioSource, "maxDistance", entity.audioSource.maxDistance);
				entity.audioSource.fadeInSeconds =
					readFloat(*audioSource, "fadeInSeconds", entity.audioSource.fadeInSeconds);
				entity.audioSource.fadeOutSeconds =
					readFloat(*audioSource, "fadeOutSeconds", entity.audioSource.fadeOutSeconds);
				// Absent in scenes written before effects existed, so every field
				// falls back to its default and an old scene loads dry.
				if (const json::Value* fx = audioSource->find("effects"))
				{
					AudioEffects& effects = entity.audioSource.effects;
					effects.reverb = readBool(*fx, "reverb", effects.reverb);
					effects.reverbRoomSize = readFloat(*fx, "reverbRoomSize", effects.reverbRoomSize);
					effects.reverbDamping = readFloat(*fx, "reverbDamping", effects.reverbDamping);
					effects.reverbWet = readFloat(*fx, "reverbWet", effects.reverbWet);
					effects.reverbDry = readFloat(*fx, "reverbDry", effects.reverbDry);
					effects.delay = readBool(*fx, "delay", effects.delay);
					effects.delaySeconds = readFloat(*fx, "delaySeconds", effects.delaySeconds);
					effects.delayDecay = readFloat(*fx, "delayDecay", effects.delayDecay);
					effects.delayWet = readFloat(*fx, "delayWet", effects.delayWet);
					effects.delayDry = readFloat(*fx, "delayDry", effects.delayDry);
					// An unrecognised filter name means a newer editor wrote this;
					// keep the default rather than guessing at it.
					(void)audioFilterFromName(readString(*fx, "filter", "none"), effects.filter);
					effects.cutoffHz = readFloat(*fx, "cutoffHz", effects.cutoffHz);
				}
			}

			entity.isCastle = readBool(obj, "isCastle", false);
			if (const json::Value* castle = obj.find("castle"))
			{
				entity.castle.hp = readFloat(*castle, "hp", entity.castle.hp);
				entity.castle.maxHp = readFloat(*castle, "maxHp", entity.castle.maxHp);
			}

			entity.isCatapult = readBool(obj, "isCatapult", false);
			if (const json::Value* catapult = obj.find("catapult"))
			{
				entity.catapult.yawEntityName =
					readString(*catapult, "yawEntityName", entity.catapult.yawEntityName);
				entity.catapult.armEntityName =
					readString(*catapult, "armEntityName", entity.catapult.armEntityName);
				entity.catapult.minPitchDegrees =
					readFloat(*catapult, "minPitchDegrees", entity.catapult.minPitchDegrees);
				entity.catapult.maxPitchDegrees =
					readFloat(*catapult, "maxPitchDegrees", entity.catapult.maxPitchDegrees);
				entity.catapult.launchSpeed = readFloat(*catapult, "launchSpeed", entity.catapult.launchSpeed);
			}

			if (const json::Value* cameraRig = obj.find("cameraRig"))
			{
				entity.cameraRig.fpsEyeHeight =
					readFloat(*cameraRig, "fpsEyeHeight", entity.cameraRig.fpsEyeHeight);
				entity.cameraRig.thirdPersonDistance =
					readFloat(*cameraRig, "thirdPersonDistance", entity.cameraRig.thirdPersonDistance);
				entity.cameraRig.thirdPersonHeight =
					readFloat(*cameraRig, "thirdPersonHeight", entity.cameraRig.thirdPersonHeight);
				entity.cameraRig.thirdPersonAimHeight =
					readFloat(*cameraRig, "thirdPersonAimHeight", entity.cameraRig.thirdPersonAimHeight);
				entity.cameraRig.thirdPersonYawOffsetDegrees = readFloat(
					*cameraRig, "thirdPersonYawOffsetDegrees", entity.cameraRig.thirdPersonYawOffsetDegrees);
			}

			if (const json::Value* scripts = obj.find("scripts"))
			{
				if (scripts->type == json::Value::Type::Array)
				{
					for (const json::Value& scriptValue : scripts->arrayValue)
					{
						if (const std::optional<std::string> path = scriptValue.asString())
						{
							entity.scripts.push_back(*path);
						}
					}
				}
			}

			if (const json::Value* animation = obj.find("animation"))
			{
				entity.animation.enabled = readBool(*animation, "enabled", false);
				entity.animation.looping = readBool(*animation, "looping", false);
				if (const json::Value* keyframes = animation->find("keyframes"))
				{
					if (keyframes->type == json::Value::Type::Array)
					{
						for (const json::Value& keyframeValue : keyframes->arrayValue)
						{
							if (keyframeValue.type != json::Value::Type::Object)
							{
								continue;
							}
							TransformKeyframe keyframe;
							keyframe.time = readFloat(keyframeValue, "time", 0.0F);
							keyframe.position = readVec3(keyframeValue, "position", glm::vec3(0.0F));
							keyframe.rotationEuler = readVec3(keyframeValue, "rotation", glm::vec3(0.0F));
							keyframe.scale = readVec3(keyframeValue, "scale", glm::vec3(1.0F));
							entity.animation.keyframes.push_back(keyframe);
						}
					}
				}
			}

			return entity;
		}

		const char* primitiveTypeToString(const PrimitiveType type)
		{
			switch (type)
			{
				case PrimitiveType::Cube: return "cube";
				case PrimitiveType::Sphere: return "sphere";
				case PrimitiveType::Cylinder: return "cylinder";
				case PrimitiveType::Cone: return "cone";
				case PrimitiveType::Plane: return "plane";
				case PrimitiveType::Capsule: return "capsule";
				case PrimitiveType::Empty: return "empty";
			}
			return "cube";
		}

		std::string escapeJson(const std::string& text)
		{
			std::string escaped;
			escaped.reserve(text.size());
			for (const char character : text)
			{
				switch (character)
				{
					case '"': escaped += "\\\""; break;
					case '\\': escaped += "\\\\"; break;
					case '\n': escaped += "\\n"; break;
					case '\r': break;
					case '\t': escaped += "\\t"; break;
					default: escaped += character; break;
				}
			}
			return escaped;
		}

		std::string vec3ToJsonArray(const glm::vec3& value)
		{
			std::array<char, 96> buffer{};
			std::snprintf(buffer.data(), buffer.size(), "[%.6f, %.6f, %.6f]", value.x, value.y, value.z);
			return buffer.data();
		}

		// Same %.6f the vec helpers use, for the scalar fields of the light /
		// camera / UI blocks. Every other scalar in this file spells out its
		// own snprintf buffer inline; this exists because those three blocks
		// alone would have added ~20 more of them.
		std::string floatToJson(const float value)
		{
			std::array<char, 32> buffer{};
			std::snprintf(buffer.data(), buffer.size(), "%.6f", value);
			return buffer.data();
		}

		std::string boolToJson(const bool value)
		{
			return value ? "true" : "false";
		}

		std::string vec2ToJsonArray(const glm::vec2& value)
		{
			std::array<char, 64> buffer{};
			std::snprintf(buffer.data(), buffer.size(), "[%.6f, %.6f]", value.x, value.y);
			return buffer.data();
		}

		void appendKeyframe(std::string& json, const TransformKeyframe& keyframe, const std::string& indent)
		{
			std::array<char, 32> timeBuffer{};
			std::snprintf(timeBuffer.data(), timeBuffer.size(), "%.6f", keyframe.time);

			json += indent + "{\n";
			json += indent + "  \"time\": " + timeBuffer.data() + ",\n";
			json += indent + "  \"position\": " + vec3ToJsonArray(keyframe.position) + ",\n";
			json += indent + "  \"rotation\": " + vec3ToJsonArray(keyframe.rotationEuler) + ",\n";
			json += indent + "  \"scale\": " + vec3ToJsonArray(keyframe.scale) + "\n";
			json += indent + "}";
		}

		void appendEntity(std::string& json, const SceneEntity& entity, const std::string& indent)
		{
			json += indent + "{\n";
			json += indent + "  \"name\": \"" + escapeJson(entity.name) + "\",\n";
			json += indent + "  \"active\": " + std::string(entity.active ? "true" : "false") + ",\n";
			json += indent + "  \"tags\": [";
			for (std::size_t index = 0; index < entity.tags.size(); ++index)
			{
				json += (index == 0 ? "\n" : ",\n");
				json += indent + "    \"" + escapeJson(entity.tags[index]) + "\"";
			}
			json += entity.tags.empty() ? "],\n" : ("\n" + indent + "  ],\n");
			json += indent + "  \"primitive\": \"" + primitiveTypeToString(entity.primitive) + "\",\n";
			json += indent + "  \"position\": " + vec3ToJsonArray(entity.position) + ",\n";
			json += indent + "  \"rotation\": " + vec3ToJsonArray(entity.rotationEuler) + ",\n";
			json += indent + "  \"scale\": " + vec3ToJsonArray(entity.scale) + ",\n";
			json += indent + "  \"color\": " + vec3ToJsonArray(entity.color) + ",\n";
			json += indent + "  \"pivot\": " + vec3ToJsonArray(entity.pivotOffset) + ",\n";
			json += indent + "  \"materialBlendWeight\": " + vec3ToJsonArray(entity.materialBlendWeight) + ",\n";
			json += indent + "  \"materialUvScale\": " + vec2ToJsonArray(entity.materialUvScale) + ",\n";
			json += indent + "  \"parentName\": \"" + escapeJson(entity.parentName) + "\",\n";
			json += indent + "  \"localPosition\": " + vec3ToJsonArray(entity.localPosition) + ",\n";
			json += indent + "  \"localRotationEuler\": " + vec3ToJsonArray(entity.localRotationEuler) + ",\n";
			json += indent + "  \"localScale\": " + vec3ToJsonArray(entity.localScale) + ",\n";
			json += indent + "  \"materialLayers\": [";
			for (std::size_t index = 0; index < entity.materialLayers.size(); ++index)
			{
				const TerrainLayerData& layer = entity.materialLayers[index];
				json += (index == 0 ? "\n" : ",\n") + indent + "    {\n";
				json += indent + "      \"diffusePath\": \"" + escapeJson(layer.diffusePath) + "\",\n";
				json += indent + "      \"normalPath\": \"" + escapeJson(layer.normalPath) + "\",\n";
				json += indent + "      \"heightPath\": \"" + escapeJson(layer.heightPath) + "\"\n";
				json += indent + "    }";
			}
			json += entity.materialLayers.empty() ? "],\n" : ("\n" + indent + "  ],\n");
			json += indent + "  \"hasCollider\": " + std::string(entity.hasCollider ? "true" : "false") + ",\n";
			json += indent + "  \"colliderType\": \"" + std::string(colliderTypeToString(entity.colliderType)) + "\",\n";
			json += indent + "  \"isPickupItem\": " + std::string(entity.isPickupItem ? "true" : "false") + ",\n";
			json += indent + "  \"pickupItem\": {\n";
			json += indent + "    \"itemName\": \"" + escapeJson(entity.pickupItem.itemName) + "\",\n";
			json += indent + "    \"iconPath\": \"" + escapeJson(entity.pickupItem.iconPath) + "\"\n";
			json += indent + "  },\n";
			json += indent + "  \"hasAudioSource\": " + std::string(entity.hasAudioSource ? "true" : "false") + ",\n";
			{
				std::array<char, 32> audioScalar{};
				json += indent + "  \"audioSource\": {\n";
				json += indent + "    \"clipAssetPath\": \"" + escapeJson(entity.audioSource.clipAssetPath) + "\",\n";
				std::snprintf(audioScalar.data(), audioScalar.size(), "%.6f", entity.audioSource.volume);
				json += indent + "    \"volume\": " + audioScalar.data() + ",\n";
				std::snprintf(audioScalar.data(), audioScalar.size(), "%.6f", entity.audioSource.pitch);
				json += indent + "    \"pitch\": " + audioScalar.data() + ",\n";
				json += indent + "    \"loop\": " + std::string(entity.audioSource.loop ? "true" : "false") + ",\n";
				json += indent + "    \"playOnAwake\": " +
					std::string(entity.audioSource.playOnAwake ? "true" : "false") + ",\n";
				json += indent + "    \"is3D\": " + std::string(entity.audioSource.is3D ? "true" : "false") + ",\n";
				std::snprintf(audioScalar.data(), audioScalar.size(), "%.6f", entity.audioSource.minDistance);
				json += indent + "    \"minDistance\": " + audioScalar.data() + ",\n";
				std::snprintf(audioScalar.data(), audioScalar.size(), "%.6f", entity.audioSource.maxDistance);
				json += indent + "    \"maxDistance\": " + audioScalar.data() + ",\n";
				std::snprintf(audioScalar.data(), audioScalar.size(), "%.6f", entity.audioSource.fadeInSeconds);
				json += indent + "    \"fadeInSeconds\": " + audioScalar.data() + ",\n";
				std::snprintf(audioScalar.data(), audioScalar.size(), "%.6f", entity.audioSource.fadeOutSeconds);
				json += indent + "    \"fadeOutSeconds\": " + audioScalar.data() + ",\n";
				{
					const AudioEffects& fx = entity.audioSource.effects;
					const auto scalar = [&audioScalar](const float value)
					{
						std::snprintf(audioScalar.data(), audioScalar.size(), "%.6f", static_cast<double>(value));
						return std::string(audioScalar.data());
					};
					json += indent + "    \"effects\": {\n";
					json += indent + "      \"reverb\": " + std::string(fx.reverb ? "true" : "false") + ",\n";
					json += indent + "      \"reverbRoomSize\": " + scalar(fx.reverbRoomSize) + ",\n";
					json += indent + "      \"reverbDamping\": " + scalar(fx.reverbDamping) + ",\n";
					json += indent + "      \"reverbWet\": " + scalar(fx.reverbWet) + ",\n";
					json += indent + "      \"reverbDry\": " + scalar(fx.reverbDry) + ",\n";
					json += indent + "      \"delay\": " + std::string(fx.delay ? "true" : "false") + ",\n";
					json += indent + "      \"delaySeconds\": " + scalar(fx.delaySeconds) + ",\n";
					json += indent + "      \"delayDecay\": " + scalar(fx.delayDecay) + ",\n";
					json += indent + "      \"delayWet\": " + scalar(fx.delayWet) + ",\n";
					json += indent + "      \"delayDry\": " + scalar(fx.delayDry) + ",\n";
					json += indent + "      \"filter\": \"" + std::string(audioFilterName(fx.filter)) + "\",\n";
					json += indent + "      \"cutoffHz\": " + scalar(fx.cutoffHz) + "\n";
					json += indent + "    }\n";
				}
				json += indent + "  },\n";
			}
			json += indent + "  \"isCastle\": " + std::string(entity.isCastle ? "true" : "false") + ",\n";
			{
				std::array<char, 32> castleScalarBuffer{};
				json += indent + "  \"castle\": {\n";
				std::snprintf(castleScalarBuffer.data(), castleScalarBuffer.size(), "%.6f", entity.castle.hp);
				json += indent + "    \"hp\": " + castleScalarBuffer.data() + ",\n";
				std::snprintf(castleScalarBuffer.data(), castleScalarBuffer.size(), "%.6f", entity.castle.maxHp);
				json += indent + "    \"maxHp\": " + castleScalarBuffer.data() + "\n";
				json += indent + "  },\n";
			}
			json += indent + "  \"isCatapult\": " + std::string(entity.isCatapult ? "true" : "false") + ",\n";
			{
				std::array<char, 32> catapultScalarBuffer{};
				json += indent + "  \"catapult\": {\n";
				json += indent + "    \"yawEntityName\": \"" + escapeJson(entity.catapult.yawEntityName) + "\",\n";
				json += indent + "    \"armEntityName\": \"" + escapeJson(entity.catapult.armEntityName) + "\",\n";
				std::snprintf(
					catapultScalarBuffer.data(), catapultScalarBuffer.size(), "%.6f",
					entity.catapult.minPitchDegrees);
				json += indent + "    \"minPitchDegrees\": " + catapultScalarBuffer.data() + ",\n";
				std::snprintf(
					catapultScalarBuffer.data(), catapultScalarBuffer.size(), "%.6f",
					entity.catapult.maxPitchDegrees);
				json += indent + "    \"maxPitchDegrees\": " + catapultScalarBuffer.data() + ",\n";
				std::snprintf(
					catapultScalarBuffer.data(), catapultScalarBuffer.size(), "%.6f", entity.catapult.launchSpeed);
				json += indent + "    \"launchSpeed\": " + catapultScalarBuffer.data() + "\n";
				json += indent + "  },\n";
			}
			json += indent + "  \"isTextMesh\": " + std::string(entity.isTextMesh ? "true" : "false") + ",\n";
			json += indent + "  \"isCineCamera\": " + std::string(entity.isCineCamera ? "true" : "false") + ",\n";

			// Written unconditionally, like "terrain" above - the nested block
			// costs a few bytes on entities that aren't lights, and always
			// emitting it keeps the writer branch-free and the diff of a saved
			// scene stable when a flag is toggled.
			json += indent + "  \"isLight\": " + boolToJson(entity.isLight) + ",\n";
			json += indent + "  \"light\": {\n";
			json += indent + "    \"type\": \"" + std::string(lightTypeName(entity.light.type)) + "\",\n";
			json += indent + "    \"color\": " + vec3ToJsonArray(entity.light.color) + ",\n";
			json += indent + "    \"intensity\": " + floatToJson(entity.light.intensity) + ",\n";
			json += indent + "    \"range\": " + floatToJson(entity.light.range) + ",\n";
			json += indent + "    \"innerCone\": " + floatToJson(entity.light.innerConeDegrees) + ",\n";
			json += indent + "    \"outerCone\": " + floatToJson(entity.light.outerConeDegrees) + ",\n";
			json += indent + "    \"castShadows\": " + boolToJson(entity.light.castShadows) + ",\n";
			json += indent + "    \"shadowBias\": " + floatToJson(entity.light.shadowBias) + "\n";
			json += indent + "  },\n";

			json += indent + "  \"isCamera\": " + boolToJson(entity.isCamera) + ",\n";
			json += indent + "  \"camera\": {\n";
			json += indent + "    \"fieldOfView\": " + floatToJson(entity.camera.fieldOfViewDegrees) + ",\n";
			json += indent + "    \"nearClip\": " + floatToJson(entity.camera.nearClip) + ",\n";
			json += indent + "    \"farClip\": " + floatToJson(entity.camera.farClip) + ",\n";
			json += indent + "    \"clearColor\": " + vec3ToJsonArray(entity.camera.clearColor) + ",\n";
			json += indent + "    \"isMainCamera\": " + boolToJson(entity.camera.isMainCamera) + "\n";
			json += indent + "  },\n";

			json += indent + "  \"isUIElement\": " + boolToJson(entity.isUIElement) + ",\n";
			json += indent + "  \"ui\": {\n";
			json += indent + "    \"kind\": \"" + std::string(uiElementKindName(entity.ui.kind)) + "\",\n";
			json += indent + "    \"anchor\": \"" + std::string(uiAnchorName(entity.ui.anchor)) + "\",\n";
			json += indent + "    \"offset\": " + vec2ToJsonArray(entity.ui.offsetPixels) + ",\n";
			json += indent + "    \"size\": " + vec2ToJsonArray(entity.ui.sizePixels) + ",\n";
			json += indent + "    \"color\": " + vec3ToJsonArray(entity.ui.color) + ",\n";
			json += indent + "    \"opacity\": " + floatToJson(entity.ui.opacity) + ",\n";
			json += indent + "    \"text\": \"" + escapeJson(entity.ui.text) + "\",\n";
			json += indent + "    \"fontPath\": \"" + escapeJson(entity.ui.fontPath) + "\",\n";
			json += indent + "    \"fontSize\": " + floatToJson(entity.ui.fontSizePixels) + ",\n";
			json += indent + "    \"imagePath\": \"" + escapeJson(entity.ui.imagePath) + "\",\n";
			json += indent + "    \"thickness\": " + floatToJson(entity.ui.thicknessPixels) + ",\n";
			json += indent + "    \"gap\": " + floatToJson(entity.ui.gapPixels) + "\n";
			json += indent + "  },\n";

			json += indent + "  \"isTerrain\": " + std::string(entity.isTerrain ? "true" : "false") + ",\n";

			std::array<char, 32> terrainScalarBuffer{};
			json += indent + "  \"terrain\": {\n";
			std::snprintf(terrainScalarBuffer.data(), terrainScalarBuffer.size(), "%d", entity.terrain.resolution);
			json += indent + "    \"resolution\": " + terrainScalarBuffer.data() + ",\n";
			std::snprintf(terrainScalarBuffer.data(), terrainScalarBuffer.size(), "%.6f", entity.terrain.worldSize);
			json += indent + "    \"worldSize\": " + terrainScalarBuffer.data() + ",\n";
			std::snprintf(terrainScalarBuffer.data(), terrainScalarBuffer.size(), "%.6f", entity.terrain.heightScale);
			json += indent + "    \"heightScale\": " + terrainScalarBuffer.data() + ",\n";
			json += indent + "    \"uvScale\": " + vec2ToJsonArray(entity.terrain.uvScale) + ",\n";
			json += indent + "    \"heights\": [";
			for (std::size_t index = 0; index < entity.terrain.heights.size(); ++index)
			{
				std::array<char, 24> heightBuffer{};
				std::snprintf(heightBuffer.data(), heightBuffer.size(), "%.6f", entity.terrain.heights[index]);
				json += (index == 0 ? "" : ",") + std::string(heightBuffer.data());
			}
			json += "],\n";
			json += indent + "    \"layers\": [";
			for (std::size_t index = 0; index < entity.terrain.layers.size(); ++index)
			{
				const TerrainLayerData& layer = entity.terrain.layers[index];
				json += (index == 0 ? "\n" : ",\n") + indent + "      {\n";
				json += indent + "        \"diffusePath\": \"" + escapeJson(layer.diffusePath) + "\",\n";
				json += indent + "        \"normalPath\": \"" + escapeJson(layer.normalPath) + "\",\n";
				json += indent + "        \"heightPath\": \"" + escapeJson(layer.heightPath) + "\"\n";
				json += indent + "      }";
			}
			json += entity.terrain.layers.empty() ? "],\n" : ("\n" + indent + "    ],\n");
			json += indent + "    \"splatWeights\": [";
			for (std::size_t index = 0; index < entity.terrain.splatWeights.size(); ++index)
			{
				std::array<char, 24> weightBuffer{};
				std::snprintf(weightBuffer.data(), weightBuffer.size(), "%.6f", entity.terrain.splatWeights[index]);
				json += (index == 0 ? "" : ",") + std::string(weightBuffer.data());
			}
			json += "]\n";
			json += indent + "  },\n";

			json += indent + "  \"isImportedMesh\": " + std::string(entity.isImportedMesh ? "true" : "false") + ",\n";
			json += indent + "  \"importedMesh\": {\n";
			json += indent + "    \"sourcePath\": \"" + escapeJson(entity.importedMesh.sourcePath) + "\"\n";
			json += indent + "  },\n";

			std::array<char, 32> textFontSizeBuffer{};
			std::snprintf(textFontSizeBuffer.data(), textFontSizeBuffer.size(), "%.6f", entity.textMesh.fontSize);
			std::array<char, 32> textDepthBuffer{};
			std::snprintf(textDepthBuffer.data(), textDepthBuffer.size(), "%.6f", entity.textMesh.depth);
			json += indent + "  \"textMesh\": {\n";
			json += indent + "    \"content\": \"" + escapeJson(entity.textMesh.content) + "\",\n";
			json += indent + "    \"fontPath\": \"" + escapeJson(entity.textMesh.fontPath) + "\",\n";
			json += indent + "    \"fontSize\": " + textFontSizeBuffer.data() + ",\n";
			json += indent + "    \"depth\": " + textDepthBuffer.data() + "\n";
			json += indent + "  },\n";

			std::array<char, 32> fpsEyeHeightBuffer{};
			std::snprintf(fpsEyeHeightBuffer.data(), fpsEyeHeightBuffer.size(), "%.6f", entity.cameraRig.fpsEyeHeight);
			std::array<char, 32> thirdPersonDistanceBuffer{};
			std::snprintf(
				thirdPersonDistanceBuffer.data(), thirdPersonDistanceBuffer.size(), "%.6f",
				entity.cameraRig.thirdPersonDistance);
			std::array<char, 32> thirdPersonHeightBuffer{};
			std::snprintf(
				thirdPersonHeightBuffer.data(), thirdPersonHeightBuffer.size(), "%.6f",
				entity.cameraRig.thirdPersonHeight);
			std::array<char, 32> thirdPersonAimHeightBuffer{};
			std::snprintf(
				thirdPersonAimHeightBuffer.data(), thirdPersonAimHeightBuffer.size(), "%.6f",
				entity.cameraRig.thirdPersonAimHeight);
			std::array<char, 32> thirdPersonYawOffsetBuffer{};
			std::snprintf(
				thirdPersonYawOffsetBuffer.data(), thirdPersonYawOffsetBuffer.size(), "%.6f",
				entity.cameraRig.thirdPersonYawOffsetDegrees);
			json += indent + "  \"cameraRig\": {\n";
			json += indent + "    \"fpsEyeHeight\": " + fpsEyeHeightBuffer.data() + ",\n";
			json += indent + "    \"thirdPersonDistance\": " + thirdPersonDistanceBuffer.data() + ",\n";
			json += indent + "    \"thirdPersonHeight\": " + thirdPersonHeightBuffer.data() + ",\n";
			json += indent + "    \"thirdPersonAimHeight\": " + thirdPersonAimHeightBuffer.data() + ",\n";
			// Last field in cameraRig now that lockCursor is gone - no trailing
			// comma, which this project's own parser rejects outright.
			json += indent + "    \"thirdPersonYawOffsetDegrees\": " + thirdPersonYawOffsetBuffer.data() + "\n";
			json += indent + "  },\n";

			json += indent + "  \"scripts\": [";
			for (std::size_t index = 0; index < entity.scripts.size(); ++index)
			{
				json += (index == 0 ? "\n" : ",\n");
				json += indent + "    \"" + escapeJson(entity.scripts[index]) + "\"";
			}
			json += entity.scripts.empty() ? "],\n" : ("\n" + indent + "  ],\n");

			json += indent + "  \"animation\": {\n";
			json += indent + "    \"enabled\": " + std::string(entity.animation.enabled ? "true" : "false") + ",\n";
			json += indent + "    \"looping\": " + std::string(entity.animation.looping ? "true" : "false") + ",\n";
			json += indent + "    \"keyframes\": [";
			for (std::size_t index = 0; index < entity.animation.keyframes.size(); ++index)
			{
				json += (index == 0 ? "\n" : ",\n");
				appendKeyframe(json, entity.animation.keyframes[index], indent + "      ");
			}
			json += entity.animation.keyframes.empty() ? "]\n" : ("\n" + indent + "    ]\n");
			json += indent + "  }\n";

			json += indent + "}";
		}
	}

	namespace
	{
		// Storyboard shots ride along in the scene file: a shot references the
		// scene's own cine cameras, so the list is meaningless without the
		// scene it was recorded against. Absent from files written before this
		// existed, which load as an empty storyboard rather than failing.
		void appendShot(std::string& json, const CineShot& shot, const std::string& indent)
		{
			json += indent + "{\n";
			json += indent + "  \"name\": \"" + escapeJson(shot.name) + "\",\n";
			json += indent + "  \"looping\": " +
				std::string(shot.cameraPath.looping ? "true" : "false") + ",\n";
			json += indent + "  \"keyframes\": [";
			for (std::size_t index = 0; index < shot.cameraPath.keyframes.size(); ++index)
			{
				json += (index == 0 ? "\n" : ",\n");
				appendKeyframe(json, shot.cameraPath.keyframes[index], indent + "    ");
			}
			json += shot.cameraPath.keyframes.empty() ? "],\n" : ("\n" + indent + "  ],\n");
			json += indent + "  \"audioCues\": [";
			for (std::size_t index = 0; index < shot.audioCues.size(); ++index)
			{
				const AudioCue& cue = shot.audioCues[index];
				std::array<char, 32> timeBuffer{};
				std::array<char, 32> volumeBuffer{};
				std::snprintf(timeBuffer.data(), timeBuffer.size(), "%.4f", static_cast<double>(cue.time));
				std::snprintf(volumeBuffer.data(), volumeBuffer.size(), "%.4f", static_cast<double>(cue.volume));
				json += (index == 0 ? "\n" : ",\n");
				json += indent + "    {\"time\": " + timeBuffer.data() +
					", \"clipPath\": \"" + escapeJson(cue.clipPath) +
					"\", \"volume\": " + volumeBuffer.data() + "}";
			}
			json += shot.audioCues.empty() ? "]\n" : ("\n" + indent + "  ]\n");
			json += indent + "}";
		}

		// Mirrors appendShot. A malformed shot is skipped rather than failing
		// the whole scene load - losing one storyboard entry beats refusing to
		// open the scene it belongs to.
		std::vector<CineShot> parseStoryboard(const json::Value& root)
		{
			std::vector<CineShot> shots;
			const json::Value* array = root.find("storyboard");
			if (array == nullptr || array->type != json::Value::Type::Array)
			{
				return shots;
			}
			for (const json::Value& shotValue : array->arrayValue)
			{
				if (shotValue.type != json::Value::Type::Object)
				{
					continue;
				}
				CineShot shot;
				shot.name = readString(shotValue, "name");
				shot.cameraPath.enabled = true;
				shot.cameraPath.looping = readBool(shotValue, "looping", false);
				if (const json::Value* keyframes = shotValue.find("keyframes");
					keyframes != nullptr && keyframes->type == json::Value::Type::Array)
				{
					for (const json::Value& keyframeValue : keyframes->arrayValue)
					{
						if (keyframeValue.type != json::Value::Type::Object)
						{
							continue;
						}
						TransformKeyframe keyframe;
						keyframe.time = readFloat(keyframeValue, "time", 0.0F);
						keyframe.position = readVec3(keyframeValue, "position", glm::vec3(0.0F));
						keyframe.rotationEuler = readVec3(keyframeValue, "rotation", glm::vec3(0.0F));
						keyframe.scale = readVec3(keyframeValue, "scale", glm::vec3(1.0F));
						shot.cameraPath.keyframes.push_back(keyframe);
					}
				}
				if (const json::Value* cues = shotValue.find("audioCues");
					cues != nullptr && cues->type == json::Value::Type::Array)
				{
					for (const json::Value& cueValue : cues->arrayValue)
					{
						if (cueValue.type != json::Value::Type::Object)
						{
							continue;
						}
						AudioCue cue;
						cue.time = readFloat(cueValue, "time", 0.0F);
						cue.clipPath = readString(cueValue, "clipPath");
						cue.volume = readFloat(cueValue, "volume", 1.0F);
						if (!cue.clipPath.empty())
						{
							(void)insertAudioCueSorted(shot.audioCues, std::move(cue));
						}
					}
				}
				shots.push_back(std::move(shot));
			}
			return shots;
		}
	}


	SceneSaveResult saveScene(
		const std::filesystem::path& filePath,
		const std::vector<SceneEntity>& entities,
		const std::vector<CineShot>& shots)
	{
		if (filePath.has_parent_path() && !filePath.parent_path().empty())
		{
			std::error_code directoryError;
			std::filesystem::create_directories(filePath.parent_path(), directoryError);
			if (directoryError)
			{
				return {false, "Could not create the scene folder: " + directoryError.message()};
			}
		}

		std::string json = "{\n";
		json += "  \"format\": \"GameForgerScene\",\n";
		json += "  \"version\": 8,\n";
		json += "  \"entities\": [";
		for (std::size_t index = 0; index < entities.size(); ++index)
		{
			json += (index == 0 ? "\n" : ",\n");
			appendEntity(json, entities[index], "    ");
		}
		json += entities.empty() ? "],\n" : "\n  ],\n";
		json += "  \"storyboard\": [";
		for (std::size_t index = 0; index < shots.size(); ++index)
		{
			json += (index == 0 ? "\n" : ",\n");
			appendShot(json, shots[index], "    ");
		}
		json += shots.empty() ? "]\n" : "\n  ]\n";
		json += "}\n";

		// Atomic write: serialize into a sibling temp file, flush + close,
		// then rename it over the destination. A crash, disk-full, or kill
		// during the old direct-truncation path could leave the destination
		// zero bytes / half-written / corrupt; the rename only happens once
		// the temp file is fully on disk, so the previous good copy survives
		// any failure between flush and rename. The .bak is a manual
		// recovery affordance: the most recent pre-save copy stays on disk.
		const std::filesystem::path tempPath = filePath.string() + ".tmp";
		{
			std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
			if (!output)
			{
				return {false, "Could not write to " + tempPath.string()};
			}
			output << json;
			if (!output)
			{
				return {false, "Failed while writing " + tempPath.string()};
			}
			output.flush();
			if (!output)
			{
				return {false, "Failed to flush " + tempPath.string()};
			}
		}
		// On flush+close failure the temp file may be left behind; if rename
		// fails (e.g. AV locking the destination) we don't lose the previous
		// good file. Keep one side-by-side backup of the prior destination.
		const std::filesystem::path backupPath = filePath.string() + ".bak";
		std::error_code ec;
		if (std::filesystem::exists(filePath, ec))
		{
			std::filesystem::copy(
				filePath, backupPath,
				std::filesystem::copy_options::overwrite_existing, ec);
			std::filesystem::remove(filePath, ec);
		}
		std::filesystem::rename(tempPath, filePath, ec);
		if (ec)
		{
			std::filesystem::remove(tempPath, ec);
			return {false, "Could not replace " + filePath.string() + " with the new contents: " + ec.message()};
		}

		return {true, "Saved " + std::to_string(entities.size()) + " entity(ies) to " + filePath.string()};
	}

	SceneLoadResult loadScene(const std::filesystem::path& filePath)
	{
		std::ifstream input(filePath, std::ios::binary);
		if (!input)
		{
			return {false, "Could not open " + filePath.string(), {}};
		}
		const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());

		const std::optional<json::Value> parsed = json::parse(text);
		if (!parsed.has_value() || parsed->type != json::Value::Type::Object)
		{
			return {false, filePath.string() + " is not a valid GameForger scene file.", {}};
		}

		// Reject any JSON object with an `entities` array that wasn't written
		// by this engine - the writer always emits format+version, and the
		// loader was previously accepting Project.json, package manifests, or
		// any third-party JSON with the right shape and silently overwriting
		// the user's current scene with default-fallback fields.
		const json::Value* formatField = parsed->find("format");
		if (formatField == nullptr || formatField->type != json::Value::Type::String ||
			formatField->stringValue != "GameForgerScene")
		{
			return {false,
				filePath.string() + " is not a GameForger scene file (missing or wrong \"format\").",
				{}};
		}
		const json::Value* versionField = parsed->find("version");
		if (versionField == nullptr)
		{
			return {false, filePath.string() + " has no \"version\" field.", {}};
		}
		if (versionField->type != json::Value::Type::Number)
		{
			return {false, filePath.string() + " has a non-numeric \"version\" field.", {}};
		}
		const double version = versionField->numberValue;
		// The current writer emits version 8. Anything in [1, 8] is treated
		// as "legacy"; newer-than-current is rejected with an explicit
		// message rather than silently misinterpreting unknown fields.
		constexpr double kCurrentVersion = 8.0;
		if (version > kCurrentVersion)
		{
			return {false,
				filePath.string() + " is scene version " + std::to_string(version) +
					", newer than this editor supports (" +
					std::to_string(static_cast<int>(kCurrentVersion)) + ").",
				{}};
		}

		const json::Value* entitiesField = parsed->find("entities");
		if (entitiesField == nullptr || entitiesField->type != json::Value::Type::Array)
		{
			return {false, filePath.string() + " has no \"entities\" array.", {}};
		}

		std::vector<SceneEntity> entities;
		entities.reserve(entitiesField->arrayValue.size());
		int skipped = 0;
		for (const json::Value& entityValue : entitiesField->arrayValue)
		{
			if (std::optional<SceneEntity> entity = parseEntity(entityValue))
			{
				entities.push_back(std::move(*entity));
			}
			else
			{
				++skipped;
			}
		}

		std::string message =
			"Loaded " + std::to_string(entities.size()) + " entity(ies) from " + filePath.string();
		if (skipped > 0)
		{
			message += " (" + std::to_string(skipped) + " skipped)";
		}
		std::vector<CineShot> shots = parseStoryboard(*parsed);
		if (!shots.empty())
		{
			message += ", " + std::to_string(shots.size()) + " storyboard shot(s)";
		}
		return {true, message, std::move(entities), std::move(shots)};
	}
}
