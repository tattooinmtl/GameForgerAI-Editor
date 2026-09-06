#include "GameForger/Editor/ProjectSettingsBus.hpp"

#include <algorithm>
#include <cmath>
#include <system_error>
#include <type_traits>
#include <utility>

namespace gameforger::editor
{
	namespace
	{
		AICommandResult invalid(std::string message)
		{
			return AICommandResult{false, false, std::move(message)};
		}

		AICommandResult ok(std::string message)
		{
			return AICommandResult{true, false, std::move(message)};
		}

		// Keys whose value is a string, and keys whose value is a number.
		// Split so a "set targetFps to 'fast'" style call is rejected with a
		// useful message rather than silently storing 0.
		bool isStringKey(const std::string& key)
		{
			return key == "name" || key == "startupScene" || key == "skeletonProfile" ||
				key == "aiProviders" || key == "scriptDirectory";
		}

		bool isNumberKey(const std::string& key)
		{
			return key == "mouseSensitivity" || key == "targetFps";
		}

		// targetFps below ~15 makes the editor feel broken; above 480 is past
		// any real display and usually a typo (or an LLM inventing a number).
		constexpr int kMinTargetFps = 15;
		constexpr int kMaxTargetFps = 480;
		constexpr float kMinMouseSensitivity = 0.001F;
		constexpr float kMaxMouseSensitivity = 10.0F;

		bool bootStepIsValid(const BootStep& step, std::string& outError)
		{
			switch (step.kind)
			{
				case BootStep::Kind::WaitSeconds:
					if (!std::isfinite(step.seconds) || step.seconds < 0.0F || step.seconds > 600.0F)
					{
						outError = "wait_seconds needs a duration between 0 and 600 seconds.";
						return false;
					}
					return true;
				case BootStep::Kind::PlayAnimation:
					if (step.targetEntity.empty())
					{
						outError = "play_animation needs a targetEntity.";
						return false;
					}
					return true;
				case BootStep::Kind::PlayCutscene:
					if (step.shotName.empty())
					{
						outError = "play_cutscene needs a shotName.";
						return false;
					}
					return true;
				case BootStep::Kind::PlayAudio:
					if (step.clipPath.empty())
					{
						outError = "play_audio needs a clipPath.";
						return false;
					}
					return true;
				case BootStep::Kind::LockPlayerInput:
				case BootStep::Kind::UnlockPlayerInput:
					return true;
			}
			outError = "Unknown boot step kind.";
			return false;
		}
	}

	ProjectSettingsBus::ProjectSettingsBus(std::filesystem::path projectRoot)
		: projectRoot_(std::move(projectRoot))
	{
		(void)loadProjectSettings(projectRoot_, settings_);
	}

	ProjectSettingsIoResult ProjectSettingsBus::reload()
	{
		return loadProjectSettings(projectRoot_, settings_);
	}

	AICommandResult ProjectSettingsBus::validate(const ProjectSettingsCommand& command) const
	{
		const int stepCount = static_cast<int>(settings_.bootSequence.size());

		return std::visit(
			[&](const auto& value) -> AICommandResult
			{
				using Command = std::decay_t<decltype(value)>;

				if constexpr (std::is_same_v<Command, SetProjectSettingCommand>)
				{
					if (value.key.empty())
					{
						return invalid("Setting key cannot be empty.");
					}
					if (isStringKey(value.key))
					{
						if (value.key == "startupScene")
						{
							if (value.stringValue.empty())
							{
								return invalid("startupScene cannot be empty.");
							}
							// A startup scene that does not exist means the
							// project simply fails to launch later, with no
							// clue why. Catch it at the point of the edit.
							std::error_code ec;
							if (!std::filesystem::exists(projectRoot_ / value.stringValue, ec))
							{
								return invalid("Scene '" + value.stringValue + "' does not exist under the project root.");
							}
						}
						return ok("Set " + value.key + ".");
					}
					if (isNumberKey(value.key))
					{
						if (!std::isfinite(value.numberValue))
						{
							return invalid(value.key + " must be a finite number.");
						}
						if (value.key == "targetFps")
						{
							const int fps = static_cast<int>(value.numberValue);
							if (fps < kMinTargetFps || fps > kMaxTargetFps)
							{
								return invalid("targetFps must be between 15 and 480.");
							}
						}
						else
						{
							const float sensitivity = static_cast<float>(value.numberValue);
							if (sensitivity < kMinMouseSensitivity || sensitivity > kMaxMouseSensitivity)
							{
								return invalid("mouseSensitivity must be between 0.001 and 10.");
							}
						}
						return ok("Set " + value.key + ".");
					}
					return invalid("Unknown setting '" + value.key +
						"'. Known keys: name, startupScene, skeletonProfile, aiProviders, "
						"scriptDirectory, mouseSensitivity, targetFps.");
				}
				else if constexpr (std::is_same_v<Command, AddBootStepCommand>)
				{
					std::string error;
					if (!bootStepIsValid(value.step, error))
					{
						return invalid(std::move(error));
					}
					if (value.index < -1 || value.index > stepCount)
					{
						return invalid("Boot step index out of range.");
					}
					return ok("Add boot step.");
				}
				else if constexpr (std::is_same_v<Command, RemoveBootStepCommand>)
				{
					if (value.index < 0 || value.index >= stepCount)
					{
						return invalid("Boot step index out of range.");
					}
					return ok("Remove boot step.");
				}
				else
				{
					static_assert(std::is_same_v<Command, MoveBootStepCommand>);
					if (value.fromIndex < 0 || value.fromIndex >= stepCount ||
						value.toIndex < 0 || value.toIndex >= stepCount)
					{
						return invalid("Boot step index out of range.");
					}
					return ok("Move boot step.");
				}
			},
			command);
	}

	AICommandResult ProjectSettingsBus::execute(const ProjectSettingsCommand& command)
	{
		const AICommandResult validation = validate(command);
		if (!validation.success)
		{
			return validation;
		}

		// Snapshot first: if the disk write fails, in-memory state is restored
		// so the panel never shows a value that was not actually persisted.
		const ProjectSettings previous = settings_;

		std::visit(
			[this](const auto& value)
			{
				using Command = std::decay_t<decltype(value)>;

				if constexpr (std::is_same_v<Command, SetProjectSettingCommand>)
				{
					if (value.key == "name")                 settings_.name = value.stringValue;
					else if (value.key == "startupScene")     settings_.startupScene = value.stringValue;
					else if (value.key == "skeletonProfile")  settings_.skeletonProfile = value.stringValue;
					else if (value.key == "aiProviders")      settings_.aiProvidersPath = value.stringValue;
					else if (value.key == "scriptDirectory")  settings_.scriptDirectory = value.stringValue;
					else if (value.key == "mouseSensitivity") settings_.mouseSensitivity = static_cast<float>(value.numberValue);
					else if (value.key == "targetFps")        settings_.targetFps = static_cast<int>(value.numberValue);
				}
				else if constexpr (std::is_same_v<Command, AddBootStepCommand>)
				{
					const std::size_t at = value.index < 0
						? settings_.bootSequence.size()
						: static_cast<std::size_t>(value.index);
					settings_.bootSequence.insert(
						settings_.bootSequence.begin() + static_cast<std::ptrdiff_t>(at), value.step);
				}
				else if constexpr (std::is_same_v<Command, RemoveBootStepCommand>)
				{
					settings_.bootSequence.erase(
						settings_.bootSequence.begin() + static_cast<std::ptrdiff_t>(value.index));
				}
				else
				{
					static_assert(std::is_same_v<Command, MoveBootStepCommand>);
					BootStep moved = settings_.bootSequence[static_cast<std::size_t>(value.fromIndex)];
					settings_.bootSequence.erase(
						settings_.bootSequence.begin() + static_cast<std::ptrdiff_t>(value.fromIndex));
					settings_.bootSequence.insert(
						settings_.bootSequence.begin() + static_cast<std::ptrdiff_t>(value.toIndex), std::move(moved));
				}
			},
			command);

		const ProjectSettingsIoResult saved = saveProjectSettings(projectRoot_, settings_);
		if (!saved.success)
		{
			settings_ = previous;
			return {false, false, "Change was not saved: " + saved.message};
		}

		mutatedOnce_ = true;
		return validation;
	}
}
