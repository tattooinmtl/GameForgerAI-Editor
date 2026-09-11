#pragma once

#include <filesystem>
#include <string>

#include "GameForger/Editor/MindGraph/GraphData.hpp"

namespace gameforger::editor::mindgraph
{
	struct GraphLoadResult
	{
		bool success = false;
		std::string message;
		MindGraph graph;
	};

	struct GraphSaveResult
	{
		bool success = false;
		std::string message;
	};

	// `.gfgraph` is JSON, written with the same hand-rolled approach
	// SceneSerializer uses rather than a new dependency, and read back through
	// the same `json::` parser.
	//
	// Enums and pin references round-trip by NAME, never by index - the rule
	// this project already applies to PrimitiveType, LightType and every audio
	// enum, applied one level deeper to pins. An integer index written to disk
	// shifts the moment a node type gains a pin, silently rebinding links.
	[[nodiscard]] GraphSaveResult saveGraph(const std::filesystem::path& path, const MindGraph& graph);
	[[nodiscard]] GraphLoadResult loadGraph(const std::filesystem::path& path);

	// Text-level round-trip, exposed for tests so a graph can be checked
	// without touching the filesystem.
	[[nodiscard]] std::string serializeGraph(const MindGraph& graph);
	[[nodiscard]] GraphLoadResult deserializeGraph(const std::string& text);
}
