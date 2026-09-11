#pragma once

#include <string>
#include <vector>

#include "GameForger/Editor/MindGraph/GraphData.hpp"

namespace gameforger::editor::mindgraph
{
	// What a node is FOR. Drives its colour on the canvas, and the grouping in
	// the palette. Readable at a glance is design pillar 1.
	enum class NodeCategory
	{
		Event,   // starts an execution chain
		Flow,    // branches, sequences, waits
		Action,  // does something to the world
		Audio,
		Value    // produces data, no execution pin
	};

	[[nodiscard]] const char* nodeCategoryName(NodeCategory category) noexcept;

	// The published contract for one node type. Pins are a STATIC list owned
	// here, not stored per placed node - which is why a node type gaining a
	// pin does not require rewriting a single saved graph.
	//
	// `id` and every PinSpec::id are permanent once shipped. Display names are
	// free to change.
	struct NodeType
	{
		std::string id;
		std::string displayName;
		std::string summary; // one line, shown in the palette and as a tooltip
		NodeCategory category = NodeCategory::Action;
		std::vector<PinSpec> inputs;
		std::vector<PinSpec> outputs;
		// Literal pins the node edits inline in its own body rather than
		// receiving down a wire - the dropdowns. Each is a PinSpec whose id
		// keys GraphNode::literals.
		std::vector<PinSpec> literals;
	};

	// Every node type the compiler knows how to emit. Stable order, so the
	// palette does not reshuffle between runs.
	[[nodiscard]] const std::vector<NodeType>& nodeCatalog();

	// Null when `typeId` is not in the catalog - which is a graph authored by
	// a newer editor, and must be preserved rather than discarded.
	[[nodiscard]] const NodeType* findNodeType(const std::string& typeId);

	// Null when the type or the pin is unknown. Used to tell a link pointing
	// at a pin that no longer exists (broken, report it) from one pointing at
	// a pin that does (fine).
	[[nodiscard]] const PinSpec* findPin(const NodeType& type, const std::string& pinId);
}
