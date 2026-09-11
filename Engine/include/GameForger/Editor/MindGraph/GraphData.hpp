#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <glm/vec2.hpp>

namespace gameforger::editor::mindgraph
{
	// What flows along a wire. Exec is the thick white execution wire that
	// says "then do this"; every other kind is a thin data wire.
	enum class PinKind
	{
		Exec,
		Data
	};

	// Data pin types. EntityRef / Tag / AudioClip / ScriptPath are the ones
	// bound to the live scene, and they follow the literal contract in
	// MindGraph-Plan section 14: they store a NAME or a project-relative
	// PATH, never a GUID, because this engine identifies entities by name
	// (EditorScene.hpp - the save format carries no entity id).
	enum class PinType
	{
		Flow,
		Bool,
		Float,
		Vec3,
		String,
		EntityRef,
		Tag,
		AudioClip,
		ScriptPath
	};

	[[nodiscard]] const char* pinTypeName(PinType type) noexcept;
	[[nodiscard]] bool pinTypeFromName(const std::string& text, PinType& outType) noexcept;

	// One pin on a node type. `id` is a STABLE STRING that is part of the node
	// type's published contract and must never change once shipped - display
	// names may change freely, ids may not.
	//
	// This is the load-bearing invariant of the whole system (plan section
	// 13). Links are stored by (nodeId, pinId-string), so adding, removing or
	// reordering a node type's pins cannot silently rebind an existing link.
	// Integer pin handles exist only in the canvas, assigned per frame for
	// hit-testing, and are never written to disk.
	struct PinSpec
	{
		std::string id;
		std::string displayName;
		PinKind kind = PinKind::Data;
		PinType type = PinType::Flow;
	};

	// A placed node. `type` names an entry in the node catalog; the catalog
	// owns the pin list, so a graph file stores no pin definitions of its own
	// and a node type gaining a pin does not require rewriting saved graphs.
	//
	// `literals` are the inline values chosen in the node's own body - the
	// dropdown selections and typed constants - keyed by pin id.
	struct GraphNode
	{
		int id = 0;
		std::string type;
		glm::vec2 canvasPosition{0.0F, 0.0F};
		std::unordered_map<std::string, std::string> literals;

		// Set when this node's `type` is not in the catalog - a graph saved by
		// a newer editor, or one whose node type was renamed. The node and all
		// its data are preserved verbatim so that opening and saving such a
		// graph does not quietly delete work. Never true for a graph the
		// running editor authored.
		bool unknownType = false;
	};

	// A wire. Endpoints are (node id, pin id STRING) pairs - see PinSpec.
	struct GraphLink
	{
		int id = 0;
		int fromNode = 0;
		std::string fromPin;
		int toNode = 0;
		std::string toPin;
	};

	struct MindGraph
	{
		std::string name;
		std::vector<GraphNode> nodes;
		std::vector<GraphLink> links;
		int nextNodeId = 1;
		int nextLinkId = 1;

		[[nodiscard]] const GraphNode* findNode(int id) const noexcept;
		[[nodiscard]] GraphNode* findNode(int id) noexcept;

		// Links whose endpoints no longer resolve to a real node, or (given a
		// catalog) to a pin that still exists on that node's type. A broken
		// link is reported, never silently dropped: dropping it would destroy
		// authored work on the first load after an engine update.
		[[nodiscard]] std::vector<const GraphLink*> brokenLinks() const;
	};
}
