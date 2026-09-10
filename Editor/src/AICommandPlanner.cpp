#include "GameForger/Editor/AICommandPlanner.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <type_traits>
#include <utility>

#include "GameForger/Editor/AIChatResponse.hpp"
#include "GameForger/Editor/AICommandBus.hpp"
#include "GameForger/Editor/Json.hpp"
#include "GameForger/Editor/Transform.hpp"

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

		// Each AI Forge request is a fresh, stateless call with no memory of
		// prior prompts (AIProviderClient doesn't thread conversation
		// history) - this snapshot is the model's only way to know what
		// already exists, so it can target existing entities by name instead
		// of only ever creating new ones.
		std::string describeCurrentScene(const std::vector<SceneEntity>& entities)
		{
			if (entities.empty())
			{
				return "The scene is currently empty - there are no existing entities yet.";
			}
			std::string summary =
				"Entities that already exist in the scene right now (use these EXACT names for "
				"rename_entity/duplicate_entity/set_position/set_rotation/set_scale/set_color/add_tag/"
				"remove_tag/set_pivot/delete_entity/attach_script/detach_script - do not create a new entity "
				"with one of these names). An entity can have zero or more tags, in priority order (first = "
				"primary) - add_tag appends a new tag, remove_tag takes one off:\n";
			std::array<char, 220> line{};
			for (const SceneEntity& entity : entities)
			{
				std::string tagsText;
				for (const std::string& tag : entity.tags)
				{
					tagsText += (tagsText.empty() ? "" : ", ") + tag;
				}
				if (tagsText.empty())
				{
					tagsText = "none";
				}
				std::snprintf(
					line.data(),
					line.size(),
					"- \"%s\" (%s, tags: %s) at [%.2f, %.2f, %.2f]\n",
					entity.name.c_str(),
					primitiveName(entity.primitive),
					tagsText.c_str(),
					static_cast<double>(entity.position.x),
					static_cast<double>(entity.position.y),
					static_cast<double>(entity.position.z));
				summary += line.data();
			}
			return summary;
		}

		PrimitiveType parsePrimitive(const std::string& text)
		{
			if (text == "sphere") return PrimitiveType::Sphere;
			if (text == "cylinder") return PrimitiveType::Cylinder;
			if (text == "cone") return PrimitiveType::Cone;
			if (text == "plane") return PrimitiveType::Plane;
			if (text == "capsule") return PrimitiveType::Capsule;
			if (text == "empty") return PrimitiveType::Empty;
			return PrimitiveType::Cube;
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

		// The "content" field of a create_script op is nested two JSON layers
		// deep (chat response envelope, then the plan array itself), so it goes
		// through two legitimate unescape passes. Models occasionally over-escape
		// anyway - writing "\\n" where a single "\n" was correct - which survives
		// both passes as a literal two-character "\n" instead of a real line
		// break. This can affect the whole file (collapsing it into one Lua
		// comment) or just a few lines (e.g. inside one table constructor), so a
		// whole-file heuristic isn't reliable. Lua never assigns meaning to a
		// bare backslash outside a string literal, so any \n/\t/\r found outside
		// single/double-quoted strings is unambiguously this artifact rather
		// than intentional script text; escapes genuinely inside a Lua string
		// literal are left completely untouched.
		std::string recoverOverEscapedNewlines(const std::string& text)
		{
			std::string result;
			result.reserve(text.size());
			char stringQuote = '\0';
			for (std::size_t index = 0; index < text.size(); ++index)
			{
				const char current = text[index];
				if (stringQuote != '\0')
				{
					result += current;
					if (current == '\\' && index + 1 < text.size())
					{
						result += text[index + 1];
						++index;
						continue;
					}
					if (current == stringQuote)
					{
						stringQuote = '\0';
					}
					continue;
				}
				if (current == '"' || current == '\'')
				{
					stringQuote = current;
					result += current;
					continue;
				}
				if (current == '\\' && index + 1 < text.size() &&
					(text[index + 1] == 'n' || text[index + 1] == 't' || text[index + 1] == 'r'))
				{
					result += text[index + 1] == 'n' ? '\n' : (text[index + 1] == 't' ? '\t' : '\r');
					++index;
					continue;
				}
				result += current;
			}
			return result;
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

		float readFloatOr(const json::Value& obj, const std::string& key, const float fallback)
		{
			const json::Value* field = obj.find(key);
			if (field == nullptr)
			{
				return fallback;
			}
			const std::optional<double> number = field->asNumber();
			return number.has_value() ? static_cast<float>(*number) : fallback;
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

		std::optional<AIEditorCommand> buildCommand(const json::Value& obj)
		{
			const std::string op = readStringOr(obj, "op");
			if (op == "create_entity")
			{
				CreateEntityCommand command;
				command.name = readStringOr(obj, "name");
				command.primitive = parsePrimitive(readStringOr(obj, "primitive", "cube"));
				command.position = readVec3Or(obj, "position", glm::vec3(0.0F));
				return command;
			}
			if (op == "create_terrain")
			{
				CreateTerrainCommand command;
				command.name = readStringOr(obj, "name");
				command.position = readVec3Or(obj, "position", glm::vec3(0.0F));
				command.shape = readStringOr(obj, "shape", "flat") == "mountain" ? "mountain" : "flat";
				command.worldSize = readFloatOr(obj, "baseSize", 50.0F);
				// heights[]=1.0 reaches +heightScale/2 above the flat baseline
				// (see EditorScene.hpp's TerrainData::heightScale doc), so
				// heightScale must be double the requested peak height for
				// "peakHeight" to mean what it says.
				command.heightScale = readFloatOr(obj, "peakHeight", 12.0F) * 2.0F;
				return command;
			}
			if (op == "delete_entity")
			{
				return DeleteEntityCommand{readStringOr(obj, "name")};
			}
			if (op == "rename_entity")
			{
				return RenameEntityCommand{readStringOr(obj, "name"), readStringOr(obj, "newName")};
			}
			if (op == "duplicate_entity")
			{
				return DuplicateEntityCommand{readStringOr(obj, "name")};
			}
			if (op == "set_position")
			{
				return SetPositionCommand{readStringOr(obj, "name"), readVec3Or(obj, "position", glm::vec3(0.0F))};
			}
			if (op == "set_rotation")
			{
				return SetPropertyCommand{
					readStringOr(obj, "name"), "Transform", "rotation", readVec3Or(obj, "rotation", glm::vec3(0.0F))};
			}
			if (op == "set_scale")
			{
				return SetPropertyCommand{
					readStringOr(obj, "name"), "Transform", "scale", readVec3Or(obj, "scale", glm::vec3(1.0F))};
			}
			if (op == "set_color")
			{
				return SetPropertyCommand{
					readStringOr(obj, "name"), "Renderer", "color", readVec3Or(obj, "color", glm::vec3(0.8F))};
			}
			if (op == "set_tag" || op == "add_tag")
			{
				return AddTagCommand{readStringOr(obj, "name"), readStringOr(obj, "tag")};
			}
			if (op == "remove_tag")
			{
				return RemoveTagCommand{readStringOr(obj, "name"), readStringOr(obj, "tag")};
			}
			if (op == "set_pivot")
			{
				glm::vec3 pivot(0.0F);
				if (const json::Value* presetField = obj.find("pivotPreset"))
				{
					if (const std::optional<std::string> presetName = presetField->asString())
					{
						if (const std::optional<glm::vec3> resolved = resolvePivotPreset(*presetName))
						{
							pivot = *resolved;
						}
					}
				}
				else
				{
					pivot = readVec3Or(obj, "pivot", glm::vec3(0.0F));
				}
				return SetPropertyCommand{readStringOr(obj, "name"), "Transform", "pivot", pivot};
			}
			if (op == "create_script")
			{
				return CreateScriptCommand{
					readStringOr(obj, "path"), "lua", recoverOverEscapedNewlines(readStringOr(obj, "content"))};
			}
			if (op == "attach_script")
			{
				return AttachScriptCommand{readStringOr(obj, "name"), readStringOr(obj, "path")};
			}
			if (op == "detach_script")
			{
				return DetachScriptCommand{readStringOr(obj, "name"), readStringOr(obj, "path")};
			}
			return std::nullopt;
		}
	}

	std::string describeCommand(const AIEditorCommand& command)
	{
		return std::visit(
			[](const auto& value) -> std::string
			{
				using Command = std::decay_t<decltype(value)>;
				if constexpr (std::is_same_v<Command, CreateEntityCommand>)
				{
					return "Create " + std::string(primitiveName(value.primitive)) + " '" + value.name + "'";
				}
				else if constexpr (std::is_same_v<Command, DeleteEntityCommand>)
				{
					return "Delete '" + value.entityName + "'";
				}
				else if constexpr (std::is_same_v<Command, RenameEntityCommand>)
				{
					return "Rename '" + value.entityName + "' to '" + value.newName + "'";
				}
				else if constexpr (std::is_same_v<Command, DuplicateEntityCommand>)
				{
					return "Duplicate '" + value.entityName + "'";
				}
				else if constexpr (std::is_same_v<Command, SetPositionCommand>)
				{
					return "Move '" + value.entityName + "'";
				}
				else if constexpr (std::is_same_v<Command, RequestAnimationCommand>)
				{
					return "Play animation '" + value.action + "' on '" + value.characterName + "'";
				}
				else if constexpr (std::is_same_v<Command, CreateScriptCommand>)
				{
					return "Create script '" + value.path + "'";
				}
				else if constexpr (std::is_same_v<Command, AttachScriptCommand>)
				{
					return "Attach script '" + value.scriptPath + "' to '" + value.entityName + "'";
				}
				else if constexpr (std::is_same_v<Command, DetachScriptCommand>)
				{
					return "Detach script '" + value.scriptPath + "' from '" + value.entityName + "'";
				}
				else if constexpr (std::is_same_v<Command, AddTagCommand>)
				{
					return "Add tag '" + value.tag + "' to '" + value.entityName + "'";
				}
				else if constexpr (std::is_same_v<Command, RemoveTagCommand>)
				{
					return "Remove tag '" + value.tag + "' from '" + value.entityName + "'";
				}
				else if constexpr (std::is_same_v<Command, CreateTextMeshCommand>)
				{
					return "Create text mesh \"" + value.content + "\"" +
						(value.name.empty() ? "" : (" '" + value.name + "'"));
				}
				else if constexpr (std::is_same_v<Command, CreateLightCommand>)
				{
					return "Create " + value.type + " light" +
						(value.name.empty() ? "" : (" '" + value.name + "'"));
				}
				else if constexpr (std::is_same_v<Command, CreateCameraCommand>)
				{
					return std::string(value.makeMain ? "Create main camera" : "Create camera") +
						(value.name.empty() ? "" : (" '" + value.name + "'"));
				}
				else if constexpr (std::is_same_v<Command, CreateUIElementCommand>)
				{
					return "Create " + value.kind + " UI element" +
						(value.name.empty() ? "" : (" '" + value.name + "'")) +
						(value.parentName.empty() ? "" : (" on '" + value.parentName + "'"));
				}
				else if constexpr (std::is_same_v<Command, CreateImportedMeshCommand>)
				{
					return "Import model '" + value.sourcePath + "'" +
						(value.name.empty() ? "" : (" as '" + value.name + "'"));
				}
				else if constexpr (std::is_same_v<Command, CreateTerrainCommand>)
				{
					return std::string(value.shape == "mountain" ? "Create mountain terrain" : "Create terrain") +
						(value.name.empty() ? "" : (" '" + value.name + "'"));
				}
				else if constexpr (std::is_same_v<Command, SetPropertyCommand>)
				{
					return "Set " + value.component + "." + value.property + " on '" + value.entityName + "'";
				}
				else
				{
					return "Unknown command";
				}
			},
			command);
	}

	AIPlanResult planEditorCommands(
		const AIProviderClient& client,
		const std::string& providerId,
		const std::string& userPrompt,
		const std::vector<SceneEntity>& currentEntities)
	{
		AIProviderRequest request;
		request.systemPrompt =
			"You are the planning layer of the GameForgerAI game editor. Translate the user's natural-language "
			"request into a JSON array of editor commands. Reply with ONLY a JSON array: no markdown code "
			"fences, no prose before or after it. Each element is an object with an \"op\" field.\n\n"
			"IMPORTANT: this is a continuation of an existing, in-progress scene, not a fresh one - the user "
			"builds it up over many requests. " +
			describeCurrentScene(currentEntities) +
			" Default to ADDING new entities alongside what's already there, or MODIFYING/DELETING only the "
			"specific existing entities the user's request clearly names or refers to (by name, tag, or "
			"description). Never recreate, delete, or otherwise touch an existing entity the user didn't ask "
			"about, and never wipe or regenerate the whole scene unless explicitly asked to.\n\n"
			"Available ops (fields other than \"op\" and \"name\" are optional and default sensibly):\n"
			"- {\"op\":\"create_entity\",\"name\":string,\"primitive\":\"cube\"|\"sphere\"|\"cylinder\"|\"cone\"|"
			"\"plane\"|\"capsule\",\"position\":[x,y,z]}\n"
			"- {\"op\":\"create_terrain\",\"name\":string,\"position\":[x,y,z],\"shape\":\"flat\"|\"mountain\","
			"\"baseSize\":number (world units, the terrain's X-by-Z footprint, e.g. a \"40x40 base\" is "
			"baseSize 40),\"peakHeight\":number (world units the mountain rises above its base; ignored for "
			"shape \"flat\")}. Use this, NOT the \"cone\" (or any other) primitive, for ANY mountain, hill, "
			"peak, or terrain-with-elevation request - a cone is a rigid hard-edged shape, while "
			"create_terrain with shape \"mountain\" sculpts an actual heightmap landform with a rounded, "
			"natural peak sized exactly to the requested base and height. A flat piece of ground/terrain "
			"(no elevation) should use shape \"flat\".\n"
			"- {\"op\":\"delete_entity\",\"name\":string}\n"
			"- {\"op\":\"rename_entity\",\"name\":string,\"newName\":string}\n"
			"- {\"op\":\"duplicate_entity\",\"name\":string}\n"
			"- {\"op\":\"set_position\",\"name\":string,\"position\":[x,y,z]}\n"
			"- {\"op\":\"set_rotation\",\"name\":string,\"rotation\":[x,y,z] in degrees}\n"
			"- {\"op\":\"set_scale\",\"name\":string,\"scale\":[x,y,z]}\n"
			"- {\"op\":\"set_color\",\"name\":string,\"color\":[r,g,b] each 0..1}\n"
			"- {\"op\":\"add_tag\",\"name\":string,\"tag\":string} adds a tag (an entity can have any number, in "
			"priority order - the first one added is primary)\n"
			"- {\"op\":\"remove_tag\",\"name\":string,\"tag\":string}\n"
			"- {\"op\":\"set_pivot\",\"name\":string,\"pivotPreset\":\"center\"|\"left\"|\"right\"|\"top\"|"
			"\"bottom\"|\"top-left\"|\"top-right\"|\"bottom-left\"|\"bottom-right\"} (preferred), or "
			"{\"op\":\"set_pivot\",\"name\":string,\"pivot\":[x,y,z]} with each component in -1..1 for a manual "
			"offset. The pivot is what Move/Rotate/Scale and animation rotate around - e.g. a door needs its "
			"pivot set to \"left\" or \"right\" (its hinge edge) before rotating it open, otherwise it swings "
			"around its own center.\n"
			"- {\"op\":\"create_script\",\"path\":string (must start with \\\"Game/Scripts/\\\" and end in .lua),"
			"\"content\":string (Lua source)}. The Lua source MUST end with `return TableName` where TableName is "
			"a local table defining on_start(self) (runs once when Play starts) and/or "
			"on_update(self, delta_time) (runs every frame) as methods, plus optional on_end(self) "
			"(runs when Play stops - release whatever on_start took), e.g. `local X = {}; function X:on_start() "
			"end; function X:on_update(dt) end; return X`. Before on_start() runs, the engine attaches to "
			"`self`, and this is the COMPLETE list - do NOT invent globals such as Input, Entity, Vector, "
			"Raycast, UI, DeltaTime or love.*, there is no Update() entry point, and vectors are plain "
			"{x=,y=,z=} tables with NO arithmetic operators. "
			"self.entity (getPosition()/setPosition({x=,y=,z=}) taking ONE table not three numbers/"
			"getRotation()/setRotation({x=,y=,z=} degrees)/getScale()/getForward()/getRight() which is "
			"screen-right for strafing and must not be negated), "
			"self.input (isKeyDown(name)/isKeyPressed(name)/getAxis(posKey,negKey)/getMouseDeltaX()/"
			"getMouseDeltaY(), key names like \\\"W\\\",\\\"Space\\\",\\\"LeftShift\\\" - MOUSE BUTTONS "
			"DO NOT EXIST, there is no \\\"LeftMouse\\\"), "
			"self.camera (setMode(\\\"fps\\\"|\\\"third_person\\\")/getMode()), "
			"self.physics (resolve(position, halfWidth, height) -> correctedPosition, grounded - the only "
			"collision query, there is no raycast), "
			"self.world (findNearestWithTag(tag)/findPositionByTag(tag)/fireProjectile(from,to,speed,hitTag)/"
			"fireGravityProjectile(from,direction,speed,hitTag)/isHoldingItem()/isAimingCatapult()/"
			"setOperatingCatapult(bool)/setEntityRotation(name,{x=,y=,z=}) - use the fire* calls rather than "
			"simulating projectiles in Lua tables), "
			"self.gameManager (setCursorLock(bool)), "
			"self.managers (register(name)/unregister(name)/has(name)/list() - a controller MUST register in "
			"on_start and unregister in on_end or the mouse cursor will never lock), and "
			"self.audio (play(clipPath,volume,loop) under Game/Audio/stop() which stops ALL sounds/"
			"setMasterVolume(0..1)/isPlaying() which always returns false so never branch on it). "
			"A script cannot delete its own entity, draw UI, load a scene or read files. "
			"Write the \"content\" string's newlines as a single "
			"`\\n` escape - do not double-escape it as `\\\\n`.\n"
			"- {\"op\":\"attach_script\",\"name\":string,\"path\":string}\n\n"
			"If an entity referenced doesn't exist yet, create it earlier in the same array; commands run in "
			"array order. If the request can't be fulfilled with these ops, reply with an empty array: [].";
		request.prompt = userPrompt;

		const AIProviderResponse response = client.send(providerId, request);
		if (!response.success)
		{
			if (!response.error.empty())
			{
				return {false, response.error, {}, {}};
			}
			const std::string detail = extractErrorMessage(response.body);
			return {false, detail.empty() ? "AI provider request failed." : detail, {}, {}};
		}

		const std::optional<std::string> content = extractChatMessageContent(response.body);
		if (!content.has_value() || content->empty())
		{
			return {false, "The AI response did not contain any content.", {}, {}};
		}

		const std::string jsonText = stripCodeFences(*content);
		const std::optional<json::Value> parsed = json::parse(jsonText);
		if (!parsed.has_value() || parsed->type != json::Value::Type::Array)
		{
			return {false, "The AI did not return a valid command plan.", {}, {}};
		}

		AIPlanResult result;
		result.success = true;
		int skipped = 0;
		for (const json::Value& element : parsed->arrayValue)
		{
			if (element.type != json::Value::Type::Object)
			{
				++skipped;
				continue;
			}
			std::optional<AIEditorCommand> command = buildCommand(element);
			if (!command.has_value())
			{
				++skipped;
				continue;
			}
			const AICommandResult validation = AICommandValidator::validate(*command);
			if (!validation.success)
			{
				++skipped;
				continue;
			}
			result.commandDescriptions.push_back(describeCommand(*command));
			result.commands.push_back(std::move(*command));
		}

		if (result.commands.empty())
		{
			result.message = skipped > 0
				? "The AI's plan didn't contain any valid commands."
				: "The AI didn't propose any changes for this request.";
		}
		else
		{
			result.message = std::to_string(result.commands.size()) + " command(s) planned" +
				(skipped > 0 ? (" (" + std::to_string(skipped) + " skipped)") : "") + ".";
		}
		return result;
	}
}
