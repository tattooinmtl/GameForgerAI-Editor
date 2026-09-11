#include "GameForger/Editor/MindGraphPanel.hpp"

#include <algorithm>

#include <imgui.h>
#include <imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace gameforger::editor
{
	namespace
	{
		// Pin ids must be unique across the whole canvas, and imgui-node-editor
		// addresses them as plain integers. Deriving them from the node id
		// keeps them unique without a registry, which is all Phase 0 needs.
		//
		// This is deliberately NOT how they will be persisted - see
		// MindGraph-Plan section 13. On disk a link is (nodeId, pinStringId),
		// because an integer index assigned at load shifts the moment a node
		// type gains a pin, silently rebinding every later link.
		constexpr int inputPinId(const int nodeId) { return nodeId * 10 + 1; }
		constexpr int outputPinId(const int nodeId) { return nodeId * 10 + 2; }

		void drawPlaceholderNode(const int nodeId, const char* title, const ImVec4& accent)
		{
			ed::BeginNode(nodeId);
			ImGui::PushStyleColor(ImGuiCol_Text, accent);
			ImGui::TextUnformatted(title);
			ImGui::PopStyleColor();
			ImGui::Dummy(ImVec2(140.0F, 4.0F));

			ed::BeginPin(inputPinId(nodeId), ed::PinKind::Input);
			ImGui::TextUnformatted("-> In");
			ed::EndPin();

			ImGui::SameLine();
			ImGui::Dummy(ImVec2(40.0F, 0.0F));
			ImGui::SameLine();

			ed::BeginPin(outputPinId(nodeId), ed::PinKind::Output);
			ImGui::TextUnformatted("Out ->");
			ed::EndPin();

			ed::EndNode();
		}
	}

	void drawMindGraphPanel(MindGraphPanelState& state)
	{
		if (!state.panelOpen)
		{
			return;
		}

		// A graph needs the big pane. The plan has this docking centre beside
		// Viewport and Game; until that layout change lands it at least opens
		// large enough to be usable rather than as a sliver.
		ImGui::SetNextWindowSize(ImVec2(980.0F, 620.0F), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Mind Graph", &state.panelOpen))
		{
			ImGui::End();
			return;
		}

		if (!state.contextCreated)
		{
			// Created lazily: the canvas costs nothing until the panel is
			// opened, and creating it at startup would make every launch pay
			// for a feature most sessions never touch.
			ed::Config config;
			// No settings file. Node positions belong in the .gfgraph once
			// that exists (Phase 1); letting the library write its own
			// sidecar .json would create a second, competing source of truth
			// for the same data.
			config.SettingsFile = nullptr;
			state.context = ed::CreateEditor(&config);
			state.contextCreated = true;
		}

		ImGui::TextColored(
			ImVec4(1.0F, 0.78F, 0.30F, 1.0F), "Phase 0 - dependency de-risk slice");
		ImGui::TextDisabled(
			"Proves the canvas renders and links can be dragged. No graph model, no compiler, "
			"nothing saved yet - those are Phases 1 and 2, which are built and tested without UI "
			"first. See docs/MindGraph-Plan.md.");
		ImGui::Separator();

		ed::SetCurrentEditor(static_cast<ed::EditorContext*>(state.context));
		ed::Begin("MindGraphCanvas", ImVec2(0.0F, 0.0F));

		drawPlaceholderNode(1, "On Game Start", ImVec4(0.45F, 0.85F, 0.45F, 1.0F));
		drawPlaceholderNode(2, "Play Audio", ImVec4(0.50F, 0.75F, 1.0F, 1.0F));

		// Frame the content once the nodes exist. Without this the view sits
		// at canvas origin and whatever the graph contains can be off-screen -
		// which looks exactly like "the canvas is empty and broken".
		// Deferred to the second frame because node extents are not known
		// until they have been laid out once.
		if (state.framesDrawn == 1)
		{
			ed::NavigateToContent(0.0F);
		}
		++state.framesDrawn;

		for (const MindGraphPanelState::Link& link : state.links)
		{
			ed::Link(link.id, link.fromPin, link.toPin);
		}

		// Link creation. Accept() only commits once the user releases, so a
		// drag that ends in empty space leaves the graph untouched.
		if (ed::BeginCreate())
		{
			ed::PinId startPin;
			ed::PinId endPin;
			if (ed::QueryNewLink(&startPin, &endPin) && startPin && endPin)
			{
				const int from = static_cast<int>(startPin.Get());
				const int to = static_cast<int>(endPin.Get());
				// Reject a pin linked to itself. Real validation - direction,
				// type compatibility, cycles - belongs to the catalog in
				// Phase 2, not here.
				if (from == to)
				{
					ed::RejectNewItem(ImColor(255, 80, 80), 2.0F);
				}
				else if (ed::AcceptNewItem())
				{
					state.links.push_back({state.nextLinkId++, from, to});
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
					state.links.erase(
						std::remove_if(
							state.links.begin(), state.links.end(),
							[id](const MindGraphPanelState::Link& link) { return link.id == id; }),
						state.links.end());
				}
			}
		}
		ed::EndDelete();

		ed::End();
		ed::SetCurrentEditor(nullptr);

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
