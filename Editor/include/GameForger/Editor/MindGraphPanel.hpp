#pragma once

#include <string>
#include <vector>

namespace gameforger::editor
{
	// Mind Graph - the visual node-scripting canvas (docs/MindGraph-Plan.md).
	//
	// PHASE 0 ONLY. This is the dependency de-risk slice and nothing more: it
	// proves imgui-node-editor renders a canvas, draws nodes with pins, and
	// lets a link be dragged between them inside this editor's own ImGui
	// context and docking setup. It compiles no graph and saves no file.
	//
	// The real data model (GraphData.hpp) and compiler land in Phases 1-2,
	// which deliberately have no UI at all so they can be fully tested before
	// the canvas work depends on them.
	struct MindGraphPanelState
	{
		bool panelOpen = false;

		// Phase 0 placeholder graph. Two nodes and whatever links the user
		// drags between them, held in memory for the session only.
		//
		// Ids are ints here purely because imgui-node-editor addresses pins
		// and nodes by integer handle. That is NOT the persistence model:
		// section 13 of the plan requires links to serialize as
		// (nodeId, pinStringId) pairs, because ints assigned at load shift
		// when a node type's pin list changes and silently rebind links.
		struct Link
		{
			int id = 0;
			int fromPin = 0;
			int toPin = 0;
		};
		std::vector<Link> links;
		int nextLinkId = 100;

		// Frames drawn since the panel opened. Used only to defer the initial
		// "frame the content" call until node extents are known.
		int framesDrawn = 0;

		// Set once the editor context has been created, so it is created
		// lazily on first draw rather than at startup - the canvas costs
		// nothing until someone opens the panel.
		bool contextCreated = false;
		void* context = nullptr; // ax::NodeEditor::EditorContext*
	};

	// Draws the "Mind Graph" window. Safe to call every frame; does nothing
	// while `panelOpen` is false.
	void drawMindGraphPanel(MindGraphPanelState& state);

	// Releases the node-editor context. Called once on shutdown.
	void shutdownMindGraphPanel(MindGraphPanelState& state) noexcept;
}
