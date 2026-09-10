#include "GameForger/Editor/AIAnimationGenerator.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>

#include "GameForger/Editor/AIChatResponse.hpp"
#include "GameForger/Editor/Json.hpp"

namespace gameforger::editor
{
	namespace
	{
		const char* primitiveName(const PrimitiveType type)
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
			return "shape";
		}

		std::string formatVec3(const glm::vec3& value)
		{
			std::array<char, 64> buffer{};
			std::snprintf(buffer.data(), buffer.size(), "[%.2f,%.2f,%.2f]", value.x, value.y, value.z);
			return buffer.data();
		}

		std::string trimCopy(std::string text)
		{
			const auto isSpace = [](const unsigned char c) { return std::isspace(c) != 0; };
			while (!text.empty() && isSpace(static_cast<unsigned char>(text.front())))
			{
				text.erase(text.begin());
			}
			while (!text.empty() && isSpace(static_cast<unsigned char>(text.back())))
			{
				text.pop_back();
			}
			return text;
		}

		// Models sometimes ignore the "no code fences" instruction; strip them defensively.
		std::string stripCodeFences(std::string text)
		{
			text = trimCopy(std::move(text));
			if (text.starts_with("```"))
			{
				const std::size_t firstNewline = text.find('\n');
				text = firstNewline == std::string::npos ? std::string{} : text.substr(firstNewline + 1);
				if (text.ends_with("```"))
				{
					text = text.substr(0, text.size() - 3);
				}
				text = trimCopy(std::move(text));
			}
			return text;
		}

		std::string readStringOr(const json::Value& obj, const std::string& key, const std::string& fallback = {})
		{
			const json::Value* field = obj.find(key);
			if (field == nullptr)
			{
				return fallback;
			}
			return field->asString().value_or(fallback);
		}

		float readNumberOr(const json::Value& obj, const std::string& key, const float fallback)
		{
			const json::Value* field = obj.find(key);
			if (field == nullptr)
			{
				return fallback;
			}
			return static_cast<float>(field->asNumber().value_or(static_cast<double>(fallback)));
		}

		bool readBoolOr(const json::Value& obj, const std::string& key, const bool fallback)
		{
			const json::Value* field = obj.find(key);
			if (field == nullptr)
			{
				return fallback;
			}
			return field->asBool().value_or(fallback);
		}

		glm::vec3 readVec3Or(const json::Value& obj, const std::string& key, const glm::vec3& fallback)
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

		const SceneEntity* findByName(const std::vector<SceneEntity>& entities, const std::string& name)
		{
			const auto iterator = std::find_if(
				entities.begin(), entities.end(), [&name](const SceneEntity& entity) { return entity.name == name; });
			return iterator == entities.end() ? nullptr : &(*iterator);
		}
	}

	AIAnimationResult generateAnimation(
		const AIProviderClient& client,
		const std::string& providerId,
		const std::vector<SceneEntity>& targetEntities,
		const std::string& userPrompt)
	{
		if (targetEntities.empty())
		{
			return {false, "Select at least one object to animate first.", {}};
		}

		std::string context;
		for (const SceneEntity& entity : targetEntities)
		{
			context += "- \"" + entity.name + "\" (" + primitiveName(entity.primitive) +
				"): position=" + formatVec3(entity.position) + ", rotation=" + formatVec3(entity.rotationEuler) +
				" (degrees), scale=" + formatVec3(entity.scale) + ", pivot=" + formatVec3(entity.pivotOffset) + "\n";
		}

		AIProviderRequest request;
		request.systemPrompt =
			"You are the animation layer of the GameForgerAI game editor. The user has selected one or more "
			"objects and describes how they should move. Reply with ONLY a JSON object: no markdown code "
			"fences, no prose before or after it.\n\n"
			"Schema:\n"
			"{\"animations\":[{\"entity\":string,\"loop\":boolean,\"keyframes\":["
			"{\"time\":number,\"position\":[x,y,z],\"rotation\":[x,y,z] in degrees,\"scale\":[x,y,z]}]}]}\n\n"
			"Rules:\n"
			"- \"entity\" must exactly match one of the object names listed below - never invent a new name.\n"
			"- Give every animated object at least 2 keyframes (a start and an end pose); add more for "
			"multi-step motion.\n"
			"- position/rotation/scale in each keyframe are ABSOLUTE world-space values, not deltas. Rotation "
			"is Euler degrees. You may omit a field in a keyframe to keep that object's current value.\n"
			"- The first keyframe would normally start at time 0 at (or near) the object's CURRENT transform "
			"given below, unless the request implies otherwise.\n"
			"- Rotations pivot around each object's \"pivot\" point listed below (e.g. a door hinge) - reason "
			"about the swing accordingly, you don't need to compensate for it yourself.\n"
			"- If the prompt names a specific object, animate only that one. If it's generic (e.g. \"make them "
			"bounce\"), animate every object listed below, adapting per object as needed.\n"
			"- Only reference objects from the list below.\n\n"
			"Objects currently selected:\n" +
			context;
		request.prompt = userPrompt;

		const AIProviderResponse response = client.send(providerId, request);
		if (!response.success)
		{
			if (!response.error.empty())
			{
				return {false, response.error, {}};
			}
			const std::string detail = extractErrorMessage(response.body);
			return {false, detail.empty() ? "AI provider request failed." : detail, {}};
		}

		const std::optional<std::string> content = extractChatMessageContent(response.body);
		if (!content.has_value() || content->empty())
		{
			return {false, "The AI response did not contain any content.", {}};
		}

		const std::string jsonText = stripCodeFences(*content);
		const std::optional<json::Value> parsed = json::parse(jsonText);
		if (!parsed.has_value() || parsed->type != json::Value::Type::Object)
		{
			return {false, "The AI did not return valid animation data.", {}};
		}

		const json::Value* animationsField = parsed->find("animations");
		if (animationsField == nullptr || animationsField->type != json::Value::Type::Array)
		{
			return {false, "The AI response was missing an \"animations\" list.", {}};
		}

		AIAnimationResult result;
		int skipped = 0;
		for (const json::Value& element : animationsField->arrayValue)
		{
			if (element.type != json::Value::Type::Object)
			{
				++skipped;
				continue;
			}
			const std::string entityName = readStringOr(element, "entity");
			const SceneEntity* baseline = findByName(targetEntities, entityName);
			if (baseline == nullptr)
			{
				++skipped;
				continue;
			}

			const json::Value* keyframesField = element.find("keyframes");
			if (keyframesField == nullptr || keyframesField->type != json::Value::Type::Array ||
				keyframesField->arrayValue.empty())
			{
				++skipped;
				continue;
			}

			GeneratedEntityAnimation animation;
			animation.entityName = entityName;
			animation.looping = readBoolOr(element, "loop", false);

			for (const json::Value& keyframeElement : keyframesField->arrayValue)
			{
				if (keyframeElement.type != json::Value::Type::Object)
				{
					continue;
				}
				TransformKeyframe keyframe;
				keyframe.time = readNumberOr(keyframeElement, "time", 0.0F);
				// Reject negative times and non-finite values: the
				// sampling loop expects times in [0, +inf) and would
				// either silently produce undefined animation or
				// propagate NaN into every transform it touches.
				if (!std::isfinite(keyframe.time) || keyframe.time < 0.0F)
				{
					continue;
				}
				keyframe.position = readVec3Or(keyframeElement, "position", baseline->position);
				keyframe.rotationEuler = readVec3Or(keyframeElement, "rotation", baseline->rotationEuler);
				keyframe.scale = readVec3Or(keyframeElement, "scale", baseline->scale);
				animation.keyframes.push_back(keyframe);
			}
			if (animation.keyframes.empty())
			{
				++skipped;
				continue;
			}
			std::sort(
				animation.keyframes.begin(),
				animation.keyframes.end(),
				[](const TransformKeyframe& a, const TransformKeyframe& b) { return a.time < b.time; });
			result.animations.push_back(std::move(animation));
		}

		result.success = !result.animations.empty();
		if (result.success)
		{
			result.message = std::to_string(result.animations.size()) + " animation(s) generated" +
				(skipped > 0 ? (" (" + std::to_string(skipped) + " skipped)") : "") + ".";
		}
		else
		{
			result.message = "The AI didn't return any usable animations for the selected object(s).";
		}
		return result;
	}
}
