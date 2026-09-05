#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "GameForger/Editor/Json.hpp"

namespace gameforger::editor
{
	// Talks JSON-RPC 2.0 over HTTP to the Blender MCP addon
	// (https://github.com/tattooinmtl/MCP_Server_blender) running in-process
	// inside a spawned Blender. Default endpoint: http://127.0.0.1:8765/mcp.
	//
	// Transport: WinHTTP, matching AIProviderClient.cpp - the project already
	// links winhttp.lib and we deliberately avoid pulling in a second HTTP
	// stack (cpp-httplib etc.) for one Windows-only consumer.
	//
	// Async model: send() runs synchronously on the caller's thread. sendAsync()
	// dispatches to a detached worker thread and stashes the result in an
	// internal queue; the main (ImGui) thread pumps that queue once per frame
	// via pumpMainThread() so callbacks run on the same thread that owns the
	// UI - never inside the worker.
	class BlenderClient final
	{
	public:
		struct Config
		{
			std::string host = "127.0.0.1";
			int port = 8765;
			// Shared secret, sent as "Authorization: Bearer <token>". Leave
			// empty and the constructor fills it with 32 bytes of
			// BCryptGenRandom entropy; BlenderLauncher then hands the same
			// value to the addon, so an editor-launched Blender is
			// authenticated end to end. Only ever empty if the OS RNG failed.
			std::string bearerToken;
			std::chrono::milliseconds requestTimeout{15000};
			// URL path for the MCP endpoint. The upstream addon serves at "/mcp".
			std::string path = "/mcp";
		};

		struct ToolInfo
		{
			std::string name;
			std::string description;
			json::Value inputSchema;
		};

		struct Response
		{
			bool ok = false;
			long httpStatus = 0;
			// Parsed JSON-RPC "result" on success; empty on error.
			json::Value result;
			// Human-readable error on failure (transport error, HTTP != 2xx,
			// or JSON-RPC "error" object). Empty on success.
			std::string error;
		};

		using Callback = std::function<void(Response)>;

		explicit BlenderClient(Config config = {});
		~BlenderClient();

		BlenderClient(const BlenderClient&) = delete;
		BlenderClient& operator=(const BlenderClient&) = delete;

		[[nodiscard]] const Config& config() const noexcept { return config_; }
		void setConfig(Config config);

		// MCP handshake: JSON-RPC `initialize` then `notifications/initialized`.
		// If the server was already initialized (second Connect), falls back to
		// `tools/list` and treats a successful list as "up". Does not require
		// the editor to have spawned blender.exe — any process on this port.
		[[nodiscard]] Response ping();

		// JSON-RPC "tools/list". Returns parsed ToolInfo entries on success.
		[[nodiscard]] Response listTools(std::vector<ToolInfo>& outTools);

		// JSON-RPC "tools/call". `argumentsJson` must be a serialised JSON
		// object literal (e.g. R"({"code":"bpy.ops.mesh.primitive_cube_add()"})").
		// Kept as a raw string for now because json::Value has no serialiser
		// yet; add one in Phase C when tool-router args become arbitrary.
		[[nodiscard]] Response callTool(const std::string& toolName, const std::string& argumentsJson);

		// Non-blocking equivalents. The callback fires on the main thread the
		// next time pumpMainThread() runs after the worker completes.
		void listToolsAsync(Callback cb);
		void callToolAsync(std::string toolName, std::string argumentsJson, Callback cb);

		// Drain completed async callbacks. Call once per ImGui frame.
		void pumpMainThread();

		// Abort in-flight HTTP (Connect / tools/list / tools/call) so shutdown
		// join/detach does not wait on the request timeout.
		void requestCancel();

	private:
		struct PendingCallback
		{
			Response response;
			Callback callback;
		};

		// Underlying request. `method` is the JSON-RPC method ("tools/list" etc.),
		// `paramsJson` is a serialised JSON object literal (or "{}" for no params).
		[[nodiscard]] Response invoke(const std::string& method, const std::string& paramsJson);

		// JSON-RPC notification (no `id`). Used for `notifications/initialized`.
		void notify(const std::string& method, const std::string& paramsJson);

		[[nodiscard]] Response postHttp(const std::string& body);

		void enqueueCallback(Response response, Callback callback);

		struct CancelState;
		Config config_;
		std::atomic<std::uint64_t> nextRequestId_{1};
		std::unique_ptr<CancelState> cancel_;

		std::mutex queueMutex_;
		std::deque<PendingCallback> completedCallbacks_;

		// Tracks in-flight worker threads so the destructor can join them.
		std::mutex workersMutex_;
		std::vector<std::thread> workers_;
	};
}
