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

		void submit(
			ProjectSettingsBus& bus, const ProjectSettingsCommand& command, const AudioPanelLogFn& log)
		{
			const AICommandResult result = bus.execute(command);
			if (log)
			{
				log(result.success, result.message);
			}
		}

		// Editor scene types -> the engine's own copy. Core deliberately does
		// not know about editor::AudioSourceData, so the conversion lives here.
		// One property edit, routed through the command bus so it is validated,
		// clamped and undoable exactly like an Inspector edit.
		void submitEntity(
			AICommandBus& commandBus,
			const std::string& entityName,
			const char* property,
			const EditableValue& value,
			const AudioPanelLogFn& log)
		{
			const AICommandResult result =
				commandBus.execute(SetPropertyCommand{entityName, "AudioSource", property, value});
			if (!result.success && log)
			{
				log(false, result.message);
			}
		}

		// DragFloat rather than SliderFloat throughout: dragging sweeps the
		// value, and ctrl-click or double-click types an exact one. That is
		// both halves of "sliders and manual entry" in a single widget.
		void effectSlider(
			AICommandBus& commandBus,
			const std::string& entityName,
			const char* label,
			const char* property,
			float value,
			const float low,
			const float high,
			const char* format,
			const AudioPanelLogFn& log)
		{
			ImGui::SetNextItemWidth(180.0F);
			if (ImGui::DragFloat(label, &value, (high - low) / 200.0F, low, high, format))
			{
				submitEntity(commandBus, entityName, property, value, log);
			}
		}

		void drawEffectControls(
			AICommandBus& commandBus, const SceneEntity& entity, const AudioPanelLogFn& log)
		{
			const AudioSourceData& source = entity.audioSource;
			const AudioEffects& fx = source.effects;
			const std::string& name = entity.name;

			// --- fades. Not effects: they ramp the voice's own volume and
			// build no DSP node, but they belong with the other per-object
			// audio tuning rather than buried in the Inspector.
			ImGui::TextDisabled("Fades");
			effectSlider(commandBus, name, "Fade in (s)", "fadeInSeconds",
				source.fadeInSeconds, 0.0F, 30.0F, "%.2f", log);
			ImGui::SameLine();
			effectSlider(commandBus, name, "Fade out (s)", "fadeOutSeconds",
				source.fadeOutSeconds, 0.0F, 30.0F, "%.2f", log);
			ImGui::TextDisabled("0 = start at full volume / stop instantly.");

			// --- reverb
			ImGui::TextDisabled("Reverb");
			bool reverb = fx.reverb;
			if (ImGui::Checkbox("Enable##reverb", &reverb))
			{
				submitEntity(commandBus, name, "fxReverb", reverb, log);
			}
			if (reverb)
			{
				effectSlider(commandBus, name, "Room size", "fxReverbRoomSize", fx.reverbRoomSize, 0.0F, 1.0F, "%.2f", log);
				effectSlider(commandBus, name, "Damping", "fxReverbDamping", fx.reverbDamping, 0.0F, 1.0F, "%.2f", log);
				effectSlider(commandBus, name, "Wet", "fxReverbWet", fx.reverbWet, 0.0F, 1.0F, "%.2f", log);
				ImGui::SameLine();
				effectSlider(commandBus, name, "Dry", "fxReverbDry", fx.reverbDry, 0.0F, 1.0F, "%.2f", log);
			}

			// --- delay / echo. One node does both: feedback is what turns a
			// single delayed copy into a repeating echo.
			ImGui::TextDisabled("Delay / Echo");
			bool delay = fx.delay;
			if (ImGui::Checkbox("Enable##delay", &delay))
			{
				submitEntity(commandBus, name, "fxDelay", delay, log);
			}
			if (delay)
			{
				effectSlider(commandBus, name, "Time (s)", "fxDelaySeconds", fx.delaySeconds, 0.01F, 2.0F, "%.3f", log);
				effectSlider(commandBus, name, "Feedback", "fxDelayDecay", fx.delayDecay, 0.0F, 0.99F, "%.2f", log);
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("0 = a single echo. Higher repeats longer. Capped below 1.0, where it would never decay.");
				}
				effectSlider(commandBus, name, "Wet##delay", "fxDelayWet", fx.delayWet, 0.0F, 1.0F, "%.2f", log);
				ImGui::SameLine();
				effectSlider(commandBus, name, "Dry##delay", "fxDelayDry", fx.delayDry, 0.0F, 1.0F, "%.2f", log);
			}

			// --- filter
			ImGui::TextDisabled("Filter");
			static constexpr const char* kFilterLabels[] = {"None", "Low pass (muffled)", "High pass (thin)"};
			int filterIndex = static_cast<int>(fx.filter);
			ImGui::SetNextItemWidth(180.0F);
			if (ImGui::Combo("##filter", &filterIndex, kFilterLabels, 3))
			{
				const auto chosen = static_cast<AudioEffects::Filter>(filterIndex);
				submitEntity(commandBus, name, "fxFilter", std::string(audioFilterName(chosen)), log);
			}
			if (fx.filter != AudioEffects::Filter::None)
			{
				effectSlider(commandBus, name, "Cutoff (Hz)", "fxCutoffHz", fx.cutoffHz, 20.0F, 20000.0F, "%.0f", log);
			}
		}
	}

	void drawAudioPanel(
		ProjectSettingsBus& bus,
		core::AudioEngine& audio,
		EditorScene& scene,
		AICommandBus& commandBus,
		const std::filesystem::path& projectRoot,
		AudioPanelState& state,
		const AudioPanelLogFn& log,
		const AudioImportFn& importSound)
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
		if (ImGui::Button("Import Sound from PC...", ImVec2(-1.0F, 0.0F)) && importSound)
		{
			if (const std::optional<std::string> imported = importSound())
			{
				// listAudioClips() re-scans every frame, so the new file is
				// already in `clips` next frame - just select it by name.
				const std::vector<std::string> refreshed = listAudioClips(projectRoot);
				for (int i = 0; i < static_cast<int>(refreshed.size()); ++i)
				{
					if (refreshed[static_cast<std::size_t>(i)] == *imported)
					{
						state.selectedClip = i;
						break;
					}
				}
				if (log) log(true, "Imported " + *imported);
			}
		}
		if (clips.empty())
		{
			ImGui::TextDisabled("No sounds yet - use Import Sound from PC above,");
			ImGui::TextDisabled("or copy .wav / .mp3 / .flac files into Game/Audio.");
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
		// --- objects ---------------------------------------------------------
		// Everything with "Has Audio Source" ticked in the Inspector. Scanned
		// each frame rather than cached: entities are created and deleted
		// constantly while editing, and a stale cache would list objects that
		// no longer exist.
		ImGui::SeparatorText("Objects");
		ImGui::TextDisabled("Tick \"Has Audio Source\" on an object to manage it here.");

		std::vector<const SceneEntity*> sources;
		for (const SceneEntity& entity : scene.entities())
		{
			if (entity.hasAudioSource)
			{
				sources.push_back(&entity);
			}
		}

		if (sources.empty())
		{
			ImGui::TextDisabled("(no objects use the Audio Manager yet)");
		}

		for (const SceneEntity* source : sources)
		{
			const SceneEntity& entity = *source;
			ImGui::PushID(entity.name.c_str());

			const bool hasClip = !entity.audioSource.clipAssetPath.empty();
			ImGui::TextUnformatted(entity.name.c_str());
			ImGui::SameLine();
			if (hasClip)
			{
				ImGui::TextDisabled("%s", entity.audioSource.clipAssetPath.c_str());
			}
			else
			{
				ImGui::TextColored(ImVec4(1.0F, 0.75F, 0.35F, 1.0F), "(no clip)");
			}

			// Upload copies a file in from anywhere; Link picks one already in
			// Game/Audio. Both write through the command bus, so the Inspector
			// updates in the same frame and undo works.
			if (ImGui::SmallButton("Upload...") && importSound)
			{
				if (const std::optional<std::string> imported = importSound())
				{
					submitEntity(commandBus, entity.name, "clipAssetPath", *imported, log);
				}
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Link..."))
			{
				ImGui::OpenPopup("LinkClip");
			}
			if (ImGui::BeginPopup("LinkClip"))
			{
				if (clips.empty())
				{
					ImGui::TextDisabled("No clips in Game/Audio yet - use Upload.");
				}
				for (const std::string& clip : clips)
				{
					if (ImGui::Selectable(clip.c_str(), clip == entity.audioSource.clipAssetPath))
					{
						submitEntity(commandBus, entity.name, "clipAssetPath", clip, log);
					}
				}
				ImGui::EndPopup();
			}

			ImGui::SameLine();
			if (ImGui::SmallButton("Preview"))
			{
				if (!hasClip)
				{
					if (log) log(false, entity.name + " has no clip to preview.");
				}
				else
				{
					// Preview WITH the object's own effects and fades, so what
					// is heard here is what Play will produce - the whole point
					// of tuning from this panel.
					core::EffectSettings settings = toEngineEffectSettings(entity.audioSource);
					if (!audio.playPreviewWithEffects(
							projectRoot, entity.audioSource.clipAssetPath, entity.audioSource.volume, settings) &&
						log)
					{
						log(false, "Could not play " + entity.audioSource.clipAssetPath);
					}
				}
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Stop"))
			{
				audio.stopWithFade(entity.audioSource.clipAssetPath, entity.audioSource.fadeOutSeconds);
			}

			// Effects are collapsed by default: most objects never need them,
			// and eleven sliders per object would bury the list.
			const bool expanded = state.expandedObject == entity.name;
			ImGui::SameLine();
			if (ImGui::SmallButton(expanded ? "Hide FX" : "Effects"))
			{
				state.expandedObject = expanded ? std::string{} : entity.name;
			}

			if (expanded)
			{
				ImGui::Indent();
				drawEffectControls(commandBus, entity, log);
				ImGui::Unindent();
			}

			ImGui::Separator();
			ImGui::PopID();
		}

		ImGui::TextUnformatted("Event hooks");
		ImGui::TextDisabled("Fired from Play start, pickup, projectiles, game over, boot audio.");

		const std::vector<AudioHook>& hooks = bus.settings().audioHooks;
		int removeIndex = -1;
		for (int i = 0; i < static_cast<int>(hooks.size()); ++i)
		{
			ImGui::PushID(i);
			ImGui::Text("%s  %s  (%.2f)%s",
				audioHookEventName(hooks[static_cast<std::size_t>(i)].event),
				hooks[static_cast<std::size_t>(i)].clipPath.c_str(),
				static_cast<double>(hooks[static_cast<std::size_t>(i)].volume),
				hooks[static_cast<std::size_t>(i)].loop ? "  [loop]" : "");
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
		ImGui::Checkbox("Loop (background music)", &state.newHookLoop);
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip(
				"On: the clip repeats until Play stops - this is how you get\n"
				"background music. Pair it with the on_play_start event.\n"
				"Off: the clip fires once each time the event happens.");
		}
		if (ImGui::Button("Add hook") && !clips.empty())
		{
			AddAudioHookCommand command;
			command.hook.event = kHookEvents[state.newHookEvent];
			command.hook.clipPath = clips[static_cast<std::size_t>(
				std::clamp(state.newHookClip, 0, static_cast<int>(clips.size()) - 1))];
			command.hook.volume = state.newHookVolume;
			command.hook.loop = state.newHookLoop;
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
