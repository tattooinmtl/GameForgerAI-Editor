#include "GameForger/Editor/ProjectSettingsPanel.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <system_error>

#include <imgui.h>

#include "GameForger/Editor/EditorLayout.hpp"

namespace gameforger::editor
{
	namespace
	{
		// Scene formats this project actually uses (see Game/Scenes).
		bool isSceneFile(const std::filesystem::path& path)
		{
			const std::string extension = path.extension().string();
			return extension == ".gfprod" || extension == ".gfai";
		}

		void copyInto(std::array<char, 128>& buffer, const std::string& text)
		{
			buffer.fill('\0');
			std::memcpy(buffer.data(), text.data(), std::min(text.size(), buffer.size() - 1));
		}

		void copyInto(std::array<char, 256>& buffer, const std::string& text)
		{
			buffer.fill('\0');
			std::memcpy(buffer.data(), text.data(), std::min(text.size(), buffer.size() - 1));
		}

		void refreshSceneChoices(
			ProjectSettingsPanelState& state, const std::filesystem::path& projectRoot)
		{
			state.sceneChoices.clear();
			const std::filesystem::path scenesDirectory = projectRoot / "Game" / "Scenes";
			std::error_code ec;
			for (const std::filesystem::directory_entry& entry :
				std::filesystem::directory_iterator(scenesDirectory, ec))
			{
				if (entry.is_regular_file(ec) && isSceneFile(entry.path()))
				{
					std::error_code relativeError;
					const std::filesystem::path relative =
						std::filesystem::relative(entry.path(), projectRoot, relativeError);
					if (!relativeError)
					{
						state.sceneChoices.push_back(relative.generic_string());
					}
				}
			}
			std::sort(state.sceneChoices.begin(), state.sceneChoices.end());
			state.sceneChoicesLoaded = true;
		}

		// Push a command and report the outcome. Validation failures are the
		// interesting case: they surface the bus's own message ("Scene X does
		// not exist under the project root") rather than silently reverting.
		void submit(
			ProjectSettingsBus& bus,
			const ProjectSettingsCommand& command,
			const ProjectSettingsLogFn& log)
		{
			const AICommandResult result = bus.execute(command);
			if (log)
			{
				log(result.success, result.message);
			}
		}

		// This panel docks into a narrow bottom strip alongside the Console, so
		// an inline ImGui label would be pushed off the right edge by a
		// full-width control (the same truncation the texture pickers hit in
		// 0.62). Label above, full-width control below, hidden "##" label.
		void fieldLabel(const char* text)
		{
			ImGui::TextDisabled("%s", text);
		}

		// Text fields commit on focus-loss, not per keystroke: execute() writes
		// both JSON files, and one disk write per character typed would be
		// absurd. IsItemDeactivatedAfterEdit fires exactly once, only when the
		// value actually changed.
		void stringField(
			ProjectSettingsBus& bus,
			const char* label,
			const char* key,
			std::array<char, 256>& buffer,
			const std::string& current,
			const ProjectSettingsLogFn& log)
		{
			if (!ImGui::IsAnyItemActive())
			{
				copyInto(buffer, current);
			}
			fieldLabel(label);
			ImGui::SetNextItemWidth(-1.0F);
			ImGui::PushID(key);
			ImGui::InputText("##field", buffer.data(), buffer.size());
			const bool committed = ImGui::IsItemDeactivatedAfterEdit();
			ImGui::PopID();
			if (committed)
			{
				SetProjectSettingCommand command;
				command.key = key;
				command.stringValue = buffer.data();
				submit(bus, command, log);
			}
		}

		void drawBootStepEditor(
			ProjectSettingsBus& bus, ProjectSettingsPanelState& state, const ProjectSettingsLogFn& log)
		{
			ImGui::SeparatorText("Startup Sequence");
			ImGui::TextWrapped(
				"Runs once when Play starts, before the player gets control. "
				"Use it for logos, an intro cutscene, or a loading animation.");

			const std::vector<BootStep>& steps = bus.settings().bootSequence;
			if (steps.empty())
			{
				ImGui::TextDisabled("(no steps - the player gets control immediately)");
			}

			// Mutations are deferred to after the loop: executing mid-iteration
			// would resize the vector the loop is walking.
			int removeIndex = -1;
			int moveFrom = -1;
			int moveTo = -1;

			for (int index = 0; index < static_cast<int>(steps.size()); ++index)
			{
				ImGui::PushID(index);
				ImGui::Text("%d.", index + 1);
				ImGui::SameLine();
				ImGui::TextUnformatted(describeBootStep(steps[static_cast<std::size_t>(index)]).c_str());

				ImGui::SameLine(ImGui::GetContentRegionAvail().x - 90.0F);
				if (ImGui::SmallButton("Up") && index > 0)
				{
					moveFrom = index;
					moveTo = index - 1;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("Dn") && index + 1 < static_cast<int>(steps.size()))
				{
					moveFrom = index;
					moveTo = index + 1;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("X"))
				{
					removeIndex = index;
				}
				ImGui::PopID();
			}

			if (moveFrom >= 0)
			{
				submit(bus, MoveBootStepCommand{moveFrom, moveTo}, log);
			}
			if (removeIndex >= 0)
			{
				submit(bus, RemoveBootStepCommand{removeIndex}, log);
			}

			ImGui::Separator();

			static constexpr std::array<const char*, 6> kKindLabels{
				"Wait (seconds)",
				"Play animation on entity",
				"Play cutscene (storyboard shot)",
				"Play audio clip",
				"Lock player input",
				"Unlock player input",
			};
			ImGui::SetNextItemWidth(-1.0F);
			ImGui::Combo("##NewStepKind", &state.newStepKind, kKindLabels.data(),
				static_cast<int>(kKindLabels.size()));

			const auto kind = static_cast<BootStep::Kind>(state.newStepKind);
			switch (kind)
			{
				case BootStep::Kind::WaitSeconds:
					ImGui::SetNextItemWidth(-1.0F);
					ImGui::DragFloat("##NewStepSeconds", &state.newStepSeconds, 0.05F, 0.0F, 600.0F, "%.2f s");
					break;
				case BootStep::Kind::PlayAnimation:
					ImGui::SetNextItemWidth(-1.0F);
					ImGui::InputTextWithHint("##NewStepEntity", "Entity name",
						state.newStepTargetEntity.data(), state.newStepTargetEntity.size());
					break;
				case BootStep::Kind::PlayCutscene:
					ImGui::SetNextItemWidth(-1.0F);
					ImGui::InputTextWithHint("##NewStepShot", "Storyboard shot name",
						state.newStepShotName.data(), state.newStepShotName.size());
					break;
				case BootStep::Kind::PlayAudio:
					ImGui::SetNextItemWidth(-1.0F);
					ImGui::InputTextWithHint("##NewStepClip", "Game/Audio/clip.wav",
						state.newStepClipPath.data(), state.newStepClipPath.size());
					break;
				case BootStep::Kind::LockPlayerInput:
				case BootStep::Kind::UnlockPlayerInput:
					ImGui::TextDisabled("(no options)");
					break;
			}

			if (ImGui::Button("Add Step", ImVec2(-1.0F, 0.0F)))
			{
				AddBootStepCommand command;
				command.step.kind = kind;
				command.step.seconds = state.newStepSeconds;
				command.step.targetEntity = state.newStepTargetEntity.data();
				command.step.shotName = state.newStepShotName.data();
				command.step.clipPath = state.newStepClipPath.data();
				submit(bus, command, log);
			}
		}
	}

	void drawProjectSettingsPanel(
		ProjectSettingsBus& bus,
		const std::filesystem::path& projectRoot,
		ProjectSettingsPanelState& state,
		const ProjectSettingsLogFn& log)
	{
		if (state.focusRequested)
		{
			ImGui::SetNextWindowFocus();
			state.focusRequested = false;
		}

		ImGui::Begin("Project Settings");

		if (!state.sceneChoicesLoaded)
		{
			refreshSceneChoices(state, projectRoot);
		}

		const ProjectSettings& settings = bus.settings();

		ImGui::TextDisabled("Game/Project.json + Game/Settings.json");
		ImGui::SameLine();
		if (ImGui::SmallButton("Reload"))
		{
			const ProjectSettingsIoResult reloaded = bus.reload();
			refreshSceneChoices(state, projectRoot);
			if (log)
			{
				log(reloaded.success, reloaded.message);
			}
		}
		ImGui::Separator();

		ImGui::SeparatorText("Project");

		if (!ImGui::IsAnyItemActive())
		{
			copyInto(state.name, settings.name);
		}
		fieldLabel("Name");
		ImGui::SetNextItemWidth(-1.0F);
		ImGui::InputText("##Name", state.name.data(), state.name.size());
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			SetProjectSettingCommand command;
			command.key = "name";
			command.stringValue = state.name.data();
			submit(bus, command, log);
		}

		// A combo rather than a text field: a mistyped startup scene is only
		// discovered when the game fails to launch, and the bus rejects
		// nonexistent scenes anyway - so offer only the real ones.
		fieldLabel("Startup Scene");
		ImGui::SetNextItemWidth(-1.0F);
		if (ImGui::BeginCombo("##StartupScene", settings.startupScene.c_str()))
		{
			for (const std::string& scene : state.sceneChoices)
			{
				const bool selected = scene == settings.startupScene;
				if (ImGui::Selectable(scene.c_str(), selected))
				{
					SetProjectSettingCommand command;
					command.key = "startupScene";
					command.stringValue = scene;
					submit(bus, command, log);
				}
			}
			if (state.sceneChoices.empty())
			{
				ImGui::TextDisabled("(no scenes found in Game/Scenes)");
			}
			ImGui::EndCombo();
		}

		stringField(bus, "Skeleton Profile", "skeletonProfile", state.skeletonProfile, settings.skeletonProfile, log);
		stringField(bus, "AI Providers", "aiProviders", state.aiProviders, settings.aiProvidersPath, log);
		stringField(bus, "Script Directory", "scriptDirectory", state.scriptDirectory, settings.scriptDirectory, log);

		if (!settings.assetDirectories.empty())
		{
			ImGui::SeparatorText("Asset Directories");
			for (const std::string& directory : settings.assetDirectories)
			{
				ImGui::BulletText("%s", directory.c_str());
			}
		}

		ImGui::SeparatorText("Player Settings");

		if (!ImGui::IsAnyItemActive())
		{
			state.mouseSensitivity = settings.mouseSensitivity;
			state.targetFps = settings.targetFps;
		}
		fieldLabel("Mouse Sensitivity");
		ImGui::SetNextItemWidth(-1.0F);
		ImGui::DragFloat("##MouseSensitivity", &state.mouseSensitivity, 0.005F, 0.001F, 10.0F, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			SetProjectSettingCommand command;
			command.key = "mouseSensitivity";
			command.numberValue = static_cast<double>(state.mouseSensitivity);
			submit(bus, command, log);
		}

		fieldLabel("Target FPS");
		ImGui::SetNextItemWidth(-1.0F);
		ImGui::DragInt("##TargetFps", &state.targetFps, 1.0F, 15, 480);
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			SetProjectSettingCommand command;
			command.key = "targetFps";
			command.numberValue = static_cast<double>(state.targetFps);
			submit(bus, command, log);
		}

		drawBootStepEditor(bus, state, log);

		ImGui::End();

		// Requested placement: a tab between Console and Storyboard, in the
		// node those two already share. Done after End() so the window
		// object exists. Never splits or resizes the layout - only this
		// panel's tab index changes, and only while it still lives in that
		// same node (or is not docked yet).
		if (!state.dockPlacementDone)
		{
			const ImGuiID consoleId = dockIdOfWindow("Console");
			const ImGuiID storyboardId = dockIdOfWindow("Storyboard");
			const ImGuiID settingsId = dockIdOfWindow("Project Settings");
			if (consoleId == 0 || storyboardId == 0)
			{
				// Anchors have not been submitted yet this session.
			}
			else if (consoleId != storyboardId)
			{
				state.dockPlacementDone = true;
			}
			else if (settingsId != 0 && settingsId != consoleId)
			{
				// User dragged this panel into a different node. Leave it.
				state.dockPlacementDone = true;
			}
			else if (dockWindowBetween("Project Settings", "Console", "Storyboard"))
			{
				state.dockPlacementDone = true;
			}
		}
	}
}
