#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "GameForger/Editor/EditorScene.hpp"
#include "GameForger/Editor/MindGraph/GraphCompiler.hpp"
#include "GameForger/Editor/MindGraph/GraphData.hpp"

namespace gameforger::editor
{
	using MindGraphLogFn = std::function<void(bool success, const std::string& message)>;

	// Mind Graph - the visual node-scripting canvas (docs/MindGraph-Plan.md).
	//
	// The graph is the authored artefact (`.gfgraph`); compiling it writes a
	// readable Lua module under Game/Scripts/generated/ that runs on the
	// existing ScriptRuntime in BOTH hosts. There is no second execution
	// engine - that is decision D9, and the reason is the Editor/Runtime seam
	// that produced every §1b defect.
	struct MindGraphPanelState
	{
		// Open by default, like the Viewport, Game and Hierarchy panels. It is
		// a headline authoring surface, not an optional tool - defaulting it
		// closed meant it simply was not there on launch, and the only way to
		// find it was a menu nobody would think to look in.
		bool panelOpen = true;

		mindgraph::MindGraph graph;
		std::filesystem::path graphPath; // empty until saved or loaded
		bool dirty = false;

		// Last compile, kept so the error list persists between frames and a
		// double-click on a diagnostic can select the node it names.
		std::vector<mindgraph::CompileDiagnostic> diagnostics;
		std::string compileStatus;
		bool compileSucceeded = false;

		int selectedNodeId = 0;
		std::array<char, 128> paletteFilter{};

		// imgui-node-editor addresses nodes and pins by integer handle, so the
		// canvas maps each (nodeId, pinStringId) to an int for the duration of
		// a frame. Those ints are NEVER persisted - section 13 requires links
		// on disk to be (nodeId, pinStringId), because an integer assigned at
		// load shifts the moment a node type gains a pin and silently rebinds
		// every later link.
		std::vector<std::pair<int, std::string>> pinHandleToPin; // index+1 == handle
		int pendingFocusNodeId = 0;

		// Frames drawn since the panel opened. The canvas cannot be framed on
		// frame 1 - node extents are not known until they have been submitted
		// once - so the initial "fit the content" call is deferred.
		int framesDrawn = 0;

		bool contextCreated = false;
		void* context = nullptr; // ax::NodeEditor::EditorContext*
	};

	// Draws the "Mind Graph" window. Safe to call every frame; does nothing
	// while `panelOpen` is false.
	//
	// `scene` populates the scene-bound dropdowns - objects, tags and lights
	// that actually exist - which is design pillar 3 ("the scene is the
	// vocabulary"): no typing names and hoping.
	void drawMindGraphPanel(
		MindGraphPanelState& state,
		const EditorScene& scene,
		const std::filesystem::path& projectRoot,
		const MindGraphLogFn& log);

	// Releases the node-editor context. Called once on shutdown.
	void shutdownMindGraphPanel(MindGraphPanelState& state) noexcept;
}
