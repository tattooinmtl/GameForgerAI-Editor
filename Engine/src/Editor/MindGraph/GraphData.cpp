#include "GameForger/Editor/MindGraph/GraphData.hpp"

#include <algorithm>

namespace gameforger::editor::mindgraph
{
	// Names, not ordinals - the same rule SceneSerializer applies to every
	// other enum here. Reordering this enum must never reinterpret a saved
	// graph as a different pin type.
	const char* pinTypeName(const PinType type) noexcept
	{
		switch (type)
		{
			case PinType::Bool:       return "bool";
			case PinType::Float:      return "float";
			case PinType::Vec3:       return "vec3";
			case PinType::String:     return "string";
			case PinType::EntityRef:  return "entity";
			case PinType::Tag:        return "tag";
			case PinType::AudioClip:  return "audio_clip";
			case PinType::ScriptPath: return "script_path";
			case PinType::Flow: break;
		}
		return "flow";
	}

	bool pinTypeFromName(const std::string& text, PinType& outType) noexcept
	{
		if (text == "flow")        { outType = PinType::Flow;       return true; }
		if (text == "bool")        { outType = PinType::Bool;       return true; }
		if (text == "float")       { outType = PinType::Float;      return true; }
		if (text == "vec3")        { outType = PinType::Vec3;       return true; }
		if (text == "string")      { outType = PinType::String;     return true; }
		if (text == "entity")      { outType = PinType::EntityRef;  return true; }
		if (text == "tag")         { outType = PinType::Tag;        return true; }
		if (text == "audio_clip")  { outType = PinType::AudioClip;  return true; }
		if (text == "script_path") { outType = PinType::ScriptPath; return true; }
		return false;
	}

	const GraphNode* MindGraph::findNode(const int id) const noexcept
	{
		const auto found = std::find_if(
			nodes.begin(), nodes.end(), [id](const GraphNode& node) { return node.id == id; });
		return found == nodes.end() ? nullptr : &*found;
	}

	GraphNode* MindGraph::findNode(const int id) noexcept
	{
		const auto found = std::find_if(
			nodes.begin(), nodes.end(), [id](const GraphNode& node) { return node.id == id; });
		return found == nodes.end() ? nullptr : &*found;
	}

	std::vector<const GraphLink*> MindGraph::brokenLinks() const
	{
		// Node-level only. Pin-level validation needs the catalog, which lives
		// a layer up - this is the check that can be made from the data alone.
		std::vector<const GraphLink*> broken;
		for (const GraphLink& link : links)
		{
			if (findNode(link.fromNode) == nullptr || findNode(link.toNode) == nullptr)
			{
				broken.push_back(&link);
			}
		}
		return broken;
	}
}
