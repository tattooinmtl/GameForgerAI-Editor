#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "GameForger/Core/AudioEngine.hpp"
#include "GameForger/Editor/ProjectSettings.hpp"
#include "GameForger/Editor/ProjectSettingsBus.hpp"

namespace gameforger::editor
{
	using AudioPanelLogFn = std::function<void(bool success, const std::string& message)>;

	struct AudioPanelState
	{
		int selectedClip = -1;
		int newHookEvent = 0;
		int newHookClip = 0;
		float newHookVolume = 1.0F;
		bool dockPlacementDone = false;
	};

	void fireAudioHooks(
		core::AudioEngine& audio,
		const std::filesystem::path& projectRoot,
		const std::vector<AudioHook>& hooks,
		AudioHook::Event event);

	void drawAudioPanel(
		ProjectSettingsBus& bus,
		core::AudioEngine& audio,
		const std::filesystem::path& projectRoot,
		AudioPanelState& state,
		const AudioPanelLogFn& log);
}
