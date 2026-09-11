#pragma once

#include <string>
#include <vector>

#include "GameForger/Editor/MindGraph/GraphData.hpp"

namespace gameforger::editor::mindgraph
{
	// One problem found while compiling. `nodeId` is 0 when the problem is not
	// attributable to a single node, so the panel can select the offending
	// node when it is.
	struct CompileDiagnostic
	{
		int nodeId = 0;
		std::string message;
		bool isError = true; // false = warning; the graph still compiles
	};

	struct CompileResult
	{
		bool success = false;
		// Generated Lua. Readable and openable on purpose - design pillar 2,
		// "never a dead end": a graph must never become a black box you cannot
		// get out of.
		std::string lua;
		std::vector<CompileDiagnostic> diagnostics;
	};

	// Compiles `graph` into a Lua module matching this project's existing
	// script contract (`return Table` with on_start / on_update).
	//
	// `emitNodeBreadcrumbs` wraps every emitted node in a __gfNode(id) call so
	// the panel can highlight whatever is executing. Per MindGraph-Plan
	// section 15 that wrapping is done HERE, once, as a compiler pass - never
	// as a line each node emitter has to remember, because twelve edit points
	// that must stay in sync is twelve chances to drift, and the drift would
	// be invisible until one node mysteriously never lit up.
	[[nodiscard]] CompileResult compileGraph(const MindGraph& graph, bool emitNodeBreadcrumbs = true);
}
