#pragma once

#include <filesystem>
#include <string>
#include <variant>

#include "GameForger/Editor/AICommandBus.hpp"
#include "GameForger/Editor/ProjectSettings.hpp"

namespace gameforger::editor
{
	// Project-level commands, deliberately NOT part of AIEditorCommand.
	// AICommandBus mutates the currently-open EditorScene; these outlive scene
	// loads and target ProjectSettings instead. Same validate -> execute ->
	// AICommandResult shape, so the AI Cockpit routes to this exactly the way
	// it already routes to the scene bus and to Blender.
	struct SetProjectSettingCommand
	{
		// One of: name, startupScene, skeletonProfile, aiProviders,
		// scriptDirectory, mouseSensitivity, targetFps.
		std::string key;
		std::string stringValue;
		double numberValue = 0.0;
	};

	struct AddBootStepCommand
	{
		BootStep step;
		// -1 appends. Otherwise inserts before this index.
		int index = -1;
	};

	struct RemoveBootStepCommand
	{
		int index = 0;
	};

	struct MoveBootStepCommand
	{
		int fromIndex = 0;
		int toIndex = 0;
	};

	using ProjectSettingsCommand = std::variant<
		SetProjectSettingCommand,
		AddBootStepCommand,
		RemoveBootStepCommand,
		MoveBootStepCommand>;

	// Owns the in-memory ProjectSettings and is the only writer to it. Every
	// mutation - from the inspector panel and from the AI alike - goes through
	// execute(), so validation and undo cannot be bypassed by one caller.
	class ProjectSettingsBus final
	{
	public:
		explicit ProjectSettingsBus(std::filesystem::path projectRoot);

		[[nodiscard]] const ProjectSettings& settings() const noexcept { return settings_; }

		// Reloads from disk, discarding unsaved edits.
		ProjectSettingsIoResult reload();

		// Checks a command without applying it. Rejects the things an LLM gets
		// wrong: an unknown key, a startupScene that does not exist on disk, a
		// non-finite number, an out-of-range fps, an out-of-range step index.
		[[nodiscard]] AICommandResult validate(const ProjectSettingsCommand& command) const;

		// Validates, applies, and writes both files. A failed write is reported
		// AND rolled back in memory, so what is on screen always matches disk.
		AICommandResult execute(const ProjectSettingsCommand& command);

		// True once execute() has succeeded at least once - lets the panel show
		// whether anything has been changed this session.
		[[nodiscard]] bool dirtyOnce() const noexcept { return mutatedOnce_; }

	private:
		std::filesystem::path projectRoot_;
		ProjectSettings settings_;
		bool mutatedOnce_ = false;
	};
}
