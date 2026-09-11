#include "GameForger/Editor/ScriptsPanel.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

#include <imgui.h>

#include "GameForger/Editor/ScriptGenerator.hpp"

namespace gameforger::editor
{
	namespace
	{
		void refreshScriptList(ScriptsPanelState& state, const std::filesystem::path& projectRoot)
		{
			state.scriptPaths.clear();
			const std::filesystem::path scriptsDir = projectRoot / "Game" / "Scripts";
			std::error_code error;
			if (!std::filesystem::is_directory(scriptsDir, error))
			{
				state.listLoaded = true;
				return;
			}
			// Recursive, so Game/Scripts/generated/ (where Mind Graph writes)
			// shows up here too rather than being invisible to the one panel
			// that is supposed to list every script.
			for (const std::filesystem::directory_entry& entry :
				 std::filesystem::recursive_directory_iterator(scriptsDir, error))
			{
				if (!entry.is_regular_file(error) || entry.path().extension() != ".lua")
				{
					continue;
				}
				const std::filesystem::path relative =
					std::filesystem::relative(entry.path(), projectRoot, error);
				if (!relative.empty())
				{
					state.scriptPaths.push_back(relative.generic_string());
				}
			}
			std::sort(state.scriptPaths.begin(), state.scriptPaths.end());
			state.listLoaded = true;
		}

		void loadScriptIntoBuffer(
			ScriptsPanelState& state, const std::filesystem::path& projectRoot, const std::string& relativePath)
		{
			std::ifstream input(projectRoot / relativePath, std::ios::binary);
			if (!input)
			{
				state.status = "Could not open " + relativePath;
				state.statusSuccess = false;
				return;
			}
			const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
			state.buffer.fill('\0');
			const std::size_t copied = std::min(text.size(), state.buffer.size() - 1);
			std::memcpy(state.buffer.data(), text.data(), copied);
			if (copied < text.size())
			{
				// Truncating silently would make Save destroy the tail of the
				// file. Say so, and the user can widen the buffer or edit
				// externally rather than losing work.
				state.status = relativePath + " is larger than the editor buffer - DO NOT SAVE, it would truncate.";
				state.statusSuccess = false;
			}
			else
			{
				state.status = "Opened " + relativePath;
				state.statusSuccess = true;
			}
			state.loadedPath = relativePath;
			state.bufferDirty = false;
		}

		void startAiRequest(
			ScriptsPanelState& state,
			const AIProviderClient& providerClient,
			const std::string& activeProviderId,
			const std::string& entityName,
			const std::string& gameTitle,
			const std::string& targetPath,
			const bool create)
		{
			if (state.worker.joinable())
			{
				state.worker.join();
			}
			{
				const std::lock_guard<std::mutex> lock(state.resultMutex);
				state.hasResult = false;
			}
			state.busy = true;

			const std::string instruction = state.aiPrompt.data();
			const std::string existing = create ? std::string() : std::string(state.buffer.data());

			state.worker = std::thread(
				[&state, &providerClient, activeProviderId, entityName, instruction, existing, targetPath, create]()
				{
					ScriptGenerationResult result;
					try
					{
						result = create
							? generateEntityScript(providerClient, activeProviderId, entityName, instruction)
							: modifyEntityScript(
								  providerClient, activeProviderId, entityName, existing, instruction);
					}
					catch (const std::exception& exception)
					{
						result = {false, std::string("AI request failed: ") + exception.what()};
					}
					catch (...)
					{
						result = {false, "AI request failed."};
					}
					const std::lock_guard<std::mutex> lock(state.resultMutex);
					state.resultSuccess = result.success;
					state.resultContent = result.content;
					state.resultTargetPath = targetPath;
					state.hasResult = true;
					state.busy = false;
				});
		}
	}

	void drawScriptsPanel(
		ScriptsPanelState& state,
		EditorScene& scene,
		AICommandBus& commandBus,
		const AIProviderClient& providerClient,
		const std::string& activeProviderId,
		const std::string& gameTitle,
		const std::filesystem::path& projectRoot,
		const int selectedEntityId,
		const ScriptsLogFn& log)
	{
		if (!state.open)
		{
			return;
		}
		if (!ImGui::Begin("Scripts", &state.open))
		{
			ImGui::End();
			return;
		}

		if (!state.listLoaded)
		{
			refreshScriptList(state, projectRoot);
		}

		// Drain the worker on the UI thread, so the buffer is only ever
		// mutated from one thread.
		{
			const std::lock_guard<std::mutex> lock(state.resultMutex);
			if (state.hasResult)
			{
				state.hasResult = false;
				if (state.resultSuccess)
				{
					state.buffer.fill('\0');
					const std::size_t copied =
						std::min(state.resultContent.size(), state.buffer.size() - 1);
					std::memcpy(state.buffer.data(), state.resultContent.data(), copied);
					state.loadedPath = state.resultTargetPath;
					state.bufferDirty = true;
					state.status =
						"AI wrote " + state.resultTargetPath + " - review it, then Save.";
					state.statusSuccess = true;
					log(true, "Scripts: " + state.status);
				}
				else
				{
					state.status = state.resultContent;
					state.statusSuccess = false;
					log(false, "Scripts: " + state.resultContent);
				}
			}
		}

		const SceneEntity* selected = scene.findEntity(selectedEntityId);

		// ---- left: the file list ----
		ImGui::BeginChild("##scriptList", ImVec2(240.0F, -150.0F), true);
		ImGui::TextUnformatted("Scripts");
		ImGui::SameLine();
		if (ImGui::SmallButton("Refresh"))
		{
			refreshScriptList(state, projectRoot);
		}
		ImGui::Separator();
		for (std::size_t index = 0; index < state.scriptPaths.size(); ++index)
		{
			const std::string& path = state.scriptPaths[index];
			const bool isOpen = path == state.loadedPath;
			// Mark what is attached to the current selection, so the list
			// answers "what is on this object" without leaving the panel.
			const bool attached = selected != nullptr
				&& std::find(selected->scripts.begin(), selected->scripts.end(), path)
					!= selected->scripts.end();
			std::string label = (attached ? "* " : "  ") + path.substr(path.rfind('/') + 1);
			if (ImGui::Selectable(label.c_str(), isOpen))
			{
				loadScriptIntoBuffer(state, projectRoot, path);
				state.selectedIndex = static_cast<int>(index);
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s%s", path.c_str(), attached ? "\n(attached to the selected object)" : "");
			}
		}
		if (state.scriptPaths.empty())
		{
			ImGui::TextDisabled("No .lua files in Game/Scripts yet.");
		}
		ImGui::EndChild();

		ImGui::SameLine();

		// ---- right: the editor ----
		ImGui::BeginChild("##scriptEditor", ImVec2(0.0F, -150.0F), false);
		if (state.loadedPath.empty())
		{
			ImGui::TextDisabled("Select a script on the left, or ask the AI to write one below.");
		}
		else
		{
			ImGui::Text("%s%s", state.loadedPath.c_str(), state.bufferDirty ? " *" : "");
			ImGui::SameLine();
			if (ImGui::SmallButton("Save"))
			{
				const AICommandResult saved = commandBus.execute(CreateScriptCommand{
					state.loadedPath, "lua", std::string(state.buffer.data()), true});
				state.status = saved.message;
				state.statusSuccess = saved.success;
				state.bufferDirty = !saved.success;
				log(saved.success, "Scripts: " + saved.message);
				if (saved.success)
				{
					refreshScriptList(state, projectRoot);
				}
			}
			// The load-to-object button is ALWAYS here, whether or not
			// something is selected. A control that vanishes when it cannot be
			// used teaches nothing - you are left wondering whether the
			// feature exists at all. Present but refusing, with a reason, is
			// the honest version.
			const bool attached = selected != nullptr
				&& std::find(selected->scripts.begin(), selected->scripts.end(), state.loadedPath)
					!= selected->scripts.end();
			ImGui::SameLine();
			if (attached)
			{
				if (ImGui::SmallButton("Remove from Object"))
				{
					const AICommandResult result =
						commandBus.execute(DetachScriptCommand{selected->name, state.loadedPath});
					state.status = result.message;
					state.statusSuccess = result.success;
					log(result.success, "Scripts: " + result.message);
				}
			}
			else if (ImGui::SmallButton("Load to Object"))
			{
				if (selected == nullptr)
				{
					// Said here AND in the status line, because the click
					// happens at the button and the eye is already there.
					state.status = "No object is selected! Pick one in the Hierarchy first.";
					state.statusSuccess = false;
					log(false, "Scripts: no object is selected.");
				}
				else
				{
					const AICommandResult result =
						commandBus.execute(AttachScriptCommand{selected->name, state.loadedPath});
					state.status = result.message;
					state.statusSuccess = result.success;
					log(result.success, "Scripts: " + result.message);
				}
			}
			ImGui::SameLine();
			if (selected != nullptr)
			{
				ImGui::TextDisabled("(%s)", selected->name.c_str());
			}
			else
			{
				ImGui::TextColored(
					ImVec4(0.95F, 0.75F, 0.35F, 1.0F), "no object selected");
			}

			if (ImGui::InputTextMultiline(
					"##source", state.buffer.data(), state.buffer.size(), ImVec2(-1.0F, -1.0F)))
			{
				state.bufferDirty = true;
			}
		}
		ImGui::EndChild();

		// ---- bottom: the AI prompt, where it belongs ----
		ImGui::Separator();
		const bool busy = state.busy.load();

		ImGui::RadioButton("Modify this script", !state.createMode);
		if (ImGui::IsItemClicked())
		{
			state.createMode = false;
		}
		ImGui::SameLine();
		ImGui::RadioButton("Create a new script", state.createMode);
		if (ImGui::IsItemClicked())
		{
			state.createMode = true;
		}

		if (state.createMode)
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(220.0F);
			ImGui::InputTextWithHint(
				"##newName", "Purpose, e.g. FPS Controller", state.newScriptName.data(),
				state.newScriptName.size());
			ImGui::SameLine();
			// Auto-named from the game title, so generated scripts cannot
			// accumulate as FPSController / PlayerFPS / fps_controller /
			// player_fps the way Game/Scripts already had.
			ImGui::TextDisabled(
				"-> %s", suggestedScriptFileName(state.newScriptName.data(), gameTitle).c_str());
		}

		ImGui::SetNextItemWidth(-120.0F);
		ImGui::InputTextWithHint(
			"##aiPrompt",
			state.createMode ? "Describe what the new script should do..."
							 : "Describe the change, e.g. make it sprint on Shift",
			state.aiPrompt.data(), state.aiPrompt.size());
		ImGui::SameLine();

		const bool canAsk = !busy && state.aiPrompt[0] != '\0'
			&& (state.createMode ? state.newScriptName[0] != '\0' : !state.loadedPath.empty());
		if (!canAsk)
		{
			ImGui::BeginDisabled();
		}
		if (ImGui::Button(busy ? "Working..." : "Ask AI", ImVec2(-1.0F, 0.0F)))
		{
			const std::string entityName = selected != nullptr ? selected->name : std::string("this object");
			const std::string target = state.createMode
				? "Game/Scripts/" + suggestedScriptFileName(state.newScriptName.data(), gameTitle)
				: state.loadedPath;
			startAiRequest(
				state, providerClient, activeProviderId, entityName, gameTitle, target, state.createMode);
			log(true, std::string("Scripts: asking the AI to ")
					+ (state.createMode ? "write " : "modify ") + target + "...");
		}
		if (!canAsk)
		{
			ImGui::EndDisabled();
			if (!busy)
			{
				// Say WHY the button is off rather than leaving it mysteriously
				// greyed.
				ImGui::TextDisabled(
					state.createMode ? "Give the new script a purpose and a description."
									 : "Open a script to modify, and describe the change.");
			}
		}

		if (!state.status.empty())
		{
			ImGui::TextColored(
				state.statusSuccess ? ImVec4(0.45F, 0.85F, 0.50F, 1.0F) : ImVec4(0.95F, 0.40F, 0.35F, 1.0F),
				"%s", state.status.c_str());
		}

		ImGui::End();
	}

	void shutdownScriptsPanel(ScriptsPanelState& state) noexcept
	{
		if (state.worker.joinable())
		{
			state.worker.join();
		}
	}
}
