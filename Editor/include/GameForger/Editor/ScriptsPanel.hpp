#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "GameForger/Editor/AICommandBus.hpp"
#include "GameForger/Editor/AIProviderClient.hpp"
#include "GameForger/Editor/EditorScene.hpp"

namespace gameforger::editor
{
	using ScriptsLogFn = std::function<void(bool success, const std::string& message)>;

	// How many built-in script presets the panel offers. Declared here only so
	// the tick state can be a fixed array; the table itself lives in
	// ScriptsPanel.cpp behind a static_assert, so adding a preset there without
	// bumping this is a build error rather than a silently ignored last entry.
	inline constexpr std::size_t kScriptPresetCount = 14;

	// The Scripts panel: browse, read, edit, attach, and ask the AI to write or
	// change a script - in one dockable window.
	//
	// This used to be fused into the Inspector: the file list, the attach
	// controls and the AI prompt were all buried inside the selected object's
	// property sheet, and the editor itself was a modal popup that blocked the
	// rest of the app while open. That made scripting something you did to one
	// object rather than a place you worked, and the AI could only ever create
	// a script from nothing - never change one that already existed
	// (MissingFunctions 4.7).
	struct ScriptsPanelState
	{
		bool open = true;

		// Files under Game/Scripts, rebuilt on demand rather than every frame.
		std::vector<std::string> scriptPaths;
		bool listLoaded = false;
		int selectedIndex = -1;

		// Open file. `buffer` is the live editing text; `loadedPath` is what it
		// came from, so switching files cannot silently save into the wrong one.
		std::string loadedPath;
		std::array<char, 32768> buffer{};
		bool bufferDirty = false;

		// A project-relative script path something outside the panel wants
		// opened - the Project browser double-clicking a .lua, or the
		// Inspector clicking an attached script. Consumed on the next draw.
		// The caller sets `open` too; this only says WHICH file.
		std::string requestOpenPath;

		// Which of the built-in presets are ticked. These moved here from the
		// Inspector's "Add Script" modal along with everything else scripting:
		// they are the fastest way to give an object working behaviour, and
		// leaving them behind in a popup while the panel owned every other
		// script path would have split the one workflow across two places.
		std::array<bool, kScriptPresetCount> presetSelected{};
		bool presetsExpanded = false;

		// The selection the panel last drew against. A status line naming an
		// object you are no longer pointing at is a lie the user reasonably
		// reads as "it is still attached to that first object", so the status
		// is cleared whenever this changes.
		int lastSelectedEntityId = -2;

		std::string status;
		bool statusSuccess = true;

		// The AI prompt at the bottom of the panel. `createMode` chooses
		// between writing a new script and rewriting the open one.
		std::array<char, 2048> aiPrompt{};
		std::array<char, 128> newScriptName{};
		bool createMode = false;

		// Worker state. The provider call blocks for seconds, so it runs off
		// the UI thread and hands the result back through the mutex - the same
		// shape the Inspector's generator used.
		std::thread worker;
		std::atomic<bool> busy{false};
		std::mutex resultMutex;
		bool hasResult = false;
		bool resultSuccess = false;
		std::string resultContent;
		std::string resultTargetPath;
	};

	// Draws the "Scripts" window. Safe to call every frame; does nothing while
	// `open` is false.
	void drawScriptsPanel(
		ScriptsPanelState& state,
		EditorScene& scene,
		AICommandBus& commandBus,
		const AIProviderClient& providerClient,
		const std::string& activeProviderId,
		const std::string& gameTitle,
		const std::filesystem::path& projectRoot,
		int selectedEntityId,
		const ScriptsLogFn& log);

	// Joins the worker. Called once on shutdown.
	void shutdownScriptsPanel(ScriptsPanelState& state) noexcept;
}
