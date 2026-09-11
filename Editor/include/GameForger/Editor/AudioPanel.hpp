#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "GameForger/Core/AudioEngine.hpp"
#include "GameForger/Editor/AICommandBus.hpp"
#include "GameForger/Editor/AudioSourceEffects.hpp"
#include "GameForger/Editor/EditorScene.hpp"
#include "GameForger/Editor/ProjectSettings.hpp"
#include "GameForger/Editor/ProjectSettingsBus.hpp"

namespace gameforger::editor
{
	using AudioPanelLogFn = std::function<void(bool success, const std::string& message)>;

	// Opens a native file picker and copies the chosen sound into
	// Game/Audio, returning its project-relative path (or nullopt if the
	// user cancelled or the copy failed).
	//
	// A callback rather than a direct call because the Win32 file dialog and
	// the import helper both live in main.cpp's anonymous namespace, which
	// this translation unit cannot reach - the same reason logging is a
	// callback here.
	using AudioImportFn = std::function<std::optional<std::string>()>;

	struct AudioPanelState
	{
		// Whether the panel is shown. Every panel is closable and reopenable
		// from the Panels menu; before this they were drawn unconditionally,
		// so a panel could be neither hidden nor recovered.
		bool open = true;


		int selectedClip = -1;
		int newHookEvent = 0;
		int newHookClip = 0;
		float newHookVolume = 1.0F;
		// Tick this for background music: an on_play_start hook that loops.
		bool newHookLoop = false;
		bool dockPlacementDone = false;

		// Which entity's Effects section is expanded, by name. Keyed by name
		// rather than index because the scene list reorders as entities are
		// added and removed, and an index would silently point at a different
		// object after that.
		std::string expandedObject;
	};

	void drawAudioPanel(
		ProjectSettingsBus& bus,
		core::AudioEngine& audio,
		// The scene supplies the object list: an entity appears below because
		// its Inspector "Has Audio Source" box is ticked, with no second
		// registration step to forget. Mutations go through the command bus,
		// so the panel and the Inspector share one validated, undoable path.
		EditorScene& scene,
		AICommandBus& commandBus,
		const std::filesystem::path& projectRoot,
		AudioPanelState& state,
		const AudioPanelLogFn& log,
		const AudioImportFn& importSound);
}
