#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "GameForger/Editor/ProjectSettings.hpp"
#include "GameForger/Editor/ProjectSettingsBus.hpp"

namespace gameforger::editor
{
	// The panel logs through this rather than touching ConsoleState directly -
	// ConsoleState and logMessage live in main.cpp's anonymous namespace and
	// are not reachable from another translation unit. main.cpp binds this to
	// logMessage when it calls the panel.
	using ProjectSettingsLogFn = std::function<void(bool success, const std::string& message)>;

	struct ProjectSettingsPanelState
	{
		// Whether the panel is shown. Every panel is closable and reopenable
		// from the Panels menu; before this they were drawn unconditionally,
		// so a panel could be neither hidden nor recovered.
		bool open = true;


		// ImGui InputText needs writable char storage. These mirror the bus's
		// settings and are re-synced from it whenever the widget is not being
		// actively edited, so an AI-driven change shows up live in the panel
		// without stomping on what the user is mid-way through typing.
		std::array<char, 128> name{};
		std::array<char, 256> skeletonProfile{};
		std::array<char, 256> aiProviders{};
		std::array<char, 256> scriptDirectory{};
		float mouseSensitivity = 0.15F;
		int targetFps = 60;

		// Scenes found under Game/Scenes, for the startupScene combo. Rebuilt
		// on demand rather than every frame - directory_iterator per frame on
		// every panel draw is wasteful for a list that changes rarely.
		std::vector<std::string> sceneChoices;
		bool sceneChoicesLoaded = false;

		// "Add boot step" staging row.
		int newStepKind = 0;
		float newStepSeconds = 1.0F;
		std::array<char, 128> newStepTargetEntity{};
		std::array<char, 128> newStepShotName{};
		std::array<char, 256> newStepClipPath{};

		// Set by the Project browser when Project.json/Settings.json is
		// clicked, so the panel can pull itself to the front.
		bool focusRequested = false;

		// Retry until this panel is a tab between Console and Storyboard
		// (same node, no splits). Stops immediately if the user has already
		// docked it somewhere else, or if Console and Storyboard no longer
		// share a node.
		bool dockPlacementDone = false;
	};

	// Draws the "Project Settings" window. Every mutation is routed through
	// `bus`, so the panel and the AI share one validated path and neither can
	// write a value the other would have rejected.
	void drawProjectSettingsPanel(
		ProjectSettingsBus& bus,
		const std::filesystem::path& projectRoot,
		ProjectSettingsPanelState& state,
		const ProjectSettingsLogFn& log);
}
