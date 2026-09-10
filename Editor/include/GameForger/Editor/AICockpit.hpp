#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "GameForger/Editor/AICommandBus.hpp"
#include "GameForger/Editor/ProjectSettingsBus.hpp"
#include "GameForger/Editor/BlenderClient.hpp"
#include "GameForger/Editor/EditorScene.hpp"
#include "GameForger/Editor/Json.hpp"

namespace gameforger::editor
{
	class AIProviderClient;

	// Phase C.5 - bounded ring of AI-initiated actions. The Undo button beside
	// the prompt pops the newest entry and dispatches its undo lambda. Ring is
	// scene-lifetime; cleared on scene load/new.
	struct AIActionEntry
	{
		std::string toolName;
		std::string argsJson;
		std::string resultSummary;
		std::chrono::system_clock::time_point at;
		// Undo callback. For scene ops, wraps a bus.execute(inverse) call.
		// For Blender ops, runs `code.execute_python "bpy.ops.ed.undo()"`.
		std::function<void()> undo;
	};

	class AIActionRing final
	{
	public:
		static constexpr std::size_t kCapacity = 10;

		void push(AIActionEntry entry);
		bool undoLast();
		void clear();
		[[nodiscard]] std::size_t size() const { return entries_.size(); }
		[[nodiscard]] const std::deque<AIActionEntry>& entries() const { return entries_; }

	private:
		std::deque<AIActionEntry> entries_;
	};

	// Phase C.4 - message shown in the chat log or the tool trace. `role` is
	// "user" / "assistant" / "tool" / "system"; `toolName` is filled for tool
	// traces so the UI can render them with an icon and expand/collapse.
	struct CockpitChatMessage
	{
		std::string role;
		std::string content;
		std::string toolName;
		bool isDestructive = false;
	};

	// Phase C.6 - a tool call waiting on the user's Approve/Reject. The
	// agentic loop sets this and blocks on `approvalGate` until the main
	// thread wakes it via approveDestructive() / rejectDestructive().
	struct PendingApproval
	{
		std::string toolName;
		std::string argsJson;
	};

	// Phase C - overall cockpit state, one instance owned by main().
	struct AICockpitState
	{
		bool panelOpen = false;

		// User input. Cleared after Send.
		std::array<char, 4096> promptInput{};

		// Full conversation for display. Also functions as the message log
		// that gets shipped back to the provider on each turn.
		std::vector<CockpitChatMessage> messages;

		// Ring buffer of undo-able actions.
		AIActionRing undoRing;

		// UI toggles.
		bool autonomousMode = false;              // If false, EVERY tool call requires approval.
		bool approveDestructiveAutomatically = false;

		// Loop status.
		std::atomic<bool> loopRunning{false};
		std::string statusText;
		std::string lastError;

		// Destructive-op approval gate.
		std::mutex approvalMutex;
		std::condition_variable approvalGate;
		std::optional<PendingApproval> pendingApproval;
		// tri-state: 0=waiting, 1=approve, 2=reject. Reset before each pending.
		std::atomic<int> approvalDecision{0};

		// Cross-thread message queue: worker thread pushes CockpitChatMessage
		// entries; main thread drains them in pumpCockpit() so the chat log
		// always mutates from a single thread.
		std::mutex incomingMutex;
		std::deque<CockpitChatMessage> incoming;
		std::deque<AIActionEntry> pendingRingEntries;

		// Worker thread (owning). Joined in the destructor via joinWorker().
		std::thread worker;
	};

	// Join worker safely (called from AICockpitState's owner on shutdown).
	void joinCockpitWorker(AICockpitState& state);

	// Called once per ImGui frame BEFORE drawing the cockpit panel so any
	// queued messages/undo entries land on the main thread.
	void pumpCockpit(AICockpitState& state);

	// Approval controls the main thread calls from the panel.
	void approveDestructive(AICockpitState& state);
	void rejectDestructive(AICockpitState& state);

	// Fire off a single user prompt. Spawns the worker thread if not already
	// running. Returns immediately.
	//
	// The worker builds a tool catalog from (a) the hardcoded scene tools and
	// (b) blenderClient.listTools() (if connected), sends the initial prompt
	// to `providerId` via `providerClient`, and iterates up to 8 tool_use
	// turns before returning the model's final text response.
	void sendCockpitPrompt(
		AICockpitState& state,
		AIProviderClient& providerClient,
		const std::string& providerId,
		const std::string& providerProtocol,   // "anthropic" or "openai-compatible"
		const std::string& modelId,
		int reasoningEffort,                   // 0..3 or -1 if unsupported
		BlenderClient& blenderClient,
		EditorScene& scene,
		AICommandBus& commandBus,
		// Project-level config (Game/Project.json + Game/Settings.json). The
		// project.* tools route here rather than to commandBus: these settings
		// outlive scene loads, and the bus validates before anything reaches
		// disk so the model cannot write a broken config.
		ProjectSettingsBus& projectSettingsBus,
		std::string userPrompt);

	// Emergency stop for a running loop. Sets a flag the worker checks
	// between turns; won't interrupt an in-flight HTTP request.
	void requestCockpitStop(AICockpitState& state);

	// Test-only helpers exposed for the panel:
	[[nodiscard]] bool isDestructiveTool(const std::string& toolName);
	[[nodiscard]] json::Value sceneToolCatalog();
}
