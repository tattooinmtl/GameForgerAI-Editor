#include "GameForger/Editor/MindGraphPanel.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>

#include <imgui.h>
#include <imgui_node_editor.h>

#include "GameForger/Core/ProjectPaths.hpp"
#include "GameForger/Editor/MindGraph/GraphSerializer.hpp"
#include "GameForger/Editor/MindGraph/NodeCatalog.hpp"

namespace ed = ax::NodeEditor;

namespace gameforger::editor
{
	using namespace gameforger::editor::mindgraph;

	namespace
	{
		// Category colour. Readable at a glance is design pillar 1, and colour
		// is what carries it - an Event is never mistaken for an Action.
		ImVec4 categoryColor(const NodeCategory category)
		{
			switch (category)
			{
				case NodeCategory::Event:  return ImVec4(0.95F, 0.45F, 0.35F, 1.0F);
				case NodeCategory::Flow:   return ImVec4(0.60F, 0.70F, 0.95F, 1.0F);
				case NodeCategory::Audio:  return ImVec4(0.45F, 0.85F, 0.60F, 1.0F);
				case NodeCategory::Value:  return ImVec4(0.85F, 0.75F, 0.45F, 1.0F);
				case NodeCategory::Action: break;
			}
			return ImVec4(0.80F, 0.80F, 0.85F, 1.0F);
		}

		ImVec4 pinColor(const PinSpec& pin)
		{
			if (pin.kind == PinKind::Exec)
			{
				return ImVec4(0.95F, 0.95F, 0.95F, 1.0F);
			}
			switch (pin.type)
			{
				case PinType::Bool:       return ImVec4(0.85F, 0.40F, 0.40F, 1.0F);
				case PinType::Float:      return ImVec4(0.55F, 0.85F, 0.55F, 1.0F);
				case PinType::Vec3:       return ImVec4(0.95F, 0.80F, 0.35F, 1.0F);
				case PinType::EntityRef:  return ImVec4(0.45F, 0.75F, 0.95F, 1.0F);
				case PinType::Tag:        return ImVec4(0.75F, 0.55F, 0.95F, 1.0F);
				case PinType::AudioClip:  return ImVec4(0.45F, 0.85F, 0.75F, 1.0F);
				case PinType::ScriptPath: return ImVec4(0.85F, 0.65F, 0.45F, 1.0F);
				case PinType::String:
				case PinType::Flow: break;
			}
			return ImVec4(0.80F, 0.80F, 0.80F, 1.0F);
		}

		// Canvas pin handles are allocated fresh every frame from the node's
		// current pin list, so a node type gaining a pin cannot disturb
		// anything: the handles never outlive the frame and never reach disk.
		int handleFor(
			MindGraphPanelState& state, const int nodeId, const std::string& pinId)
		{
			for (std::size_t index = 0; index < state.pinHandleToPin.size(); ++index)
			{
				if (state.pinHandleToPin[index].first == nodeId
					&& state.pinHandleToPin[index].second == pinId)
				{
					return static_cast<int>(index) + 1;
				}
			}
			state.pinHandleToPin.emplace_back(nodeId, pinId);
			return static_cast<int>(state.pinHandleToPin.size());
		}

		bool resolveHandle(
			const MindGraphPanelState& state, const int handle, int& outNode, std::string& outPin)
		{
			const std::size_t index = static_cast<std::size_t>(handle) - 1;
			if (handle <= 0 || index >= state.pinHandleToPin.size())
			{
				return false;
			}
			outNode = state.pinHandleToPin[index].first;
			outPin = state.pinHandleToPin[index].second;
			return true;
		}

		// SECTION 14, the literal contract. Scene-bound literals store a NAME
		// or a project-relative PATH, never a GUID - this engine identifies
		// entities by name and has no entity GUID at all. An unresolved value
		// is shown as VISIBLY BROKEN rather than as a silently empty dropdown,
		// which is the part that actually matters.
		bool literalResolves(
			const EditorScene& scene, const PinSpec& pin, const std::string& value)
		{
			if (value.empty())
			{
				return true; // not set yet is not the same as broken
			}
			switch (pin.type)
			{
				case PinType::EntityRef:
					return scene.findEntity(value) != nullptr;
				case PinType::Tag:
					for (const SceneEntity& entity : scene.entities())
					{
						if (std::find(entity.tags.begin(), entity.tags.end(), value) != entity.tags.end())
						{
							return true;
						}
					}
					return false;
				default:
					return true; // paths are validated on compile, not per frame
			}
		}

		void drawNodeBody(
			MindGraphPanelState& state, const EditorScene& scene, GraphNode& node, const NodeType& type)
		{
			const ImVec4 accent = categoryColor(type.category);
			ed::BeginNode(node.id);

			ImGui::PushStyleColor(ImGuiCol_Text, accent);
			ImGui::TextUnformatted(type.displayName.c_str());
			ImGui::PopStyleColor();
			ImGui::Dummy(ImVec2(150.0F, 2.0F));

			const std::size_t rows = std::max(type.inputs.size(), type.outputs.size());
			for (std::size_t row = 0; row < rows; ++row)
			{
				if (row < type.inputs.size())
				{
					const PinSpec& pin = type.inputs[row];
					ed::BeginPin(handleFor(state, node.id, pin.id), ed::PinKind::Input);
					ImGui::PushStyleColor(ImGuiCol_Text, pinColor(pin));
					ImGui::Text("%s %s", pin.kind == PinKind::Exec ? "|>" : "o", pin.displayName.c_str());
					ImGui::PopStyleColor();
					ed::EndPin();
				}
				else
				{
					ImGui::Dummy(ImVec2(60.0F, 0.0F));
				}

				if (row < type.outputs.size())
				{
					ImGui::SameLine();
					ImGui::Dummy(ImVec2(30.0F, 0.0F));
					ImGui::SameLine();
					const PinSpec& pin = type.outputs[row];
					ed::BeginPin(handleFor(state, node.id, pin.id), ed::PinKind::Output);
					ImGui::PushStyleColor(ImGuiCol_Text, pinColor(pin));
					ImGui::Text("%s %s", pin.displayName.c_str(), pin.kind == PinKind::Exec ? "|>" : "o");
					ImGui::PopStyleColor();
					ed::EndPin();
				}
			}

			// Literals inline in the body, so a node reads as a complete
			// statement without having to select it and look elsewhere.
			for (const PinSpec& pin : type.literals)
			{
				const std::string value = node.literals.count(pin.id) ? node.literals.at(pin.id) : std::string();
				const bool broken = !literalResolves(scene, pin, value);
				if (broken)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95F, 0.35F, 0.30F, 1.0F));
					ImGui::Text("! %s: missing \"%s\"", pin.displayName.c_str(), value.c_str());
					ImGui::PopStyleColor();
				}
				else
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65F, 0.65F, 0.70F, 1.0F));
					ImGui::Text("%s: %s", pin.displayName.c_str(), value.empty() ? "-" : value.c_str());
					ImGui::PopStyleColor();
				}
			}

			ed::EndNode();
		}

		// Palette entry -> a new node at the canvas centre.
		void addNode(MindGraphPanelState& state, const NodeType& type)
		{
			GraphNode node;
			node.id = state.graph.nextNodeId++;
			node.type = type.id;
			const ImVec2 canvasCentre = ed::ScreenToCanvas(ImGui::GetWindowPos());
			node.canvasPosition = glm::vec2(canvasCentre.x + 60.0F, canvasCentre.y + 60.0F);
			state.graph.nodes.push_back(node);
			state.selectedNodeId = node.id;
			state.pendingFocusNodeId = node.id;
			state.dirty = true;
		}

		void drawPalette(MindGraphPanelState& state)
		{
			ImGui::TextUnformatted("Palette");
			ImGui::SetNextItemWidth(-1.0F);
			ImGui::InputTextWithHint(
				"##paletteFilter", "Search nodes...", state.paletteFilter.data(), state.paletteFilter.size());
			const std::string filter = state.paletteFilter.data();

			const auto matches = [&filter](const NodeType& type)
			{
				if (filter.empty())
				{
					return true;
				}
				std::string haystack = type.displayName + " " + type.summary + " " + type.id;
				std::string needle = filter;
				std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);
				std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
				return haystack.find(needle) != std::string::npos;
			};

			ImGui::Separator();
			for (const NodeCategory category :
				 {NodeCategory::Event, NodeCategory::Flow, NodeCategory::Action, NodeCategory::Audio,
				  NodeCategory::Value})
			{
				bool headerDrawn = false;
				for (const NodeType& type : nodeCatalog())
				{
					if (type.category != category || !matches(type))
					{
						continue;
					}
					if (!headerDrawn)
					{
						ImGui::PushStyleColor(ImGuiCol_Text, categoryColor(category));
						ImGui::TextUnformatted(nodeCategoryName(category));
						ImGui::PopStyleColor();
						headerDrawn = true;
					}
					ImGui::PushID(type.id.c_str());
					if (ImGui::Button(type.displayName.c_str(), ImVec2(-1.0F, 0.0F)))
					{
						addNode(state, type);
					}
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s", type.summary.c_str());
					}
					ImGui::PopID();
				}
				if (headerDrawn)
				{
					ImGui::Spacing();
				}
			}
		}

		// The Details strip. Every scene-bound literal is a dropdown of what
		// actually exists, per design pillar 3 - no typing names and hoping.
		void drawDetails(MindGraphPanelState& state, const EditorScene& scene)
		{
			ImGui::TextUnformatted("Details");
			ImGui::Separator();

			GraphNode* node = state.graph.findNode(state.selectedNodeId);
			if (node == nullptr)
			{
				ImGui::TextDisabled("Select a node to edit its values.");
				return;
			}
			const NodeType* type = findNodeType(node->type);
			if (type == nullptr)
			{
				ImGui::TextColored(
					ImVec4(0.95F, 0.75F, 0.35F, 1.0F), "Unknown node type \"%s\".", node->type.c_str());
				ImGui::TextWrapped(
					"This graph was probably saved by a newer editor. The node is kept exactly as it was "
					"and will survive a save - it just cannot be edited or compiled here.");
				return;
			}

			ImGui::PushStyleColor(ImGuiCol_Text, categoryColor(type->category));
			ImGui::TextUnformatted(type->displayName.c_str());
			ImGui::PopStyleColor();
			ImGui::TextWrapped("%s", type->summary.c_str());
			ImGui::Separator();

			for (const PinSpec& pin : type->literals)
			{
				std::string value = node->literals.count(pin.id) ? node->literals.at(pin.id) : std::string();
				ImGui::PushID(pin.id.c_str());

				if (pin.type == PinType::EntityRef || pin.type == PinType::Tag)
				{
					// Gather what the scene actually offers. Rebuilt each
					// frame rather than cached: the whole point of section 14
					// is that a renamed or deleted object shows up here
					// immediately rather than lingering as a stale entry.
					std::vector<std::string> choices;
					if (pin.type == PinType::EntityRef)
					{
						for (const SceneEntity& entity : scene.entities())
						{
							choices.push_back(entity.name);
						}
					}
					else
					{
						for (const SceneEntity& entity : scene.entities())
						{
							for (const std::string& tag : entity.tags)
							{
								if (std::find(choices.begin(), choices.end(), tag) == choices.end())
								{
									choices.push_back(tag);
								}
							}
						}
					}
					std::sort(choices.begin(), choices.end());

					const bool broken = !literalResolves(scene, pin, value);
					if (broken)
					{
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95F, 0.35F, 0.30F, 1.0F));
					}
					const std::string preview = value.empty() ? "(none)" : value;
					if (ImGui::BeginCombo(pin.displayName.c_str(), preview.c_str()))
					{
						if (ImGui::Selectable("(none)", value.empty()))
						{
							node->literals.erase(pin.id);
							state.dirty = true;
						}
						for (const std::string& choice : choices)
						{
							if (ImGui::Selectable(choice.c_str(), choice == value))
							{
								node->literals[pin.id] = choice;
								state.dirty = true;
							}
						}
						ImGui::EndCombo();
					}
					if (broken)
					{
						ImGui::PopStyleColor();
						// Named, not blanked. Silently clearing it would
						// destroy the author's intent along with the error.
						ImGui::TextColored(
							ImVec4(0.95F, 0.35F, 0.30F, 1.0F),
							"\"%s\" is not in this scene any more.", value.c_str());
					}
				}
				else if (pin.type == PinType::Bool)
				{
					bool flag = value == "true";
					if (ImGui::Checkbox(pin.displayName.c_str(), &flag))
					{
						node->literals[pin.id] = flag ? "true" : "false";
						state.dirty = true;
					}
				}
				else if (pin.type == PinType::Float)
				{
					float number = 0.0F;
					try
					{
						number = value.empty() ? 0.0F : std::stof(value);
					}
					catch (...)
					{
						number = 0.0F;
					}
					if (ImGui::DragFloat(pin.displayName.c_str(), &number, 0.05F))
					{
						std::array<char, 32> buffer{};
						std::snprintf(buffer.data(), buffer.size(), "%.4f", number);
						node->literals[pin.id] = buffer.data();
						state.dirty = true;
					}
				}
				else
				{
					std::array<char, 256> buffer{};
					std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
					if (ImGui::InputText(pin.displayName.c_str(), buffer.data(), buffer.size()))
					{
						node->literals[pin.id] = buffer.data();
						state.dirty = true;
					}
				}
				ImGui::PopID();
			}

			ImGui::Separator();
			if (ImGui::Button("Delete Node", ImVec2(-1.0F, 0.0F)))
			{
				const int deletedId = node->id;
				state.graph.nodes.erase(
					std::remove_if(
						state.graph.nodes.begin(), state.graph.nodes.end(),
						[deletedId](const GraphNode& candidate) { return candidate.id == deletedId; }),
					state.graph.nodes.end());
				// Links to a deleted node go with it. This is the one place
				// dropping a link is right: the user asked for the node to go,
				// so its wires are not lost work.
				state.graph.links.erase(
					std::remove_if(
						state.graph.links.begin(), state.graph.links.end(),
						[deletedId](const GraphLink& link)
						{ return link.fromNode == deletedId || link.toNode == deletedId; }),
					state.graph.links.end());
				state.selectedNodeId = 0;
				state.dirty = true;
			}
		}

		void compileAndWrite(
			MindGraphPanelState& state, const std::filesystem::path& projectRoot, const MindGraphLogFn& log)
		{
			const CompileResult result = compileGraph(state.graph);
			state.diagnostics = result.diagnostics;
			state.compileSucceeded = result.success;

			if (!result.success)
			{
				state.compileStatus = "Compile failed - see the list below.";
				log(false, "Mind Graph: compile failed.");
				return;
			}

			const std::string name = state.graph.name.empty() ? std::string("MindGraph") : state.graph.name;
			const std::filesystem::path outDir = projectRoot / "Game" / "Scripts" / "generated";
			std::error_code error;
			std::filesystem::create_directories(outDir, error);
			const std::filesystem::path outPath = outDir / (name + ".lua");

			std::ofstream out(outPath, std::ios::binary);
			if (!out)
			{
				state.compileStatus = "Could not write " + outPath.string();
				state.compileSucceeded = false;
				log(false, state.compileStatus);
				return;
			}
			out << result.lua;
			state.compileStatus = "Compiled to Game/Scripts/generated/" + name + ".lua";
			log(true, "Mind Graph: " + state.compileStatus);
		}
	}

	void drawMindGraphPanel(
		MindGraphPanelState& state,
		const EditorScene& scene,
		const std::filesystem::path& projectRoot,
		const MindGraphLogFn& log)
	{
		if (!state.panelOpen)
		{
			return;
		}
		if (!ImGui::Begin("Mind Graph", &state.panelOpen))
		{
			ImGui::End();
			return;
		}

		if (!state.contextCreated)
		{
			// Created lazily, so the canvas costs nothing until the panel is
			// opened for the first time.
			ed::Config config;
			config.SettingsFile = nullptr; // node positions live in the .gfgraph, not a side file
			state.context = ed::CreateEditor(&config);
			state.contextCreated = true;
		}

		// ---- toolbar ----
		if (ImGui::Button("New"))
		{
			state.graph = MindGraph{};
			state.graph.name = "Untitled";
			state.graphPath.clear();
			state.diagnostics.clear();
			state.compileStatus.clear();
			state.selectedNodeId = 0;
			state.dirty = false;
			state.framesDrawn = 0; // re-fit the view to the new graph
		}
		ImGui::SameLine();
		if (ImGui::Button("Save"))
		{
			const std::string name = state.graph.name.empty() ? std::string("Untitled") : state.graph.name;
			const std::filesystem::path path = state.graphPath.empty()
				? projectRoot / "Game" / "Graphs" / (name + ".gfgraph")
				: state.graphPath;
			const GraphSaveResult saved = saveGraph(path, state.graph);
			log(saved.success, "Mind Graph: " + saved.message);
			if (saved.success)
			{
				state.graphPath = path;
				state.dirty = false;
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Fit View"))
		{
			state.framesDrawn = 1; // triggers the fit on the next frame
		}
		ImGui::SameLine();
		if (ImGui::Button("Compile"))
		{
			compileAndWrite(state, projectRoot, log);
		}
		ImGui::SameLine();
		{
			// Pillar 2: never a dead end. The generated Lua is always one
			// click away, and it is plain readable text.
			const std::string name = state.graph.name.empty() ? std::string("MindGraph") : state.graph.name;
			const std::filesystem::path generated =
				projectRoot / "Game" / "Scripts" / "generated" / (name + ".lua");
			const bool exists = std::filesystem::exists(generated);
			if (!exists)
			{
				ImGui::BeginDisabled();
			}
			if (ImGui::Button("Open Generated Lua"))
			{
				log(true, "Generated Lua: " + generated.string());
			}
			if (!exists)
			{
				ImGui::EndDisabled();
			}
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%s%s", state.graph.name.c_str(), state.dirty ? " *" : "");

		ImGui::Separator();

		const float paletteWidth = 190.0F;
		const float detailsWidth = 260.0F;
		const float bottomHeight = 120.0F;
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const float canvasHeight = std::max(available.y - bottomHeight, 120.0F);

		ImGui::BeginChild("##palette", ImVec2(paletteWidth, canvasHeight), true);
		drawPalette(state);
		ImGui::EndChild();

		ImGui::SameLine();

		// ---- canvas ----
		ImGui::BeginChild(
			"##canvas", ImVec2(available.x - paletteWidth - detailsWidth - 16.0F, canvasHeight), false);
		ed::SetCurrentEditor(static_cast<ed::EditorContext*>(state.context));
		ed::Begin("MindGraphCanvas");

		// Handles are rebuilt every frame from the current pin lists, so they
		// cannot go stale and never reach disk (section 13).
		state.pinHandleToPin.clear();

		for (GraphNode& node : state.graph.nodes)
		{
			const NodeType* type = findNodeType(node.type);
			if (type == nullptr)
			{
				// Unknown types still draw, so a graph from a newer editor is
				// visibly intact rather than appearing to have lost nodes.
				ed::BeginNode(node.id);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95F, 0.75F, 0.35F, 1.0F));
				ImGui::Text("? %s", node.type.c_str());
				ImGui::PopStyleColor();
				ImGui::TextDisabled("unknown node type");
				ed::EndNode();
				continue;
			}
			ed::SetNodePosition(node.id, ImVec2(node.canvasPosition.x, node.canvasPosition.y));
			drawNodeBody(state, scene, node, *type);
		}

		for (const GraphLink& link : state.graph.links)
		{
			const int fromHandle = handleFor(state, link.fromNode, link.fromPin);
			const int toHandle = handleFor(state, link.toNode, link.toPin);
			ed::Link(link.id, fromHandle, toHandle);
		}

		// ---- link creation ----
		if (ed::BeginCreate())
		{
			ed::PinId startPin;
			ed::PinId endPin;
			if (ed::QueryNewLink(&startPin, &endPin) && startPin && endPin)
			{
				int fromNode = 0;
				int toNode = 0;
				std::string fromPin;
				std::string toPin;
				const bool resolved =
					resolveHandle(state, static_cast<int>(startPin.Get()), fromNode, fromPin)
					&& resolveHandle(state, static_cast<int>(endPin.Get()), toNode, toPin);
				// Refuse a self-link outright: it is always a mis-drag, and
				// allowing it would create a cycle the compiler then reports.
				if (resolved && fromNode != toNode && ed::AcceptNewItem())
				{
					GraphLink link;
					link.id = state.graph.nextLinkId++;
					link.fromNode = fromNode;
					link.fromPin = fromPin;
					link.toNode = toNode;
					link.toPin = toPin;
					state.graph.links.push_back(link);
					state.dirty = true;
				}
				else if (!resolved || fromNode == toNode)
				{
					ed::RejectNewItem();
				}
			}
		}
		ed::EndCreate();

		if (ed::BeginDelete())
		{
			ed::LinkId deletedLink;
			while (ed::QueryDeletedLink(&deletedLink))
			{
				if (ed::AcceptDeletedItem())
				{
					const int id = static_cast<int>(deletedLink.Get());
					state.graph.links.erase(
						std::remove_if(
							state.graph.links.begin(), state.graph.links.end(),
							[id](const GraphLink& link) { return link.id == id; }),
						state.graph.links.end());
					state.dirty = true;
				}
			}
		}
		ed::EndDelete();

		// Positions are authored by dragging, so read them back every frame -
		// they belong in the .gfgraph, not in a node-editor side file.
		for (GraphNode& node : state.graph.nodes)
		{
			const ImVec2 position = ed::GetNodePosition(node.id);
			if (std::abs(position.x - node.canvasPosition.x) > 0.01F
				|| std::abs(position.y - node.canvasPosition.y) > 0.01F)
			{
				node.canvasPosition = glm::vec2(position.x, position.y);
				state.dirty = true;
			}
		}

		if (ed::HasSelectionChanged())
		{
			std::array<ed::NodeId, 1> selected{};
			if (ed::GetSelectedNodes(selected.data(), 1) > 0)
			{
				state.selectedNodeId = static_cast<int>(selected[0].Get());
			}
		}
		if (state.pendingFocusNodeId != 0)
		{
			ed::SelectNode(state.pendingFocusNodeId);
			state.pendingFocusNodeId = 0;
		}

		// Fit the view to the graph once, after nodes have been submitted at
		// least once so their extents are known. Without this the canvas opens
		// at whatever zoom the context defaults to, which on a fresh graph
		// means a single node filling the whole pane.
		++state.framesDrawn;
		if (state.framesDrawn == 2 && !state.graph.nodes.empty())
		{
			ed::NavigateToContent(0.0F);
		}

		ed::End();
		ed::SetCurrentEditor(nullptr);
		ImGui::EndChild();

		ImGui::SameLine();
		ImGui::BeginChild("##details", ImVec2(detailsWidth, canvasHeight), true);
		drawDetails(state, scene);
		ImGui::EndChild();

		// ---- compile bar ----
		ImGui::BeginChild("##diagnostics", ImVec2(0.0F, 0.0F), true);
		if (state.compileStatus.empty())
		{
			ImGui::TextDisabled("Not compiled yet. Press Compile to generate the Lua this graph runs as.");
		}
		else
		{
			ImGui::TextColored(
				state.compileSucceeded ? ImVec4(0.55F, 0.90F, 0.55F, 1.0F)
									   : ImVec4(0.95F, 0.45F, 0.40F, 1.0F),
				"%s", state.compileStatus.c_str());
		}
		for (const CompileDiagnostic& diagnostic : state.diagnostics)
		{
			ImGui::PushID(&diagnostic);
			ImGui::TextColored(
				diagnostic.isError ? ImVec4(0.95F, 0.45F, 0.40F, 1.0F) : ImVec4(0.95F, 0.80F, 0.40F, 1.0F),
				"%s %s", diagnostic.isError ? "[error]" : "[warn] ", diagnostic.message.c_str());
			// Double-click selects the offending node, so an error is one
			// click from the thing that caused it.
			if (diagnostic.nodeId != 0 && ImGui::IsItemHovered()
				&& ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				state.selectedNodeId = diagnostic.nodeId;
				state.pendingFocusNodeId = diagnostic.nodeId;
			}
			ImGui::PopID();
		}
		ImGui::EndChild();

		ImGui::End();
	}

	void shutdownMindGraphPanel(MindGraphPanelState& state) noexcept
	{
		if (state.contextCreated && state.context != nullptr)
		{
			ed::DestroyEditor(static_cast<ed::EditorContext*>(state.context));
			state.context = nullptr;
			state.contextCreated = false;
		}
	}
}
