#include "GameForger/Editor/AudioPanel.hpp"

#include <algorithm>
#include <iterator>
#include <system_error>

#include <imgui.h>

#include "GameForger/Core/ProjectPaths.hpp"
#include "GameForger/Editor/EditorLayout.hpp"

namespace gameforger::editor
{
	namespace
	{
		constexpr AudioHook::Event kHookEvents[] = {
			AudioHook::Event::OnPlayStart,
			AudioHook::Event::OnPickup,
			AudioHook::Event::OnProjectileFire,
			AudioHook::Event::OnProjectileHit,
			AudioHook::Event::OnGameOver,
			AudioHook::Event::OnBootStep,
		};

		std::vector<std::string> listAudioClips(const std::filesystem::path& projectRoot)
		{
			std::vector<std::string> clips;
			const std::filesystem::path audioRoot = projectRoot / "Game" / "Audio";
			std::error_code ec;
			if (!std::filesystem::exists(audioRoot, ec))
			{
				return clips;
			}
			for (const std::filesystem::directory_entry& entry :
				std::filesystem::directory_iterator(audioRoot, ec))
			{
				if (!entry.is_regular_file(ec))
				{
					continue;
				}
				std::error_code relativeError;
				const std::filesystem::path relative =
					std::filesystem::relative(entry.path(), projectRoot, relativeError);
				if (relativeError)
				{
					continue;
				}
				const std::string generic = relative.generic_string();
				if (core::resolveProjectFile(projectRoot, generic, "Game/Audio", core::audioClipExtensions()))
				{
					clips.push_back(generic);
				}
			}
			std::sort(clips.begin(), clips.end());
			return clips;
		}

		void submit(
			ProjectSettingsBus& bus, const ProjectSettingsCommand& command, const AudioPanelLogFn& log)
		{
			const AICommandResult result = bus.execute(command);
			if (log)
			{
				log(result.success, result.message);
			}
		}
	}

	void fireAudioHooks(
		core::AudioEngine& audio,
		const std::filesystem::path& projectRoot,
		const std::vector<AudioHook>& hooks,
		const AudioHook::Event event)
	{
		for (const AudioHook& hook : hooks)
		{
			if (hook.event == event && !hook.clipPath.empty())
			{
				audio.play(projectRoot, hook.clipPath, hook.volume);
			}
		}
	}

	void drawAudioPanel(
		ProjectSettingsBus& bus,
		core::AudioEngine& audio,
		const std::filesystem::path& projectRoot,
		AudioPanelState& state,
		const AudioPanelLogFn& log)
	{
		ImGui::Begin("Audio");

		if (audio.isSilent())
		{
			ImGui::TextDisabled("No audio device — playback is silent. Clips still validate.");
		}

		float master = audio.masterVolume();
		if (ImGui::SliderFloat("Master Volume", &master, 0.0F, 1.0F, "%.2f"))
		{
			audio.setMasterVolume(master);
		}

		const std::vector<std::string> clips = listAudioClips(projectRoot);
		ImGui::Separator();
		ImGui::TextUnformatted("Clips (Game/Audio)");
		if (clips.empty())
		{
			ImGui::TextDisabled("Drop .wav / .mp3 / .flac files into Game/Audio.");
		}
		else
		{
			if (ImGui::BeginListBox("##Clips", ImVec2(-1.0F, 120.0F)))
			{
				for (int i = 0; i < static_cast<int>(clips.size()); ++i)
				{
					const bool selected = state.selectedClip == i;
					if (ImGui::Selectable(clips[static_cast<std::size_t>(i)].c_str(), selected))
					{
						state.selectedClip = i;
					}
				}
				ImGui::EndListBox();
			}
			if (state.selectedClip >= 0 && state.selectedClip < static_cast<int>(clips.size()))
			{
				const std::string& clip = clips[static_cast<std::size_t>(state.selectedClip)];
				if (ImGui::Button("Play"))
				{
					if (!audio.playPreview(projectRoot, clip) && log)
					{
						log(false, "Could not play " + clip);
					}
				}
				ImGui::SameLine();
				if (ImGui::Button("Stop"))
				{
					audio.stopPreview();
				}
			}
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Event hooks");
		ImGui::TextDisabled("Fired from Play start, pickup, projectiles, game over, boot audio.");

		const std::vector<AudioHook>& hooks = bus.settings().audioHooks;
		int removeIndex = -1;
		for (int i = 0; i < static_cast<int>(hooks.size()); ++i)
		{
			ImGui::PushID(i);
			ImGui::Text("%s  %s  (%.2f)",
				audioHookEventName(hooks[static_cast<std::size_t>(i)].event),
				hooks[static_cast<std::size_t>(i)].clipPath.c_str(),
				static_cast<double>(hooks[static_cast<std::size_t>(i)].volume));
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove"))
			{
				removeIndex = i;
			}
			ImGui::PopID();
		}
		if (removeIndex >= 0)
		{
			submit(bus, RemoveAudioHookCommand{removeIndex}, log);
		}

		if (ImGui::BeginCombo("Event", audioHookEventName(kHookEvents[state.newHookEvent])))
		{
			for (int i = 0; i < static_cast<int>(std::size(kHookEvents)); ++i)
			{
				const bool selected = state.newHookEvent == i;
				if (ImGui::Selectable(audioHookEventName(kHookEvents[i]), selected))
				{
					state.newHookEvent = i;
				}
			}
			ImGui::EndCombo();
		}

		const char* clipPreview = clips.empty()
			? "(no clips)"
			: clips[static_cast<std::size_t>(
				  std::clamp(state.newHookClip, 0, std::max(0, static_cast<int>(clips.size()) - 1)))]
				  .c_str();
		if (ImGui::BeginCombo("Clip", clipPreview))
		{
			for (int i = 0; i < static_cast<int>(clips.size()); ++i)
			{
				const bool selected = state.newHookClip == i;
				if (ImGui::Selectable(clips[static_cast<std::size_t>(i)].c_str(), selected))
				{
					state.newHookClip = i;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SliderFloat("Volume", &state.newHookVolume, 0.0F, 1.0F, "%.2f");
		if (ImGui::Button("Add hook") && !clips.empty())
		{
			AddAudioHookCommand command;
			command.hook.event = kHookEvents[state.newHookEvent];
			command.hook.clipPath = clips[static_cast<std::size_t>(
				std::clamp(state.newHookClip, 0, static_cast<int>(clips.size()) - 1))];
			command.hook.volume = state.newHookVolume;
			submit(bus, command, log);
		}

		ImGui::End();

		if (!state.dockPlacementDone)
		{
			const ImGuiID consoleId = dockIdOfWindow("Console");
			const ImGuiID storyboardId = dockIdOfWindow("Storyboard");
			const ImGuiID audioId = dockIdOfWindow("Audio");
			if (consoleId == 0 || storyboardId == 0)
			{
			}
			else if (consoleId != storyboardId)
			{
				state.dockPlacementDone = true;
			}
			else if (audioId != 0 && audioId != consoleId)
			{
				state.dockPlacementDone = true;
			}
			else if (dockIdOfWindow("Project Settings") == consoleId
				? dockWindowBetween("Audio", "Project Settings", "Storyboard")
				: dockWindowBetween("Audio", "Console", "Storyboard"))
			{
				state.dockPlacementDone = true;
			}
		}
	}
}
