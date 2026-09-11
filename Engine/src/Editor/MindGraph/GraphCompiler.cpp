#include "GameForger/Editor/MindGraph/GraphCompiler.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "GameForger/Editor/MindGraph/NodeCatalog.hpp"

namespace gameforger::editor::mindgraph
{
	namespace
	{
		// Lua string literal. Everything a scene name or a Windows path can
		// contain has to survive this, which is why it is one function rather
		// than inline concatenation at a dozen emit sites.
		std::string luaString(const std::string& value)
		{
			std::string out = "\"";
			for (const char character : value)
			{
				switch (character)
				{
					case '"':  out += "\\\""; break;
					case '\\': out += "\\\\"; break;
					case '\n': out += "\\n";  break;
					case '\r': out += "\\r";  break;
					default:   out += character; break;
				}
			}
			out += "\"";
			return out;
		}

		std::string literalOr(const GraphNode& node, const std::string& pinId, const std::string& fallback)
		{
			const auto found = node.literals.find(pinId);
			return found == node.literals.end() || found->second.empty() ? fallback : found->second;
		}

		std::string luaNumberOr(const GraphNode& node, const std::string& pinId, const char* fallback)
		{
			const std::string raw = literalOr(node, pinId, fallback);
			// A malformed number would produce Lua that fails to load, which
			// surfaces as a runtime error long after the edit. Validate here
			// and fall back, so a half-typed value cannot break the build.
			try
			{
				(void)std::stod(raw);
				return raw;
			}
			catch (...)
			{
				return fallback;
			}
		}

		std::string luaBoolOr(const GraphNode& node, const std::string& pinId, const bool fallback)
		{
			const std::string raw = literalOr(node, pinId, fallback ? "true" : "false");
			return (raw == "true" || raw == "1") ? "true" : "false";
		}

		struct Emitter
		{
			const MindGraph& graph;
			bool breadcrumbs = true;
			std::vector<CompileDiagnostic> diagnostics;
			// Guards against a cycle in the execution chain turning codegen
			// into an infinite loop. A cycle is a real authoring mistake, so
			// it is reported rather than silently broken.
			std::unordered_set<int> onStack;

			const GraphLink* execLinkFrom(const int nodeId, const std::string& pinId) const
			{
				for (const GraphLink& link : graph.links)
				{
					if (link.fromNode == nodeId && link.fromPin == pinId)
					{
						return &link;
					}
				}
				return nullptr;
			}

			void emitChain(const int nodeId, const std::string& execPin, std::string& out, const int indent)
			{
				const GraphLink* link = execLinkFrom(nodeId, execPin);
				if (link == nullptr)
				{
					return;
				}
				emitNode(link->toNode, out, indent);
			}

			void emitNode(const int nodeId, std::string& out, const int indent)
			{
				const GraphNode* node = graph.findNode(nodeId);
				if (node == nullptr)
				{
					return;
				}
				if (onStack.count(nodeId) > 0)
				{
					diagnostics.push_back(
						{nodeId, "This node is part of a loop in the execution chain. Codegen stops here.", true});
					return;
				}
				const NodeType* type = findNodeType(node->type);
				if (type == nullptr)
				{
					diagnostics.push_back(
						{nodeId, "Unknown node type \"" + node->type + "\" - skipped. The node itself is kept.",
						 false});
					return;
				}

				onStack.insert(nodeId);
				const std::string pad(static_cast<std::size_t>(indent), '\t');

				// SECTION 15: the breadcrumb wrapper, applied once here for
				// every node, rather than by each emitter individually.
				if (breadcrumbs)
				{
					out += pad + "__gfNode(" + std::to_string(nodeId) + ")\n";
				}

				emitBody(*node, *type, out, indent);
				onStack.erase(nodeId);
			}

			void emitBody(
				const GraphNode& node, const NodeType& type, std::string& out, const int indent)
			{
				const std::string pad(static_cast<std::size_t>(indent), '\t');
				const std::string& id = type.id;

				if (id == "flow.branch")
				{
					out += pad + "if self:__cond(" + std::to_string(node.id) + ") then\n";
					emitChain(node.id, "exec_true", out, indent + 1);
					out += pad + "else\n";
					emitChain(node.id, "exec_false", out, indent + 1);
					out += pad + "end\n";
					return;
				}
				if (id == "flow.sequence")
				{
					for (const char* pin : {"exec_0", "exec_1", "exec_2"})
					{
						emitChain(node.id, pin, out, indent);
					}
					return;
				}
				if (id == "flow.delay")
				{
					// Timers are per-node state on self, so two Delay nodes
					// never share a countdown. Scheduling rather than blocking
					// - a script that blocked would freeze the whole game.
					out += pad + "self:__after(" + std::to_string(node.id) + ", "
						+ luaNumberOr(node, "seconds", "1.0") + ", function()\n";
					emitChain(node.id, "exec_out", out, indent + 1);
					out += pad + "end)\n";
					return;
				}
				if (id == "audio.play")
				{
					// The binding is play(clip, VOLUME, loop) - three arguments,
					// with volume second. Emitting play(clip, loop) put a
					// boolean where a number belongs, and the script died at
					// on_start with "bad argument #2 to 'play'". Caught only by
					// actually running the generated Lua, which is exactly why
					// testMindGraphGeneratedLuaRuns exists: every other test
					// here checks the compiler emits what was intended, and
					// intending the wrong signature still passes all of them.
					out += pad + "self.audio:play(" + luaString(literalOr(node, "clip", "")) + ", "
						+ luaNumberOr(node, "volume", "1.0") + ", " + luaBoolOr(node, "loop", false)
						+ ")\n";
				}
				else if (id == "audio.stop")
				{
					out += pad + "self.audio:stop(" + luaString(literalOr(node, "clip", "")) + ")\n";
				}
				else if (id == "world.set_active")
				{
					out += pad + "self.world:setEntityActive("
						+ luaString(literalOr(node, "target", "")) + ", "
						+ luaBoolOr(node, "active", true) + ")\n";
				}
				else if (id == "world.set_light")
				{
					out += pad + "self:__setLight(" + luaString(literalOr(node, "target", "")) + ", "
						+ luaNumberOr(node, "intensity", "1.0") + ")\n";
				}
				else if (type.category != NodeCategory::Event)
				{
					diagnostics.push_back(
						{node.id, "No code generator for node type \"" + id + "\" yet.", false});
				}

				emitChain(node.id, "exec_out", out, indent);
			}
		};
	}

	namespace
	{
		// Support code the generated body calls into. Emitted with every graph
		// rather than shipped as a separate .lua, so a generated script has no
		// dependency that can go missing and stays readable end to end - which
		// is what design pillar 2 ("never a dead end") actually requires.
		//
		// Everything is per-instance state on `self`, so two graphs - or two
		// Delay nodes in one graph - never share a timer or a zone flag.
		std::string graphRuntimePrelude()
		{
			return
				"-- Support code the generated body calls into.\n"
				"\n"
				"-- Scheduled continuation for a Delay node. Scheduling rather\n"
				"-- than blocking: a script that blocked would freeze the game.\n"
				"function Graph:__after(id, seconds, fn)\n"
				"\tself.__timers = self.__timers or {}\n"
				"\tself.__timers[id] = { remaining = seconds, fn = fn }\n"
				"end\n"
				"\n"
				"function Graph:__tickTimers(dt)\n"
				"\tif self.__timers == nil then return end\n"
				"\tfor id, timer in pairs(self.__timers) do\n"
				"\t\ttimer.remaining = timer.remaining - dt\n"
				"\t\tif timer.remaining <= 0 then\n"
				"\t\t\tself.__timers[id] = nil\n"
				"\t\t\ttimer.fn()\n"
				"\t\tend\n"
				"\tend\n"
				"end\n"
				"\n"
				"-- Branch condition. Data wires are not compiled yet (a later\n"
				"-- phase), so a Branch currently takes its False path. Stated\n"
				"-- plainly rather than silently taking True and looking right.\n"
				"function Graph:__cond(id)\n"
				"\treturn false\n"
				"end\n"
				"\n"
				"-- Zone enter/exit as EDGES, not states: fires on the frame the\n"
				"-- watched object crosses the boundary, not every frame it is\n"
				"-- inside. Without the remembered flag an On Zone Enter would\n"
				"-- retrigger sixty times a second while the player stood there.\n"
				"function Graph:__zone(id, zoneName, watchTag, wantEnter)\n"
				"\tself.__zoneInside = self.__zoneInside or {}\n"
				"\tlocal target, distance = self.world:findNearestWithTag(watchTag)\n"
				"\tlocal radius = self.zone_radius or 4.0\n"
				"\tlocal inside = target ~= nil and distance ~= nil and distance <= radius\n"
				"\tlocal was = self.__zoneInside[id] or false\n"
				"\tself.__zoneInside[id] = inside\n"
				"\tif wantEnter then return inside and not was end\n"
				"\treturn was and not inside\n"
				"end\n"
				"\n"
				"function Graph:__interact(id, targetName, range)\n"
				"\tlocal target, distance = self.world:findNearestWithTag(\"Player\")\n"
				"\tif target == nil or distance == nil or distance > range then return false end\n"
				"\treturn self.input:isKeyPressed(\"E\")\n"
				"end\n"
				"\n"
				"function Graph:__setLight(name, intensity)\n"
				"\tif name == nil or name == \"\" then return end\n"
				"\tself.world:setLightIntensity(name, intensity)\n"
				"end\n"
				"\n";
		}
	}

	CompileResult compileGraph(const MindGraph& graph, const bool emitNodeBreadcrumbs)
	{
		CompileResult result;
		Emitter emitter{graph, emitNodeBreadcrumbs, {}, {}};

		std::string startBody;
		std::string updateBody;
		std::string zoneBody;

		for (const GraphNode& node : graph.nodes)
		{
			const NodeType* type = findNodeType(node.type);
			if (type == nullptr)
			{
				// Preserved, not discarded - a graph from a newer editor must
				// survive a load/save here without losing that node.
				result.diagnostics.push_back(
					{node.id, "Unknown node type \"" + node.type + "\" - kept in the graph, not compiled.",
					 false});
				continue;
			}
			if (type->category != NodeCategory::Event)
			{
				continue;
			}
			if (type->id == "event.game_start")
			{
				emitter.emitChain(node.id, "exec_out", startBody, 2);
			}
			else if (type->id == "event.update")
			{
				emitter.emitChain(node.id, "exec_out", updateBody, 2);
			}
			else if (type->id == "event.zone_enter" || type->id == "event.zone_exit")
			{
				const bool entering = type->id == "event.zone_enter";
				zoneBody += "\t\tif self:__zone(" + std::to_string(node.id) + ", "
					+ luaString(literalOr(node, "zone", "")) + ", "
					+ luaString(literalOr(node, "watch_tag", "Player")) + ", "
					+ (entering ? "true" : "false") + ") then\n";
				emitter.emitChain(node.id, "exec_out", zoneBody, 3);
				zoneBody += "\t\tend\n";
			}
			else if (type->id == "event.interact")
			{
				zoneBody += "\t\tif self:__interact(" + std::to_string(node.id) + ", "
					+ luaString(literalOr(node, "target", "")) + ", "
					+ luaNumberOr(node, "range", "2.5") + ") then\n";
				emitter.emitChain(node.id, "exec_out", zoneBody, 3);
				zoneBody += "\t\tend\n";
			}
		}

		for (const GraphLink* broken : graph.brokenLinks())
		{
			result.diagnostics.push_back(
				{broken->fromNode, "A link points at a node that no longer exists.", true});
		}
		for (const CompileDiagnostic& diagnostic : emitter.diagnostics)
		{
			result.diagnostics.push_back(diagnostic);
		}

		const std::string name = graph.name.empty() ? std::string("MindGraph") : graph.name;
		std::string lua;
		lua += "-- GENERATED BY MIND GRAPH - DO NOT EDIT BY HAND.\n";
		lua += "-- Source graph: " + name + ".gfgraph\n";
		lua += "-- Editing this file is fine for reading and debugging, but the next\n";
		lua += "-- compile overwrites it. Change the graph instead.\n";
		lua += "\n";
		lua += "local Graph = {}\n\n";
		if (emitNodeBreadcrumbs)
		{
			// Resolves to a no-op unless the editor has installed a watcher,
			// so a shipped game pays one empty call per node and nothing more.
			// Exactly one line, no trailing blank. The blank would itself be a
			// difference between the wrapped and unwrapped output, and section
			// 15 requires the diff to be wrapper lines and nothing else - a
			// contract the test enforces by stripping those lines and
			// demanding what remains is identical.
			lua += "local __gfNode = rawget(_G, \"__gfNode\") or function(id) end\n";
		}
		lua += graphRuntimePrelude();
		lua += "function Graph:on_start()\n";
		lua += "\tself.__timers = {}\n";
		lua += "\tself.__zoneInside = {}\n";
		lua += "\tdo\n";
		lua += startBody.empty() ? "\t\t-- nothing wired to On Game Start\n" : startBody;
		lua += "\tend\n";
		lua += "end\n\n";
		lua += "function Graph:on_update(dt)\n";
		lua += "\tself:__tickTimers(dt)\n";
		lua += "\tdo\n";
		lua += updateBody.empty() ? "\t\t-- nothing wired to On Update\n" : updateBody;
		lua += "\tend\n";
		lua += zoneBody;
		lua += "end\n\n";
		lua += "return Graph\n";

		result.lua = lua;
		result.success = std::none_of(
			result.diagnostics.begin(), result.diagnostics.end(),
			[](const CompileDiagnostic& diagnostic) { return diagnostic.isError; });
		return result;
	}
}
