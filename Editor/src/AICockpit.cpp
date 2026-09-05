#include "GameForger/Editor/AICockpit.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <utility>
#include <variant>

#include "GameForger/Editor/AICommand.hpp"
#include "GameForger/Editor/AIProviderClient.hpp"
#include "GameForger/Editor/AIChatResponse.hpp"

namespace gameforger::editor
{
	// Kill-switch checked between turns. One per state - referenced by the
	// worker via a captured pointer.
	namespace
	{
		std::atomic<bool>* stopFlagFor(AICockpitState& state)
		{
			// Piggyback on loopRunning as the observable; a dedicated flag
			// would double state without changing behaviour. We poll
			// loopRunning at natural yield points in the worker.
			return &state.loopRunning;
		}
	}

	void AIActionRing::push(AIActionEntry entry)
	{
		entries_.push_back(std::move(entry));
		while (entries_.size() > kCapacity)
		{
			entries_.pop_front();
		}
	}

	bool AIActionRing::undoLast()
	{
		if (entries_.empty()) return false;
		AIActionEntry entry = std::move(entries_.back());
		entries_.pop_back();
		if (entry.undo)
		{
			entry.undo();
		}
		return true;
	}

	void AIActionRing::clear()
	{
		entries_.clear();
	}

	// NOTE: this is a PRESENTATION helper - it drives the orange highlight on
	// tool rows in the chat transcript. It is deliberately NOT the security
	// gate; see requiresApproval(), which works from an allowlist. A
	// substring blocklist cannot be a security boundary here because most
	// tool names arrive from the MCP server's own tools/list response, i.e.
	// they are chosen by the other end of the socket.
	bool isDestructiveTool(const std::string& name)
	{
		// Case-insensitive substring test for the usual suspects. Anything
		// that names `delete` / `remove` / `clear` / `overwrite` / `stop`,
		// plus MCP's `code.execute_python` (always dangerous - arbitrary bpy).
		std::string lower = name;
		std::transform(lower.begin(), lower.end(), lower.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		static const char* kMarkers[] = {
			"delete", "remove", "clear", "overwrite", "erase", "purge",
			"scene.stop", "code.execute_python", "execute_python"
		};
		for (const char* marker : kMarkers)
		{
			if (lower.find(marker) != std::string::npos) return true;
		}
		return false;
	}

	// -----------------------------------------------------------------------
	// Scene tool catalog + dispatcher
	// -----------------------------------------------------------------------

	namespace
	{
		json::Value makeProperty(const char* type, const char* description)
		{
			return json::makeObject({
				{"type",        json::makeString(type)},
				{"description", json::makeString(description)},
			});
		}

		json::Value makeVec3Property(const char* description)
		{
			return json::makeObject({
				{"type", json::makeString("object")},
				{"description", json::makeString(description)},
				{"properties", json::makeObject({
					{"x", json::makeObject({{"type", json::makeString("number")}})},
					{"y", json::makeObject({{"type", json::makeString("number")}})},
					{"z", json::makeObject({{"type", json::makeString("number")}})},
				})},
				{"required", json::makeArray({
					json::makeString("x"),
					json::makeString("y"),
					json::makeString("z"),
				})},
			});
		}

		// One catalog entry -> a JSON tool schema in the shape both Anthropic
		// and OpenAI-compat accept (they read `name`, `description`, and
		// input_schema/parameters similarly).
		json::Value makeToolSchema(const char* name, const char* description, json::Value properties, std::vector<std::string> required)
		{
			std::vector<json::Value> req;
			req.reserve(required.size());
			for (const std::string& r : required) req.push_back(json::makeString(r));
			return json::makeObject({
				{"name",        json::makeString(name)},
				{"description", json::makeString(description)},
				{"input_schema", json::makeObject({
					{"type",       json::makeString("object")},
					{"properties", std::move(properties)},
					{"required",   json::makeArray(std::move(req))},
				})},
			});
		}

		float readFloat(const json::Value* v, float fallback)
		{
			if (v == nullptr) return fallback;
			auto n = v->asNumber();
			return n ? static_cast<float>(*n) : fallback;
		}

		glm::vec3 readVec3(const json::Value* v)
		{
			if (v == nullptr || v->type != json::Value::Type::Object) return {0.0F, 0.0F, 0.0F};
			return glm::vec3(
				readFloat(v->find("x"), 0.0F),
				readFloat(v->find("y"), 0.0F),
				readFloat(v->find("z"), 0.0F));
		}

		std::string readString(const json::Value* v)
		{
			if (v == nullptr) return {};
			auto s = v->asString();
			return s ? *s : std::string{};
		}
	}

	json::Value sceneToolCatalog()
	{
		std::vector<json::Value> tools;

		tools.push_back(makeToolSchema(
			"scene.list_entities",
			"List every entity currently in the editor scene. Returns an array of {name, position} objects.",
			json::makeObject({}), {}));

		tools.push_back(makeToolSchema(
			"scene.create_primitive",
			"Create a primitive-mesh entity (cube/sphere/cylinder/cone/plane/capsule) at the given position.",
			json::makeObject({
				{"name",      makeProperty("string", "Display name for the new entity.")},
				{"primitive", makeProperty("string", "One of: cube, sphere, cylinder, cone, plane, capsule.")},
				{"position",  makeVec3Property("Where to place the entity (world coords, Y up).")},
			}),
			{"name", "primitive"}));

		tools.push_back(makeToolSchema(
			"scene.delete_entity",
			"Delete an entity by name. Destructive; requires user approval unless autonomous mode is on.",
			json::makeObject({
				{"name", makeProperty("string", "Exact name of the entity to delete.")},
			}),
			{"name"}));

		tools.push_back(makeToolSchema(
			"scene.set_position",
			"Move an existing entity to a new position (world coords).",
			json::makeObject({
				{"name",     makeProperty("string", "Entity to move.")},
				{"position", makeVec3Property("Target position.")},
			}),
			{"name", "position"}));

		tools.push_back(makeToolSchema(
			"scene.duplicate_entity",
			"Duplicate an existing entity; the copy is placed near the original.",
			json::makeObject({
				{"name", makeProperty("string", "Entity to duplicate.")},
			}),
			{"name"}));

		tools.push_back(makeToolSchema(
			"scene.import_model",
			"Import a 3D model file (already inside Game/Models/) into the scene as a new entity.",
			json::makeObject({
				{"source_path", makeProperty("string", "Path RELATIVE to the project root, e.g. Game/Models/foo.glb.")},
				{"name",        makeProperty("string", "Display name for the new entity.")},
				{"position",    makeVec3Property("Where to place it.")},
			}),
			{"source_path", "name"}));

		tools.push_back(makeToolSchema(
			"scene.attach_script",
			"Attach a Lua script (already in Game/Scripts/) to an entity.",
			json::makeObject({
				{"entity_name", makeProperty("string", "Entity to attach the script to.")},
				{"script_path", makeProperty("string", "Path RELATIVE to the project root, e.g. Game/Scripts/fps_controller.lua.")},
			}),
			{"entity_name", "script_path"}));

		tools.push_back(makeToolSchema(
			"scene.add_tag",
			"Add a string tag to an entity.",
			json::makeObject({
				{"entity_name", makeProperty("string", "Entity to tag.")},
				{"tag",         makeProperty("string", "Tag string.")},
			}),
			{"entity_name", "tag"}));

		return json::makeArray(std::move(tools));
	}

	namespace
	{
		struct ToolCallResult
		{
			bool ok = false;
			std::string summary;
			std::function<void()> undo;
		};

		PrimitiveType parsePrimitive(const std::string& s)
		{
			std::string lower = s;
			std::transform(lower.begin(), lower.end(), lower.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (lower == "sphere")   return PrimitiveType::Sphere;
			if (lower == "cylinder") return PrimitiveType::Cylinder;
			if (lower == "cone")     return PrimitiveType::Cone;
			if (lower == "plane")    return PrimitiveType::Plane;
			if (lower == "capsule")  return PrimitiveType::Capsule;
			return PrimitiveType::Cube;
		}

		// Route a scene tool call to AICommandBus. Returns a summary string
		// for the tool trace + an undo lambda that pushes the inverse command.
		ToolCallResult dispatchSceneTool(
			EditorScene& scene,
			AICommandBus& bus,
			const std::string& name,
			const json::Value& args)
		{
			ToolCallResult out;

			if (name == "scene.list_entities")
			{
				std::ostringstream summary;
				summary << "[";
				bool first = true;
				for (const SceneEntity& e : scene.entities())
				{
					if (!first) summary << ",";
					first = false;
					summary << "{\"name\":\"" << e.name << "\",\"position\":{"
					        << "\"x\":" << e.position.x << ",\"y\":" << e.position.y << ",\"z\":" << e.position.z << "}}";
				}
				summary << "]";
				out.ok = true;
				out.summary = summary.str();
				return out;
			}

			if (name == "scene.create_primitive")
			{
				CreateEntityCommand cmd;
				cmd.name = readString(args.find("name"));
				cmd.primitive = parsePrimitive(readString(args.find("primitive")));
				cmd.position = readVec3(args.find("position"));
				const AICommandResult result = bus.execute(cmd);
				out.ok = result.success;
				out.summary = result.message;
				if (out.ok)
				{
					const std::string createdName = cmd.name;
					out.undo = [&bus, createdName]()
					{
						DeleteEntityCommand del;
						del.entityName = createdName;
						(void)bus.execute(del);
					};
				}
				return out;
			}

			if (name == "scene.delete_entity")
			{
				DeleteEntityCommand cmd;
				cmd.entityName = readString(args.find("name"));
				const AICommandResult result = bus.execute(cmd);
				out.ok = result.success;
				out.summary = result.message;
				// Undo of a delete would require restoring the entity's full
				// state. The editor's own undo history already snapshots this
				// via the EditHistory pipeline; ring-undo of a delete is
				// therefore a no-op here to avoid partial restoration.
				return out;
			}

			if (name == "scene.set_position")
			{
				SetPositionCommand cmd;
				cmd.entityName = readString(args.find("name"));
				cmd.position = readVec3(args.find("position"));
				// Capture the old position for undo BEFORE executing.
				glm::vec3 oldPosition = cmd.position;
				bool foundOld = false;
				for (const SceneEntity& e : scene.entities())
				{
					if (e.name == cmd.entityName) { oldPosition = e.position; foundOld = true; break; }
				}
				const AICommandResult result = bus.execute(cmd);
				out.ok = result.success;
				out.summary = result.message;
				if (out.ok && foundOld)
				{
					const std::string entityName = cmd.entityName;
					const glm::vec3 restore = oldPosition;
					out.undo = [&bus, entityName, restore]()
					{
						SetPositionCommand revert;
						revert.entityName = entityName;
						revert.position = restore;
						(void)bus.execute(revert);
					};
				}
				return out;
			}

			if (name == "scene.duplicate_entity")
			{
				DuplicateEntityCommand cmd;
				cmd.entityName = readString(args.find("name"));
				const AICommandResult result = bus.execute(cmd);
				out.ok = result.success;
				out.summary = result.message;
				return out;
			}

			if (name == "scene.import_model")
			{
				CreateImportedMeshCommand cmd;
				cmd.name = readString(args.find("name"));
				cmd.sourcePath = readString(args.find("source_path"));
				cmd.position = readVec3(args.find("position"));
				const AICommandResult result = bus.execute(cmd);
				out.ok = result.success;
				out.summary = result.message;
				return out;
			}

			if (name == "scene.attach_script")
			{
				AttachScriptCommand cmd;
				cmd.entityName = readString(args.find("entity_name"));
				cmd.scriptPath = readString(args.find("script_path"));
				const AICommandResult result = bus.execute(cmd);
				out.ok = result.success;
				out.summary = result.message;
				return out;
			}

			if (name == "scene.add_tag")
			{
				AddTagCommand cmd;
				cmd.entityName = readString(args.find("entity_name"));
				cmd.tag = readString(args.find("tag"));
				const AICommandResult result = bus.execute(cmd);
				out.ok = result.success;
				out.summary = result.message;
				if (out.ok)
				{
					const std::string entityName = cmd.entityName;
					const std::string tag = cmd.tag;
					out.undo = [&bus, entityName, tag]()
					{
						RemoveTagCommand rem;
						rem.entityName = entityName;
						rem.tag = tag;
						(void)bus.execute(rem);
					};
				}
				return out;
			}

			out.summary = "Unknown scene tool: " + name;
			return out;
		}

		ToolCallResult dispatchBlenderTool(
			BlenderClient& client,
			const std::string& name,
			const std::string& argsJson)
		{
			ToolCallResult out;
			// Strip the "blender." prefix before sending upstream.
			std::string bareName = name;
			const std::string prefix = "blender.";
			if (bareName.rfind(prefix, 0) == 0) bareName = bareName.substr(prefix.size());
			const BlenderClient::Response response = client.callTool(bareName, argsJson);
			out.ok = response.ok;
			out.summary = response.ok
				? json::serialize(response.result)
				: ("Blender error: " + response.error);
			if (out.ok)
			{
				// Best-effort undo via bpy.ops.ed.undo(). Not perfect for
				// multi-op tool calls but a workable v1 - see plan Part C.5.
				out.undo = [&client]()
				{
					// Custom raw-string delimiter: R"(...)" would terminate at
					// the `)"` inside `undo()"`.
					(void)client.callTool(
						"code.execute_python",
						R"py({"code":"import bpy\nbpy.ops.ed.undo()"})py");
				};
			}
			return out;
		}
	}

	// -----------------------------------------------------------------------
	// Approval gating
	// -----------------------------------------------------------------------

	namespace
	{
		// Tools this editor implements itself, which are non-destructive and
		// reversible through the AI undo ring. This is an ALLOWLIST, and it is
		// exhaustive on purpose: the previous gate asked "does this name look
		// destructive?", which fails open for anything it does not recognise.
		// That matters because only these entries are ours - every other tool
		// name reaching requiresApproval() came from the MCP server's
		// tools/list response, so the string is chosen by the other end of the
		// socket. A tool called "bpy.run" or "object.set_script" contains none
		// of the old blocklist markers and would have run unattended.
		//
		// scene.delete_entity and scene.remove_tag are ours too, but they
		// destroy authored work, so they stay out of this list.
		bool isAutoApprovableTool(const std::string& toolName)
		{
			static const char* kSafeTools[] = {
				"scene.list_entities",
				"scene.create_primitive",
				"scene.set_position",
				"scene.duplicate_entity",
				"scene.import_model",
				"scene.attach_script",
				"scene.add_tag",
			};
			for (const char* safe : kSafeTools)
			{
				if (toolName == safe) return true;
			}
			return false;
		}

		// Arbitrary Python inside Blender. Never auto-approved, in any mode,
		// under any setting - this is the one capability that can do anything
		// the user's account can.
		bool isAlwaysGatedTool(const std::string& toolName)
		{
			return toolName.find("execute_python") != std::string::npos;
		}

		bool requiresApproval(const AICockpitState& state, const std::string& toolName)
		{
			if (isAlwaysGatedTool(toolName))
			{
				return true;
			}
			// Non-autonomous mode: everything needs approval (except read-only
			// scene.list_entities which is safe).
			if (!state.autonomousMode)
			{
				return toolName != "scene.list_entities";
			}
			// Autonomous mode: run the known-safe editor tools unattended.
			if (isAutoApprovableTool(toolName))
			{
				return false;
			}
			// Everything else - our own destructive tools, and every tool
			// discovered from the MCP server - needs approval unless the user
			// has explicitly opted into blanket auto-approval.
			return !state.approveDestructiveAutomatically;
		}

		// Blocks the worker until the main thread calls approve/reject.
		// Returns true = approved, false = rejected.
		bool waitForApproval(AICockpitState& state, const std::string& toolName, const std::string& argsJson)
		{
			{
				std::lock_guard lock(state.approvalMutex);
				state.pendingApproval = PendingApproval{toolName, argsJson};
				state.approvalDecision.store(0);
			}
			state.approvalGate.notify_all();

			std::unique_lock lock(state.approvalMutex);
			state.approvalGate.wait(lock, [&]()
			{
				return state.approvalDecision.load() != 0 || !state.loopRunning.load();
			});
			const int decision = state.approvalDecision.load();
			state.pendingApproval.reset();
			return decision == 1;
		}

		void pushIncoming(AICockpitState& state, CockpitChatMessage msg)
		{
			std::lock_guard lock(state.incomingMutex);
			state.incoming.push_back(std::move(msg));
		}

		void pushRingEntry(AICockpitState& state, AIActionEntry entry)
		{
			std::lock_guard lock(state.incomingMutex);
			state.pendingRingEntries.push_back(std::move(entry));
		}
	}

	void approveDestructive(AICockpitState& state)
	{
		state.approvalDecision.store(1);
		state.approvalGate.notify_all();
	}

	void rejectDestructive(AICockpitState& state)
	{
		state.approvalDecision.store(2);
		state.approvalGate.notify_all();
	}

	void requestCockpitStop(AICockpitState& state)
	{
		// loopRunning is part of the approvalGate predicate (see
		// waitForApproval), so it must be mutated under approvalMutex. Storing
		// it unlocked lets this notify land while the worker still holds the
		// mutex evaluating that predicate - the wakeup is then lost and the
		// worker blocks forever on an approval that will never come again.
		{
			std::lock_guard lock(state.approvalMutex);
			state.loopRunning.store(false);
		}
		state.approvalGate.notify_all();
	}

	void pumpCockpit(AICockpitState& state)
	{
		std::deque<CockpitChatMessage> drainedMessages;
		std::deque<AIActionEntry> drainedEntries;
		{
			std::lock_guard lock(state.incomingMutex);
			drainedMessages.swap(state.incoming);
			drainedEntries.swap(state.pendingRingEntries);
		}
		for (CockpitChatMessage& msg : drainedMessages)
		{
			state.messages.push_back(std::move(msg));
		}
		for (AIActionEntry& entry : drainedEntries)
		{
			state.undoRing.push(std::move(entry));
		}
	}

	void joinCockpitWorker(AICockpitState& state)
	{
		// Same lost-wakeup hazard as requestCockpitStop, and worse here: this
		// runs on editor shutdown, so a missed notify hangs the process on
		// join() with its window already gone.
		{
			std::lock_guard lock(state.approvalMutex);
			state.loopRunning.store(false);
		}
		state.approvalGate.notify_all();
		if (state.worker.joinable())
		{
			state.worker.join();
		}
	}

	// -----------------------------------------------------------------------
	// Body builders: Anthropic vs. OpenAI-compat
	// -----------------------------------------------------------------------

	namespace
	{
		std::string toProviderToolName(const std::string& name)
		{
			std::string out;
			out.reserve(name.size() + 4);
			for (const char c : name)
			{
				if (c == '.')
				{
					out += "__";
				}
				else
				{
					out += c;
				}
			}
			return out;
		}

		std::string fromProviderToolName(const std::string& name)
		{
			std::string out;
			out.reserve(name.size());
			for (std::size_t i = 0; i < name.size(); ++i)
			{
				if (i + 1 < name.size() && name[i] == '_' && name[i + 1] == '_')
				{
					out += '.';
					++i;
				}
				else
				{
					out += name[i];
				}
			}
			return out;
		}

		struct ParsedTurn
		{
			std::string assistantText;
			struct ToolUse { std::string id; std::string name; std::string argsJson; };
			std::vector<ToolUse> toolUses;
			std::string stopReason;
			std::string errorMessage;
		};

		struct LoopTurn
		{
			std::string role; // user | assistant | tool
			std::string text;
			std::vector<ParsedTurn::ToolUse> toolUses;
			std::string toolCallId;
			std::string toolName;
		};

		std::string buildAnthropicBody(
			const std::string& modelId,
			const std::vector<LoopTurn>& history,
			const json::Value& tools,
			const std::string& systemPrompt)
		{
			std::vector<json::Value> messages;
			for (const LoopTurn& m : history)
			{
				if (m.role == "user")
				{
					messages.push_back(json::makeObject({
						{"role", json::makeString("user")},
						{"content", json::makeString(m.text)},
					}));
				}
				else if (m.role == "assistant")
				{
					std::vector<json::Value> blocks;
					if (!m.text.empty())
					{
						blocks.push_back(json::makeObject({
							{"type", json::makeString("text")},
							{"text", json::makeString(m.text)},
						}));
					}
					for (const ParsedTurn::ToolUse& use : m.toolUses)
					{
						json::Value input = json::makeObject({});
						if (const std::optional<json::Value> parsed = json::parse(use.argsJson); parsed)
						{
							input = *parsed;
						}
						blocks.push_back(json::makeObject({
							{"type", json::makeString("tool_use")},
							{"id", json::makeString(use.id)},
							{"name", json::makeString(use.name)},
							{"input", std::move(input)},
						}));
					}
					if (blocks.empty())
					{
						blocks.push_back(json::makeObject({
							{"type", json::makeString("text")},
							{"text", json::makeString(m.text)},
						}));
					}
					messages.push_back(json::makeObject({
						{"role", json::makeString("assistant")},
						{"content", json::makeArray(std::move(blocks))},
					}));
				}
				else if (m.role == "tool")
				{
					messages.push_back(json::makeObject({
						{"role", json::makeString("user")},
						{"content", json::makeArray({json::makeObject({
							{"type", json::makeString("tool_result")},
							{"tool_use_id", json::makeString(m.toolCallId)},
							{"content", json::makeString(m.text)},
						})})},
					}));
				}
			}
			return json::serialize(json::makeObject({
				{"model",      json::makeString(modelId)},
				{"max_tokens", json::makeNumber(4096)},
				{"system",     json::makeString(systemPrompt)},
				{"tools",      tools},
				{"messages",   json::makeArray(std::move(messages))},
			}));
		}

		std::string buildOpenAiBody(
			const std::string& modelId,
			const std::vector<LoopTurn>& history,
			const json::Value& toolSchemas,
			const std::string& systemPrompt,
			int reasoningEffort,
			bool supportsReasoningEffort)
		{
			std::vector<json::Value> openaiTools;
			if (toolSchemas.type == json::Value::Type::Array)
			{
				for (const json::Value& t : toolSchemas.arrayValue)
				{
					const json::Value* schema = t.find("input_schema");
					openaiTools.push_back(json::makeObject({
						{"type", json::makeString("function")},
						{"function", json::makeObject({
							{"name",        t.find("name") ? *t.find("name") : json::makeString("")},
							{"description", t.find("description") ? *t.find("description") : json::makeString("")},
							{"parameters",  schema ? *schema : json::makeObject({})},
						})},
					}));
				}
			}

			std::vector<json::Value> messages;
			messages.push_back(json::makeObject({
				{"role",    json::makeString("system")},
				{"content", json::makeString(systemPrompt)},
			}));
			for (const LoopTurn& m : history)
			{
				if (m.role == "user")
				{
					messages.push_back(json::makeObject({
						{"role", json::makeString("user")},
						{"content", json::makeString(m.text)},
					}));
				}
				else if (m.role == "assistant")
				{
					std::vector<std::pair<std::string, json::Value>> fields = {
						{"role", json::makeString("assistant")},
						{"content", json::makeString(m.text)},
					};
					if (!m.toolUses.empty())
					{
						std::vector<json::Value> calls;
						for (const ParsedTurn::ToolUse& use : m.toolUses)
						{
							calls.push_back(json::makeObject({
								{"id", json::makeString(use.id)},
								{"type", json::makeString("function")},
								{"function", json::makeObject({
									{"name", json::makeString(use.name)},
									{"arguments", json::makeString(use.argsJson)},
								})},
							}));
						}
						fields.emplace_back("tool_calls", json::makeArray(std::move(calls)));
					}
					messages.push_back(json::makeObject(std::move(fields)));
				}
				else if (m.role == "tool")
				{
					messages.push_back(json::makeObject({
						{"role", json::makeString("tool")},
						{"tool_call_id", json::makeString(m.toolCallId)},
						{"content", json::makeString(m.text)},
					}));
				}
			}

			std::vector<std::pair<std::string, json::Value>> body = {
				{"model",       json::makeString(modelId)},
				{"messages",    json::makeArray(std::move(messages))},
				{"tools",       json::makeArray(std::move(openaiTools))},
				{"tool_choice", json::makeString("auto")},
			};
			if (supportsReasoningEffort && reasoningEffort >= 0)
			{
				static const char* const kLabels[] = { "minimal", "low", "medium", "high" };
				const int clamped = std::clamp(reasoningEffort, 0, 3);
				body.emplace_back("reasoning_effort", json::makeString(kLabels[clamped]));
			}
			return json::serialize(json::makeObject(std::move(body)));
		}

		ParsedTurn parseAnthropicTurn(const std::string& body)
		{
			ParsedTurn out;
			std::optional<json::Value> root = json::parse(body);
			if (!root.has_value())
			{
				out.errorMessage = "Response was not valid JSON.";
				return out;
			}
			if (const json::Value* err = root->find("error"))
			{
				if (const json::Value* msg = err->find("message"))
				{
					if (auto s = msg->asString()) out.errorMessage = *s;
				}
				if (out.errorMessage.empty()) out.errorMessage = "Unspecified Anthropic error.";
				return out;
			}
			if (const json::Value* stop = root->find("stop_reason"))
			{
				if (auto s = stop->asString()) out.stopReason = *s;
			}
			const json::Value* content = root->find("content");
			if (content == nullptr || content->type != json::Value::Type::Array)
			{
				return out;
			}
			for (const json::Value& block : content->arrayValue)
			{
				const json::Value* typeValue = block.find("type");
				const std::string type = typeValue ? readString(typeValue) : "";
				if (type == "text")
				{
					out.assistantText += readString(block.find("text"));
				}
				else if (type == "tool_use")
				{
					ParsedTurn::ToolUse use;
					use.id = readString(block.find("id"));
					use.name = readString(block.find("name"));
					if (const json::Value* input = block.find("input"))
					{
						use.argsJson = json::serialize(*input);
					}
					else
					{
						use.argsJson = "{}";
					}
					out.toolUses.push_back(std::move(use));
				}
			}
			return out;
		}

		ParsedTurn parseOpenAiTurn(const std::string& body)
		{
			ParsedTurn out;
			std::optional<json::Value> root = json::parse(body);
			if (!root.has_value())
			{
				out.errorMessage = "Response was not valid JSON.";
				return out;
			}
			if (const json::Value* err = root->find("error"))
			{
				if (const json::Value* msg = err->find("message"))
				{
					if (auto s = msg->asString()) out.errorMessage = *s;
				}
				if (out.errorMessage.empty()) out.errorMessage = "Unspecified OpenAI-compat error.";
				return out;
			}
			const json::Value* choices = root->find("choices");
			if (choices == nullptr || choices->type != json::Value::Type::Array || choices->arrayValue.empty())
			{
				out.errorMessage = "No choices in response.";
				return out;
			}
			const json::Value& choice = choices->arrayValue.front();
			if (const json::Value* finish = choice.find("finish_reason"))
			{
				if (auto s = finish->asString()) out.stopReason = *s;
			}
			const json::Value* message = choice.find("message");
			if (message == nullptr) return out;
			if (const json::Value* content = message->find("content"))
			{
				if (auto s = content->asString()) out.assistantText = *s;
			}
			if (const json::Value* toolCalls = message->find("tool_calls"))
			{
				if (toolCalls->type == json::Value::Type::Array)
				{
					for (const json::Value& call : toolCalls->arrayValue)
					{
						ParsedTurn::ToolUse use;
						use.id = readString(call.find("id"));
						const json::Value* fn = call.find("function");
						if (fn != nullptr)
						{
							use.name = readString(fn->find("name"));
							use.argsJson = readString(fn->find("arguments"));
							if (use.argsJson.empty()) use.argsJson = "{}";
						}
						out.toolUses.push_back(std::move(use));
					}
				}
			}
			return out;
		}
	}

	// -----------------------------------------------------------------------
	// Worker loop
	// -----------------------------------------------------------------------

	namespace
	{
		void runWorker(
			AICockpitState* statePtr,
			AIProviderClient* clientPtr,
			std::string providerId,
			std::string protocol,
			std::string modelId,
			int reasoningEffort,
			BlenderClient* blenderPtr,
			EditorScene* scenePtr,
			AICommandBus* busPtr,
			std::string userPrompt)
		{
			AICockpitState& state = *statePtr;
			AIProviderClient& client = *clientPtr;
			BlenderClient& blender = *blenderPtr;
			EditorScene& scene = *scenePtr;
			AICommandBus& bus = *busPtr;

			pushIncoming(state, {"user", userPrompt, "", false});

			std::vector<LoopTurn> localHistory;
			localHistory.push_back(LoopTurn{"user", userPrompt, {}, "", ""});

			json::Value sceneTools = sceneToolCatalog();
			std::vector<json::Value> allTools;
			if (sceneTools.type == json::Value::Type::Array)
			{
				for (const json::Value& t : sceneTools.arrayValue)
				{
					allTools.push_back(json::makeObject({
						{"name", json::makeString(toProviderToolName(readString(t.find("name"))))},
						{"description", t.find("description") ? *t.find("description") : json::makeString("")},
						{"input_schema", t.find("input_schema") ? *t.find("input_schema") : json::makeObject({})},
					}));
				}
			}

			(void)blender.ping();
			std::vector<BlenderClient::ToolInfo> blenderTools;
			const BlenderClient::Response listResponse = blender.listTools(blenderTools);
			if (!listResponse.ok)
			{
				pushIncoming(state, {"system",
					"Blender MCP not connected: " + listResponse.error +
					" Start Blender, enable the MCP addon, then Connect. Scene tools still work.",
					"", false});
			}
			for (const BlenderClient::ToolInfo& t : blenderTools)
			{
				json::Value schema = t.inputSchema.type == json::Value::Type::Object
					? t.inputSchema
					: json::makeObject({{"type", json::makeString("object")}});
				allTools.push_back(json::makeObject({
					{"name",         json::makeString(toProviderToolName("blender." + t.name))},
					{"description",  json::makeString("[Blender MCP] " + t.description)},
					{"input_schema", std::move(schema)},
				}));
			}
			const json::Value toolCatalog = json::makeArray(std::move(allTools));

			const std::string systemPrompt =
				"You are the GameForgerAI editor assistant. Call tools to do the work; do not only describe it. "
				"scene__* tools mutate the game scene. blender__* tools drive Blender through MCP "
				"(" + std::to_string(blenderTools.size()) + " Blender tools currently connected). "
				"Prefer blender tools for modelling, materials, and export. Prefer scene tools for placing "
				"objects in the game. To bring a Blender mesh into the game: export GLB under Game/Models/ "
				"then call scene__import_model. Tool names use double underscores instead of dots.";

			constexpr int kMaxTurns = 40;
			int turn = 0;
			bool supportsReasoningEffort = (reasoningEffort >= 0);

			while (turn < kMaxTurns && state.loopRunning.load())
			{
				++turn;

				std::string body;
				std::string overridePath;
				if (protocol == "anthropic")
				{
					body = buildAnthropicBody(modelId, localHistory, toolCatalog, systemPrompt);
				}
				else
				{
					body = buildOpenAiBody(modelId, localHistory, toolCatalog, systemPrompt,
						reasoningEffort, supportsReasoningEffort);
				}

				const AIProviderResponse response = client.sendRaw(providerId, body, overridePath);
				if (!response.success)
				{
					const std::string err = response.error.empty()
						? "Provider request failed."
						: response.error;
					pushIncoming(state, {"system", "Error: " + err, "", false});
					state.lastError = err;
					break;
				}

				ParsedTurn parsed = (protocol == "anthropic")
					? parseAnthropicTurn(response.body)
					: parseOpenAiTurn(response.body);

				if (!parsed.errorMessage.empty())
				{
					pushIncoming(state, {"system", "Error: " + parsed.errorMessage, "", false});
					state.lastError = parsed.errorMessage;
					break;
				}

				if (!parsed.assistantText.empty())
				{
					pushIncoming(state, {"assistant", parsed.assistantText, "", false});
				}

				if (parsed.toolUses.empty())
				{
					break;
				}

				LoopTurn assistantTurn;
				assistantTurn.role = "assistant";
				assistantTurn.text = parsed.assistantText;
				assistantTurn.toolUses = parsed.toolUses;
				localHistory.push_back(assistantTurn);

				for (const ParsedTurn::ToolUse& use : parsed.toolUses)
				{
					if (!state.loopRunning.load()) break;

					const std::string internalName = fromProviderToolName(use.name);
					const bool destructive = isDestructiveTool(internalName);
					const bool needsApproval = requiresApproval(state, internalName);

					pushIncoming(state, {"tool", "-> " + use.argsJson, internalName, destructive});

					if (needsApproval)
					{
						pushIncoming(state, {"system", "Awaiting approval for " + internalName + "...", "", false});
						const bool approved = waitForApproval(state, internalName, use.argsJson);
						if (!approved)
						{
							pushIncoming(state, {"system", "Tool call rejected by user.", "", false});
							LoopTurn rejected;
							rejected.role = "tool";
							rejected.toolCallId = use.id;
							rejected.toolName = use.name;
							rejected.text = "Rejected by the user.";
							localHistory.push_back(std::move(rejected));
							continue;
						}
					}

					ToolCallResult result;
					if (internalName.rfind("blender.", 0) == 0)
					{
						result = dispatchBlenderTool(blender, internalName, use.argsJson);
					}
					else if (internalName.rfind("scene.", 0) == 0)
					{
						std::optional<json::Value> parsedArgs = json::parse(use.argsJson);
						if (!parsedArgs.has_value())
						{
							result.ok = false;
							result.summary = "Could not parse tool arguments as JSON.";
						}
						else
						{
							result = dispatchSceneTool(scene, bus, internalName, *parsedArgs);
						}
					}
					else
					{
						result.ok = false;
						result.summary = "Unknown tool: " + internalName;
					}

					pushIncoming(state, {"tool", "<- " + result.summary, internalName, destructive});

					if (result.ok && result.undo)
					{
						AIActionEntry entry;
						entry.toolName = internalName;
						entry.argsJson = use.argsJson;
						entry.resultSummary = result.summary;
						entry.at = std::chrono::system_clock::now();
						entry.undo = std::move(result.undo);
						pushRingEntry(state, std::move(entry));
					}

					LoopTurn toolTurn;
					toolTurn.role = "tool";
					toolTurn.toolCallId = use.id;
					toolTurn.toolName = use.name;
					toolTurn.text = result.summary;
					localHistory.push_back(std::move(toolTurn));
				}
			}

			state.statusText = "Idle";
			state.loopRunning.store(false);
		}
	}

	void sendCockpitPrompt(
		AICockpitState& state,
		AIProviderClient& providerClient,
		const std::string& providerId,
		const std::string& providerProtocol,
		const std::string& modelId,
		int reasoningEffort,
		BlenderClient& blenderClient,
		EditorScene& scene,
		AICommandBus& commandBus,
		std::string userPrompt)
	{
		if (state.loopRunning.load()) return;
		joinCockpitWorker(state); // ensure previous worker fully cleaned up

		state.loopRunning.store(true);
		state.statusText = "Working...";
		state.lastError.clear();

		state.worker = std::thread(
			runWorker,
			&state,
			&providerClient,
			providerId,
			providerProtocol,
			modelId,
			reasoningEffort,
			&blenderClient,
			&scene,
			&commandBus,
			std::move(userPrompt));
	}
}
