#include "GameForger/Editor/MindGraph/NodeCatalog.hpp"

#include <algorithm>

namespace gameforger::editor::mindgraph
{
	namespace
	{
		PinSpec exec(std::string id, std::string display)
		{
			return PinSpec{std::move(id), std::move(display), PinKind::Exec, PinType::Flow};
		}

		PinSpec data(std::string id, std::string display, const PinType type)
		{
			return PinSpec{std::move(id), std::move(display), PinKind::Data, type};
		}

		// Built once, on first use. Every id here is permanent: a saved graph
		// names its node type and its pins by these strings, so changing one
		// orphans every graph that used it.
		std::vector<NodeType> buildCatalog()
		{
			std::vector<NodeType> catalog;

			// ---- Events: the entry points an execution chain hangs off ----
			catalog.push_back(NodeType{
				"event.game_start", "On Game Start",
				"Runs once when the game starts.",
				NodeCategory::Event,
				{},
				{exec("exec_out", "Then")},
				{}});

			catalog.push_back(NodeType{
				"event.update", "On Update",
				"Runs every frame. Delta time in seconds is available as Dt.",
				NodeCategory::Event,
				{},
				{exec("exec_out", "Then"), data("dt", "Dt", PinType::Float)},
				{}});

			catalog.push_back(NodeType{
				"event.zone_enter", "On Zone Enter",
				"Runs when the watched object first comes inside the zone.",
				NodeCategory::Event,
				{},
				{exec("exec_out", "Then")},
				{data("zone", "Zone", PinType::EntityRef), data("watch_tag", "Watch Tag", PinType::Tag)}});

			catalog.push_back(NodeType{
				"event.zone_exit", "On Zone Exit",
				"Runs when the watched object leaves the zone.",
				NodeCategory::Event,
				{},
				{exec("exec_out", "Then")},
				{data("zone", "Zone", PinType::EntityRef), data("watch_tag", "Watch Tag", PinType::Tag)}});

			catalog.push_back(NodeType{
				"event.interact", "On Interact (E)",
				"Runs when the player presses E while close to the target.",
				NodeCategory::Event,
				{},
				{exec("exec_out", "Then")},
				{data("target", "Target", PinType::EntityRef), data("range", "Range", PinType::Float)}});

			// ---- Flow ----
			catalog.push_back(NodeType{
				"flow.branch", "Branch",
				"Runs one path or the other depending on a condition.",
				NodeCategory::Flow,
				{exec("exec_in", "In"), data("condition", "Condition", PinType::Bool)},
				{exec("exec_true", "True"), exec("exec_false", "False")},
				{}});

			catalog.push_back(NodeType{
				"flow.sequence", "Sequence",
				"Runs each output in order, top to bottom.",
				NodeCategory::Flow,
				{exec("exec_in", "In")},
				{exec("exec_0", "First"), exec("exec_1", "Second"), exec("exec_2", "Third")},
				{}});

			catalog.push_back(NodeType{
				"flow.delay", "Delay",
				"Waits, then continues. Does not block anything else.",
				NodeCategory::Flow,
				{exec("exec_in", "In")},
				{exec("exec_out", "Then")},
				{data("seconds", "Seconds", PinType::Float)}});

			// ---- Audio. Distance falloff is the AudioSource's own, already
			// working - these nodes only start and stop it. ----
			catalog.push_back(NodeType{
				"audio.play", "Play Audio",
				"Plays a clip. 3D falloff comes from the object's own Audio Source.",
				NodeCategory::Audio,
				{exec("exec_in", "In")},
				{exec("exec_out", "Then")},
				{data("clip", "Clip", PinType::AudioClip), data("volume", "Volume", PinType::Float),
				 data("loop", "Loop", PinType::Bool)}});

			catalog.push_back(NodeType{
				"audio.stop", "Stop Audio",
				"Stops a clip, or every clip when none is named.",
				NodeCategory::Audio,
				{exec("exec_in", "In")},
				{exec("exec_out", "Then")},
				{data("clip", "Clip", PinType::AudioClip)}});

			// ---- World ----
			catalog.push_back(NodeType{
				"world.set_active", "Show / Hide Object",
				"Shows or hides an object in the scene.",
				NodeCategory::Action,
				{exec("exec_in", "In")},
				{exec("exec_out", "Then")},
				{data("target", "Object", PinType::EntityRef), data("active", "Visible", PinType::Bool)}});

			catalog.push_back(NodeType{
				"world.set_light", "Set Light",
				"Changes a light's intensity and colour.",
				NodeCategory::Action,
				{exec("exec_in", "In")},
				{exec("exec_out", "Then")},
				{data("target", "Light", PinType::EntityRef),
				 data("intensity", "Intensity", PinType::Float),
				 data("color", "Colour", PinType::Vec3)}});

			return catalog;
		}
	}

	const char* nodeCategoryName(const NodeCategory category) noexcept
	{
		switch (category)
		{
			case NodeCategory::Event:  return "Event";
			case NodeCategory::Flow:   return "Flow";
			case NodeCategory::Audio:  return "Audio";
			case NodeCategory::Value:  return "Value";
			case NodeCategory::Action: break;
		}
		return "Action";
	}

	const std::vector<NodeType>& nodeCatalog()
	{
		static const std::vector<NodeType> catalog = buildCatalog();
		return catalog;
	}

	const NodeType* findNodeType(const std::string& typeId)
	{
		const std::vector<NodeType>& catalog = nodeCatalog();
		const auto found = std::find_if(
			catalog.begin(), catalog.end(), [&typeId](const NodeType& type) { return type.id == typeId; });
		return found == catalog.end() ? nullptr : &*found;
	}

	const PinSpec* findPin(const NodeType& type, const std::string& pinId)
	{
		for (const std::vector<PinSpec>* list : {&type.inputs, &type.outputs, &type.literals})
		{
			const auto found = std::find_if(
				list->begin(), list->end(), [&pinId](const PinSpec& pin) { return pin.id == pinId; });
			if (found != list->end())
			{
				return &*found;
			}
		}
		return nullptr;
	}
}
