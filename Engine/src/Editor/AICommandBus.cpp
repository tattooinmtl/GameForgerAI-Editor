#include "GameForger/Editor/AICommandBus.hpp"

#include <cmath>
#include <type_traits>
#include <utility>

namespace gameforger::editor
{
	namespace
	{
		bool hasText(const std::string& value)
		{
			return !value.empty();
		}

		AICommandResult invalid(const char* message)
		{
			return AICommandResult{false, false, message};
		}
	}

	AICommandResult AICommandValidator::validate(const AIEditorCommand& command)
	{
		return std::visit(
			[](const auto& value) -> AICommandResult
			{
				using Command = std::decay_t<decltype(value)>;
				if constexpr (std::is_same_v<Command, CreateEntityCommand>)
				{
					return hasText(value.name)
						? AICommandResult{true, false, "Create entity command is valid."}
						: invalid("Entity name cannot be empty.");
				}
				else if constexpr (std::is_same_v<Command, DeleteEntityCommand>)
				{
					return hasText(value.entityName)
						? AICommandResult{true, false, "Delete entity command is valid."}
						: invalid("Entity name cannot be empty.");
				}
				else if constexpr (std::is_same_v<Command, RenameEntityCommand>)
				{
					return hasText(value.entityName) && hasText(value.newName)
						? AICommandResult{true, false, "Rename entity command is valid."}
						: invalid("Entity name and new name cannot be empty.");
				}
				else if constexpr (std::is_same_v<Command, DuplicateEntityCommand>)
				{
					return hasText(value.entityName)
						? AICommandResult{true, false, "Duplicate entity command is valid."}
						: invalid("Entity name cannot be empty.");
				}
				else if constexpr (std::is_same_v<Command, SetPositionCommand>)
				{
					if (!hasText(value.entityName))
					{
						return invalid("Entity name cannot be empty.");
					}
					if (!std::isfinite(value.position.x) ||
						!std::isfinite(value.position.y) ||
						!std::isfinite(value.position.z))
					{
						return invalid("Position must contain finite values.");
					}
					return {true, false, "Set position command is valid."};
				}
				else if constexpr (std::is_same_v<Command, RequestAnimationCommand>)
				{
					return hasText(value.characterName) && hasText(value.action)
						? AICommandResult{true, false, "Animation command is valid."}
						: invalid("Character and animation names cannot be empty.");
				}
				else if constexpr (std::is_same_v<Command, SetAnimationCommand>)
				{
					return hasText(value.entityName)
						? AICommandResult{true, false, "Set animation command is valid."}
						: invalid("Entity name cannot be empty.");
				}
				else if constexpr (std::is_same_v<Command, CreateScriptCommand>)
				{
					if (!hasText(value.path) || !hasText(value.content))
					{
						return invalid("Script path and content cannot be empty.");
					}
					if (value.language != "lua")
					{
						return invalid("Only Lua scripts are currently supported.");
					}
					if (value.path.find("..") != std::string::npos)
					{
						return invalid("Script path cannot escape the project directory.");
					}
					return {true, false, "Create script command is valid."};
				}
				else if constexpr (std::is_same_v<Command, AttachScriptCommand>)
				{
					if (!hasText(value.entityName) || !hasText(value.scriptPath))
					{
						return invalid("Entity name and script path cannot be empty.");
					}
					// Same traversal guard CreateScriptCommand applies - the
					// executor will refuse anyway via resolveProjectFile,
					// but the preview path bypasses the executor and would
					// otherwise disagree with execute.
					if (value.scriptPath.find("..") != std::string::npos)
					{
						return invalid("Script path cannot escape the project directory.");
					}
					return {true, false, "Attach script command is valid."};
				}
				else if constexpr (std::is_same_v<Command, DetachScriptCommand>)
				{
					return hasText(value.entityName) && hasText(value.scriptPath)
						? AICommandResult{true, false, "Detach script command is valid."}
						: invalid("Entity name and script path cannot be empty.");
				}
				else if constexpr (std::is_same_v<Command, AddTagCommand>)
				{
					return hasText(value.entityName) && hasText(value.tag)
						? AICommandResult{true, false, "Add tag command is valid."}
						: invalid("Entity name and tag cannot be empty.");
				}
				else if constexpr (std::is_same_v<Command, RemoveTagCommand>)
				{
					return hasText(value.entityName) && hasText(value.tag)
						? AICommandResult{true, false, "Remove tag command is valid."}
						: invalid("Entity name and tag cannot be empty.");
				}
				else if constexpr (std::is_same_v<Command, SetPropertyCommand>)
				{
					if (!hasText(value.entityName) ||
						!hasText(value.component) ||
						!hasText(value.property))
					{
						return invalid("Entity, component and property are required.");
					}
					// Finiteness + non-negative-scale checks for the vec3
					// properties that are easy to corrupt with NaN / Inf
					// (SetPositionCommand already gets this; SetPropertyCommand
					// for the same fields was previously unguarded, so a
					// single bad AI call could push NaNs into the model matrix
					// for every entity in the scene).
					if (value.component == "Transform" || value.component == "Parent")
					{
						if (const auto* vec = std::get_if<glm::vec3>(&value.value))
						{
							if (!std::isfinite(vec->x) || !std::isfinite(vec->y) || !std::isfinite(vec->z))
							{
								return invalid(
									(value.component + "." + value.property + " must contain finite values.").c_str());
							}
							if ((value.property == "scale" || value.property == "localScale") &&
								(vec->x <= 0.0F || vec->y <= 0.0F || vec->z <= 0.0F))
							{
								return invalid(
									(value.component + "." + value.property + " components must be positive.").c_str());
							}
						}
					}
					return {true, false, "Set property command is valid."};
				}
				else if constexpr (std::is_same_v<Command, CreateTerrainCommand>)
				{
					if (value.resolution < 2 || value.worldSize <= 0.0F)
					{
						return invalid("Terrain resolution must be at least 2 and world size must be positive.");
					}
					return {true, false, "Create terrain command is valid."};
				}
				else if constexpr (std::is_same_v<Command, CreateImportedMeshCommand>)
				{
					return hasText(value.sourcePath)
						? AICommandResult{true, false, "Create imported mesh command is valid."}
						: invalid("Model source path cannot be empty.");
				}
				else if constexpr (std::is_same_v<Command, CreateTextMeshCommand>)
				{
					if (!hasText(value.content) || !hasText(value.fontPath))
					{
						return invalid("Text content and font path cannot be empty.");
					}
					if (value.fontSize <= 0.0F || value.depth <= 0.0F)
					{
						return invalid("Font size and depth must be positive.");
					}
					return {true, false, "Create text mesh command is valid."};
				}
				else
				{
					// Default branch: previously returned "Command is valid."
					// for any command the validator didn't have an explicit
					// case for. If a new AIEditorCommand variant is added
					// without updating this switch, the catch-all silently
					// approved it; the executor then returned "not
					// implemented" without telling the AI that the command
					// type itself was unhandled. Now: every unhandled
					// variant fails the validator with an explicit
					// "Unknown command type" message so the AI / script
					// author can see the gap immediately.
					return invalid("Unknown command type - no validator case handles this variant.");
				}
			},
			command);
	}

	void AICommandBus::setHandler(Handler handler)
	{
		handler_ = std::move(handler);
	}

	AICommandResult AICommandBus::preview(const AIEditorCommand& command) const
	{
		AICommandResult result = AICommandValidator::validate(command);
		result.preview = true;
		if (result.success)
		{
			result.message = "Preview: " + result.message;
		}
		return result;
	}

	AICommandResult AICommandBus::execute(const AIEditorCommand& command) const
	{
		const AICommandResult validation = AICommandValidator::validate(command);
		if (!validation.success)
		{
			return validation;
		}
		if (!handler_)
		{
			return {false, false, "No editor command handler is connected."};
		}
		return handler_(command);
	}
}