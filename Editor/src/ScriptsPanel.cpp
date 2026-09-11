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
		enum class PresetKind
		{
			MovementController,
			Rigidbody,
			ColliderOnly,
			// A real script, unlike ColliderOnly, but not a physics/movement
			// driver either - doesn't call self.entity:setPosition() or
			// self.physics:resolve(), so it never fights a MovementController/
			// Rigidbody for the entity's transform and stays free to combine
			// with either (or neither) exactly like ColliderOnly does.
			Utility,
			// enemy_ai.lua DOES call self.entity:setPosition() every frame
			// (wander/chase movement), same as a MovementController - so it's
			// exclusive with those too, just its own kind rather than reusing
			// MovementController (which is worded as player-input-driven).
			EnemyAI
		};

		struct ScriptPreset
		{
			const char* label;
			const char* path; // unused for ColliderOnly
			const char* description;
			PresetKind kind;
			// Whether applying this preset also marks the object solid. This is
			// per-preset data rather than derived from `kind` because the two do
			// not line up: Ranged Attacker is a Utility that SHOULD be solid (it
			// is an enemy or a tower), while Game Manager, Audio Manager, Door,
			// Keypad and Key Item are Utilities that should not be. The old
			// Inspector version set Collider on every preset that had a script,
			// which quietly made an audio manager attached to an empty block the
			// player - contradicting this table's own descriptions.
			bool marksCollider;
		};

		constexpr std::array<ScriptPreset, kScriptPresetCount> kPresets{{
			{"FPS Controller",
			 "Game/Scripts/fps_controller.lua",
			 "WASD move, Space jump, Shift sprint, mouse-look. First-person camera by default - "
			 "press C in Play to swap to third-person. Also marks this object as a Collider.",
			 PresetKind::MovementController,
			 true},
			{"Third-Person Controller",
			 "Game/Scripts/third_person_controller.lua",
			 "Same movement/sprint/jump/mouse-look, plus the capsule turns to face where it's "
			 "moving. Third-person camera by default - press C to swap to first-person. Also marks "
			 "this object as a Collider.",
			 PresetKind::MovementController,
			 true},
			{"Rigidbody",
			 "Game/Scripts/rigidbody.lua",
			 "Falls under gravity and lands on/collides with Collider-enabled objects - for props "
			 "like a cube you want to fall and stay put. Also marks this object as a Collider. Not "
			 "for a player character - use FPS/Third-Person Controller instead, which already "
			 "include their own gravity and ground collision.",
			 PresetKind::Rigidbody,
			 true},
			{"Collider Only",
			 nullptr,
			 "No script - just marks this object solid, so an FPS/Third-Person/Rigidbody script's "
			 "self.physics:resolve() collides with it. Use for ground, walls, and platforms.",
			 PresetKind::ColliderOnly,
			 true},
			{"Inventory & Pickup",
			 "Game/Scripts/inventory_system.lua",
			 "E picks up any nearby \"Is Pickup Item\" object (Inspector), I opens the inventory "
			 "grid. Attach to the player alongside FPS/Third-Person Controller - has no effect on "
			 "its own without one of those claiming the camera. pickup_range/pickup_height_tolerance "
			 "are editable in the script itself.",
			 PresetKind::Utility,
			 false},
			{"Enemy AI",
			 "Game/Scripts/enemy_ai.lua",
			 "Wanders near its spawn point until an object tagged target_tag (default \"Player\") "
			 "comes within search_radius, then chases it until it escapes escape_radius. Shows the "
			 "red detection icon while chasing. All radii/speeds/target_tag are editable in the "
			 "script itself. Also marks this object as a Collider.",
			 PresetKind::EnemyAI,
			 true},
			{"Ranged Attacker",
			 "Game/Scripts/ranged_attacker.lua",
			 "Fires projectiles at the nearest object tagged target_tag once it's within fire_range "
			 "- attach to an enemy (target_tag=\"Player\"), the player (target_tag=\"Enemy\"), or a "
			 "future tower (either). Optional muzzle_tag names a separate tagged entity to fire "
			 "from (e.g. \"player_gun_muzzle\") instead of this object's own position - just a "
			 "regular Inspector tag, no extra setup needed. Needs a target_tag that actually exists "
			 "in the scene to do anything. Also marks this object as a Collider - fine for an enemy "
			 "or tower, uncheck it after applying if used on something that shouldn't block movement.",
			 PresetKind::Utility,
			 true},
			{"Catapult Controller",
			 "Game/Scripts/catapult_controller.lua",
			 "Attach to a catapult's base (needs Is Catapult + a \"PlayerCatapult\" tag, Inspector). "
			 "F grabs/tows it toward you, F again drops it; walk up and press E to load/aim, mouse to "
			 "aim, hold T to power up the throw, R to fire. Finds its own arm by proximity to an "
			 "entity tagged \"CatapultArm\" - tag your imported arm object with that. All ranges/"
			 "speeds/power-charge-time are editable in the script itself.",
			 PresetKind::Utility,
			 false},
			{"Game Manager",
			 "Game/Scripts/game_manager.lua",
			 "Session-wide state that isn't any one object's business. Owns cursor lock - which "
			 "used to be a checkbox on every entity's Inspector - and registers itself in the "
			 "manager list. Attach to ONE entity per scene (an empty is fine). Doesn't touch "
			 "the transform, so it combines with anything.",
			 PresetKind::Utility,
			 false},
			{"Audio Manager",
			 "Game/Scripts/audio_manager.lua",
			 "Plays background music on a loop and exposes self.audio to every other script "
			 "(play/stop/setMasterVolume/isPlaying). Set music_clip to something in Game/Audio - "
			 "use the Audio panel's Import Sound from PC to put one there. Doesn't touch the "
			 "transform.",
			 PresetKind::Utility,
			 false},
			{"Weapons System",
			 "Game/Scripts/weapons_system.lua",
			 "Viewmodel, weapon switching, firing and melee in one pack. Attach to the player "
			 "alongside FPS Controller. Parent each weapon model as a child of the scene's Main "
			 "Camera - the engine drives that camera from the live view, so anything under it "
			 "rides the player's eyes and becomes a viewmodel. Mouse wheel or number keys switch, "
			 "left mouse attacks. Slots/cooldowns/reach are editable in the script itself.",
			 PresetKind::Utility,
			 false},
			{"Door",
			 "Game/Scripts/door_interaction.lua",
			 "E opens and closes this object, swinging it around its own Pivot - set Pivot to the "
			 "hinge edge first, or it spins around its middle. Optionally locked behind a key item "
			 "or a keypad code (lock_mode in the script). Doesn't drive its own position, so it "
			 "combines with anything.",
			 PresetKind::Utility,
			 false},
			{"Keypad Panel",
			 "Game/Scripts/keypad_panel.lua",
			 "Walk up, E to activate, type a code, Enter to submit. A correct code unlocks every "
			 "Door in the scene set to that same code - no link between the two objects needed. "
			 "Entry progress prints to the Console.",
			 PresetKind::Utility,
			 false},
			{"Key Item",
			 "Game/Scripts/key_item.lua",
			 "Marks this object as the key to a locked Door. Also tick \"Is Pickup Item\" and set "
			 "its Item Name to match the Door's key_item_name. Walking close enough to pick it up "
			 "unlocks every Door waiting on that key.",
			 PresetKind::Utility,
			 false},
		}};

		// At most one physics-driving preset at a time: each runs its own
		// independent gravity/ground-collision every frame and calls
		// self.entity:setPosition(), so attaching two to the same object makes
		// them fight over its transform - symptoms are frozen or jittery
		// movement, a broken jump, and a third-person camera that looks like it
		// isn't following. Collider Only and the Utilities never touch the
		// transform, so they stay free to combine with any of these.
		constexpr bool isExclusivePreset(const PresetKind kind) noexcept
		{
			return kind == PresetKind::MovementController || kind == PresetKind::Rigidbody
				|| kind == PresetKind::EnemyAI;
		}

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

		// Attaches one preset to `entityName`, marking it solid when that preset
		// says it should be. Shared by the Presets section and the script
		// list's right-click menu so the two cannot drift.
		AICommandResult applyPreset(
			AICommandBus& commandBus, const ScriptPreset& preset, const std::string& entityName)
		{
			AICommandResult result{true, false, ""};
			if (preset.kind != PresetKind::ColliderOnly)
			{
				result = commandBus.execute(AttachScriptCommand{entityName, preset.path});
			}
			if (result.success && preset.marksCollider)
			{
				const AICommandResult collider =
					commandBus.execute(SetPropertyCommand{entityName, "Collider", "enabled", true});
				if (preset.kind == PresetKind::ColliderOnly)
				{
					result = collider;
				}
			}
			return result;
		}

		// The built-in behaviour packs. Full panel width, not inside the file
		// list's narrow column: when this lived in a 260px child, the "-> which
		// object" label beside the Apply button was clipped off the right edge,
		// so nothing on screen ever showed that the target had changed.
		void drawPresetsSection(
			ScriptsPanelState& state,
			AICommandBus& commandBus,
			const SceneEntity* selected,
			const ScriptsLogFn& log)
		{
			ImGui::SetNextItemOpen(state.presetsExpanded, ImGuiCond_Always);
			state.presetsExpanded = ImGui::CollapsingHeader("Presets");
			if (!state.presetsExpanded)
			{
				return;
			}

			ImGui::TextDisabled(
				"Tick one or more, then Apply to Object. Ticks stay put afterwards, so the same set "
				"can go onto several objects one after another.");

			// Three columns: fourteen stacked checkboxes pushed the Apply
			// button below the fold.
			if (ImGui::BeginTable("##presetGrid", 3, ImGuiTableFlags_SizingStretchSame))
			{
				for (std::size_t index = 0; index < kPresets.size(); ++index)
				{
					ImGui::TableNextColumn();
					ImGui::PushID(static_cast<int>(index));
					const bool wasSelected = state.presetSelected[index];
					if (ImGui::Checkbox(kPresets[index].label, &state.presetSelected[index]) && !wasSelected
						&& isExclusivePreset(kPresets[index].kind))
					{
						for (std::size_t other = 0; other < kPresets.size(); ++other)
						{
							if (other != index && isExclusivePreset(kPresets[other].kind))
							{
								state.presetSelected[other] = false;
							}
						}
					}
					if (ImGui::IsItemHovered())
					{
						// Tooltip rather than inline text: these descriptions
						// are paragraphs, and printing fourteen of them would
						// bury everything below.
						ImGui::BeginTooltip();
						ImGui::PushTextWrapPos(420.0F);
						ImGui::TextUnformatted(kPresets[index].description);
						ImGui::PopTextWrapPos();
						ImGui::EndTooltip();
					}
					ImGui::PopID();
				}
				ImGui::EndTable();
			}

			const bool anySelected = std::any_of(
				state.presetSelected.begin(), state.presetSelected.end(), [](const bool s) { return s; });
			if (!anySelected)
			{
				ImGui::BeginDisabled();
			}
			// Present-but-refusing when nothing is selected, same as the
			// Load to Object button below - see the note there.
			if (ImGui::Button("Apply to Object"))
			{
				if (selected == nullptr)
				{
					state.status = "No object is selected! Pick one in the Hierarchy first.";
					state.statusSuccess = false;
					log(false, "Scripts: no object is selected.");
				}
				else
				{
					bool anySuccess = false;
					for (std::size_t index = 0; index < kPresets.size(); ++index)
					{
						if (!state.presetSelected[index])
						{
							continue;
						}
						const AICommandResult result =
							applyPreset(commandBus, kPresets[index], selected->name);
						log(result.success, std::string(kPresets[index].label) + ": " + result.message);
						anySuccess = anySuccess || result.success;
						// Ticks are deliberately NOT cleared here. Clearing them
						// left the Apply button disabled the instant it was
						// used, so applying the same preset to a second object
						// looked like the panel had stopped working.
					}
					state.status = anySuccess
						? "Applied to " + selected->name + ". Select another object and Apply again, "
							"or untick above."
						: "Nothing applied to " + selected->name + " - see the Console.";
					state.statusSuccess = anySuccess;
				}
			}
			if (!anySelected)
			{
				ImGui::EndDisabled();
			}
			ImGui::SameLine();
			if (ImGui::Button("Clear ticks"))
			{
				state.presetSelected.fill(false);
			}
			ImGui::SameLine();
			if (selected != nullptr)
			{
				ImGui::TextDisabled("-> %s", selected->name.c_str());
			}
			else
			{
				ImGui::TextColored(ImVec4(0.95F, 0.75F, 0.35F, 1.0F), "no object selected");
			}
		}

		void startAiRequest(
			ScriptsPanelState& state,
			const AIProviderClient& providerClient,
			const std::string& activeProviderId,
			const std::string& entityName,
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
		// Must happen BEFORE Begin, and by window name: when the panel is a
		// background tab, Begin returns false and returns early, so a focus
		// call inside the body would never run - which is exactly why the
		// buttons that raise this panel appeared to do nothing.
		if (state.requestFocus)
		{
			state.requestFocus = false;
			ImGui::SetWindowFocus("Scripts");
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

		// The selection moved. Drop the old status: it names the previous
		// object, and a line reading "Applied to Capsule" under a panel now
		// pointing at something else is indistinguishable from the panel being
		// stuck on the capsule.
		if (selectedEntityId != state.lastSelectedEntityId)
		{
			state.lastSelectedEntityId = selectedEntityId;
			state.status.clear();
			state.statusSuccess = true;
		}

		// Who everything on this panel acts on, stated once at the top where it
		// cannot be clipped or missed. Every attach below goes to THIS object.
		ImGui::TextUnformatted("Target object:");
		ImGui::SameLine();
		if (selected != nullptr)
		{
			ImGui::TextColored(ImVec4(0.45F, 0.85F, 0.50F, 1.0F), "%s", selected->name.c_str());
		}
		else
		{
			ImGui::TextColored(
				ImVec4(0.95F, 0.75F, 0.35F, 1.0F), "none - pick one in the Hierarchy");
		}
		ImGui::Separator();

		drawPresetsSection(state, commandBus, selected, log);
		ImGui::Separator();

		// Someone outside the panel asked for a specific file - the Project
		// browser, or the Inspector's list of attached scripts.
		if (!state.requestOpenPath.empty())
		{
			const std::string wanted = state.requestOpenPath;
			state.requestOpenPath.clear();
			if (wanted != state.loadedPath)
			{
				refreshScriptList(state, projectRoot);
				loadScriptIntoBuffer(state, projectRoot, wanted);
				const auto found = std::find(state.scriptPaths.begin(), state.scriptPaths.end(), wanted);
				state.selectedIndex = found == state.scriptPaths.end()
					? -1
					: static_cast<int>(std::distance(state.scriptPaths.begin(), found));
			}
		}

		// ---- left: the file list ----
		ImGui::BeginChild("##scriptList", ImVec2(260.0F, -150.0F), true);
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
			ImGui::PushID(static_cast<int>(index));
			if (ImGui::Selectable(label.c_str(), isOpen))
			{
				loadScriptIntoBuffer(state, projectRoot, path);
				state.selectedIndex = static_cast<int>(index);
			}
			// Right-click acts on the row under the cursor without disturbing
			// which file is open in the editor - you can send a script to an
			// object while reading a different one.
			if (ImGui::BeginPopupContextItem("##scriptRowMenu"))
			{
				ImGui::TextDisabled("%s", path.c_str());
				ImGui::Separator();
				if (ImGui::MenuItem("Send to Object", nullptr, false, !attached))
				{
					if (selected == nullptr)
					{
						state.status = "No object is selected! Pick one in the Hierarchy first.";
						state.statusSuccess = false;
						log(false, "Scripts: no object is selected.");
					}
					else
					{
						const AICommandResult result =
							commandBus.execute(AttachScriptCommand{selected->name, path});
						state.status = result.message;
						state.statusSuccess = result.success;
						log(result.success, "Scripts: " + result.message);
					}
				}
				if (ImGui::MenuItem("Remove from Object", nullptr, false, attached))
				{
					const AICommandResult result =
						commandBus.execute(DetachScriptCommand{selected->name, path});
					state.status = result.message;
					state.statusSuccess = result.success;
					log(result.success, "Scripts: " + result.message);
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Open in Editor"))
				{
					loadScriptIntoBuffer(state, projectRoot, path);
					state.selectedIndex = static_cast<int>(index);
				}
				if (ImGui::MenuItem("Copy Path"))
				{
					ImGui::SetClipboardText(path.c_str());
					state.status = "Copied " + path;
					state.statusSuccess = true;
				}
				// Named rather than "Modify": it says which script the AI
				// prompt at the bottom is about to rewrite.
				if (ImGui::MenuItem("Ask AI to modify this"))
				{
					loadScriptIntoBuffer(state, projectRoot, path);
					state.selectedIndex = static_cast<int>(index);
					state.createMode = false;
				}
				ImGui::Separator();
				if (selected != nullptr)
				{
					ImGui::TextDisabled("target: %s", selected->name.c_str());
				}
				else
				{
					ImGui::TextColored(ImVec4(0.95F, 0.75F, 0.35F, 1.0F), "no object selected");
				}
				ImGui::EndPopup();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip(
					"%s%s\nRight-click for Send to Object.",
					path.c_str(),
					attached ? "\n(attached to the selected object)" : "");
			}
			ImGui::PopID();
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
				state, providerClient, activeProviderId, entityName, target, state.createMode);
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
