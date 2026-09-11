#include "GameForger/Editor/MindGraph/GraphSerializer.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>

#include "GameForger/Editor/Json.hpp"

namespace gameforger::editor::mindgraph
{
	namespace
	{
		constexpr int kGraphFormatVersion = 1;

		std::string escapeJson(const std::string& text)
		{
			std::string escaped;
			escaped.reserve(text.size());
			for (const char character : text)
			{
				switch (character)
				{
					case '"':  escaped += "\\\""; break;
					case '\\': escaped += "\\\\"; break;
					case '\n': escaped += "\\n";  break;
					case '\r': escaped += "\\r";  break;
					case '\t': escaped += "\\t";  break;
					default:   escaped += character; break;
				}
			}
			return escaped;
		}

		std::string floatToJson(const float value)
		{
			std::array<char, 32> buffer{};
			std::snprintf(buffer.data(), buffer.size(), "%.6f", value);
			return buffer.data();
		}

		std::string readString(const json::Value& obj, const char* key, const std::string& fallback = {})
		{
			const json::Value* field = obj.find(key);
			return field == nullptr ? fallback : field->asString().value_or(fallback);
		}

		int readInt(const json::Value& obj, const char* key, const int fallback)
		{
			const json::Value* field = obj.find(key);
			return field == nullptr ? fallback : static_cast<int>(field->asNumber().value_or(fallback));
		}
	}

	std::string serializeGraph(const MindGraph& graph)
	{
		std::string json;
		json += "{\n";
		json += "  \"format\": \"GameForgerMindGraph\",\n";
		json += "  \"version\": " + std::to_string(kGraphFormatVersion) + ",\n";
		json += "  \"name\": \"" + escapeJson(graph.name) + "\",\n";
		json += "  \"nextNodeId\": " + std::to_string(graph.nextNodeId) + ",\n";
		json += "  \"nextLinkId\": " + std::to_string(graph.nextLinkId) + ",\n";

		json += "  \"nodes\": [\n";
		for (std::size_t index = 0; index < graph.nodes.size(); ++index)
		{
			const GraphNode& node = graph.nodes[index];
			json += "    {\n";
			json += "      \"id\": " + std::to_string(node.id) + ",\n";
			json += "      \"type\": \"" + escapeJson(node.type) + "\",\n";
			json += "      \"position\": [" + floatToJson(node.canvasPosition.x) + ", "
				+ floatToJson(node.canvasPosition.y) + "],\n";
			json += "      \"literals\": {";
			// std::map rather than the node's own unordered_map purely so the
			// output is deterministic: an unordered iteration order would make
			// a saved graph differ byte-for-byte between runs with no edit,
			// which turns every save into a spurious diff.
			const std::map<std::string, std::string> ordered(node.literals.begin(), node.literals.end());
			bool firstLiteral = true;
			for (const auto& [key, value] : ordered)
			{
				json += (firstLiteral ? "\n" : ",\n");
				json += "        \"" + escapeJson(key) + "\": \"" + escapeJson(value) + "\"";
				firstLiteral = false;
			}
			json += firstLiteral ? "}\n" : "\n      }\n";
			json += "    }";
			json += (index + 1 < graph.nodes.size()) ? ",\n" : "\n";
		}
		json += "  ],\n";

		json += "  \"links\": [\n";
		for (std::size_t index = 0; index < graph.links.size(); ++index)
		{
			const GraphLink& link = graph.links[index];
			// Endpoints as (node id, pin id STRING). Never an integer pin
			// index - see GraphData.hpp. This is the line that makes adding a
			// pin to a node type safe for every graph already on disk.
			json += "    { \"id\": " + std::to_string(link.id)
				+ ", \"fromNode\": " + std::to_string(link.fromNode)
				+ ", \"fromPin\": \"" + escapeJson(link.fromPin) + "\""
				+ ", \"toNode\": " + std::to_string(link.toNode)
				+ ", \"toPin\": \"" + escapeJson(link.toPin) + "\" }";
			json += (index + 1 < graph.links.size()) ? ",\n" : "\n";
		}
		json += "  ]\n";
		json += "}\n";
		return json;
	}

	GraphLoadResult deserializeGraph(const std::string& text)
	{
		GraphLoadResult result;

		const std::optional<json::Value> parsed = json::parse(text);
		if (!parsed.has_value() || parsed->type != json::Value::Type::Object)
		{
			result.message = "Not a valid JSON object.";
			return result;
		}
		if (readString(*parsed, "format") != "GameForgerMindGraph")
		{
			// Refuse rather than guess. Loading an arbitrary JSON file as a
			// graph would produce an empty canvas and a silent data loss on
			// the next save.
			result.message = "Not a GameForgerMindGraph file.";
			return result;
		}
		const int version = readInt(*parsed, "version", 0);
		if (version > kGraphFormatVersion)
		{
			result.message = "This graph was written by a newer editor (format version "
				+ std::to_string(version) + ").";
			return result;
		}

		MindGraph graph;
		graph.name = readString(*parsed, "name");

		if (const json::Value* nodes = parsed->find("nodes"))
		{
			if (nodes->type == json::Value::Type::Array)
			{
				for (const json::Value& nodeValue : nodes->arrayValue)
				{
					if (nodeValue.type != json::Value::Type::Object)
					{
						continue;
					}
					GraphNode node;
					node.id = readInt(nodeValue, "id", 0);
					node.type = readString(nodeValue, "type");
					if (node.id == 0 || node.type.empty())
					{
						continue;
					}
					if (const json::Value* position = nodeValue.find("position"))
					{
						if (const std::optional<std::vector<double>> numbers = position->asNumberArray())
						{
							if (numbers->size() == 2)
							{
								node.canvasPosition = glm::vec2(
									static_cast<float>((*numbers)[0]), static_cast<float>((*numbers)[1]));
							}
						}
					}
					if (const json::Value* literals = nodeValue.find("literals"))
					{
						if (literals->type == json::Value::Type::Object)
						{
							for (const auto& [key, value] : literals->objectValue)
							{
								if (const std::optional<std::string> text2 = value.asString())
								{
									node.literals[key] = *text2;
								}
							}
						}
					}
					graph.nodes.push_back(std::move(node));
				}
			}
		}

		if (const json::Value* links = parsed->find("links"))
		{
			if (links->type == json::Value::Type::Array)
			{
				for (const json::Value& linkValue : links->arrayValue)
				{
					if (linkValue.type != json::Value::Type::Object)
					{
						continue;
					}
					GraphLink link;
					link.id = readInt(linkValue, "id", 0);
					link.fromNode = readInt(linkValue, "fromNode", 0);
					link.toNode = readInt(linkValue, "toNode", 0);
					link.fromPin = readString(linkValue, "fromPin");
					link.toPin = readString(linkValue, "toPin");
					// A link missing an endpoint is structurally meaningless -
					// unlike a link pointing at a pin that no longer exists,
					// which is preserved and reported as broken.
					if (link.id == 0 || link.fromNode == 0 || link.toNode == 0 || link.fromPin.empty()
						|| link.toPin.empty())
					{
						continue;
					}
					graph.links.push_back(std::move(link));
				}
			}
		}

		// Recomputed from content rather than trusted from the file, so a
		// hand-edited or truncated graph cannot hand out an id that is already
		// in use - which would make two nodes indistinguishable to every link.
		int maxNodeId = 0;
		for (const GraphNode& node : graph.nodes)
		{
			maxNodeId = std::max(maxNodeId, node.id);
		}
		int maxLinkId = 0;
		for (const GraphLink& link : graph.links)
		{
			maxLinkId = std::max(maxLinkId, link.id);
		}
		graph.nextNodeId = std::max(readInt(*parsed, "nextNodeId", 1), maxNodeId + 1);
		graph.nextLinkId = std::max(readInt(*parsed, "nextLinkId", 1), maxLinkId + 1);

		result.success = true;
		result.graph = std::move(graph);
		return result;
	}

	GraphSaveResult saveGraph(const std::filesystem::path& path, const MindGraph& graph)
	{
		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);

		std::ofstream output(path, std::ios::binary);
		if (!output)
		{
			return {false, "Could not open " + path.string() + " for writing."};
		}
		output << serializeGraph(graph);
		if (!output)
		{
			return {false, "Failed while writing " + path.string() + "."};
		}
		return {true, "Saved " + path.string() + "."};
	}

	GraphLoadResult loadGraph(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		if (!input)
		{
			GraphLoadResult result;
			result.message = "Could not open " + path.string() + ".";
			return result;
		}
		const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		GraphLoadResult result = deserializeGraph(text);
		if (result.success && result.graph.name.empty())
		{
			result.graph.name = path.stem().string();
		}
		return result;
	}
}
