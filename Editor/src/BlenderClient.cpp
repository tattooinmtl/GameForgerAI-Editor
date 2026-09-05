#include "GameForger/Editor/BlenderClient.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace gameforger::editor
{
	namespace
	{
		// The MCP server binds 127.0.0.1 with no transport security, and its
		// tool surface includes code.execute_python - arbitrary Python inside
		// Blender with the user's privileges. Any local process can reach that
		// port, so the shared secret is the only thing standing between a
		// stray process and RCE. Generated once per BlenderClient and handed
		// to the addon via BlenderLauncher's --python-expr, so the editor and
		// the Blender it spawns agree without the token ever touching disk.
		//
		// BCryptGenRandom, not std::random_device/mt19937: this is a security
		// token, and the standard library gives no portable guarantee about
		// the entropy source behind random_device.
		std::string generateBearerToken()
		{
			unsigned char bytes[32] = {};
			const NTSTATUS status = BCryptGenRandom(
				nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
			if (status != 0)
			{
				// Never fall back to a weak/predictable token - an empty one
				// makes the missing Authorization header obvious at the call
				// site instead of silently pretending to be authenticated.
				return {};
			}
			static constexpr char kHex[] = "0123456789abcdef";
			std::string token;
			token.reserve(sizeof(bytes) * 2);
			for (const unsigned char byte : bytes)
			{
				token += kHex[(byte >> 4) & 0x0F];
				token += kHex[byte & 0x0F];
			}
			return token;
		}
	}

	namespace
	{
		std::wstring widen(const std::string& value)
		{
			if (value.empty())
			{
				return {};
			}
			const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
			std::wstring result(static_cast<std::size_t>(size), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
			return result;
		}

		// Escape a UTF-8 string for inclusion inside a JSON string literal.
		// Same policy as AIProviderClient.cpp - control characters go through
		// as \uXXXX; the structural characters (\ and ") get \-prefixed. Any
		// other consumer that copies this style should stay in sync.
		std::string escapeJsonString(const std::string& value)
		{
			std::string escaped;
			escaped.reserve(value.size() + 16);
			auto appendHex4 = [&escaped](const unsigned char byte)
			{
				static constexpr char hex[] = "0123456789abcdef";
				escaped += "\\u00";
				escaped += hex[(byte >> 4) & 0x0F];
				escaped += hex[byte & 0x0F];
			};
			for (const unsigned char byte :
				 std::basic_string_view<unsigned char>(reinterpret_cast<const unsigned char*>(value.data()), value.size()))
			{
				switch (byte)
				{
					case '\\': escaped += "\\\\"; break;
					case '"':  escaped += "\\\""; break;
					case '\b': escaped += "\\b"; break;
					case '\f': escaped += "\\f"; break;
					case '\n': escaped += "\\n"; break;
					case '\r': escaped += "\\r"; break;
					case '\t': escaped += "\\t"; break;
					default:
						if (byte < 0x20)
						{
							appendHex4(byte);
						}
						else
						{
							escaped += static_cast<char>(byte);
						}
						break;
				}
			}
			return escaped;
		}

		// Serialised JSON literal for a JSON-RPC 2.0 envelope. `paramsJson`
		// must already be a JSON object literal (e.g. "{}", or {"foo":1}).
		std::string buildRpcBody(std::uint64_t id, const std::string& method, const std::string& paramsJson)
		{
			std::ostringstream body;
			body << R"({"jsonrpc":"2.0","id":)" << id
			     << R"(,"method":")" << escapeJsonString(method)
			     << R"(","params":)" << (paramsJson.empty() ? "{}" : paramsJson)
			     << "}";
			return body.str();
		}

		std::string buildRpcNotification(const std::string& method, const std::string& paramsJson)
		{
			std::ostringstream body;
			body << R"({"jsonrpc":"2.0","method":")" << escapeJsonString(method)
			     << R"(","params":)" << (paramsJson.empty() ? "{}" : paramsJson)
			     << "}";
			return body.str();
		}

		// Reads a JSON-RPC error object into a human-readable string.
		// Format: "<code>: <message>". Returns "" if `err` is not an error object.
		std::string extractJsonRpcError(const json::Value& err)
		{
			if (err.type != json::Value::Type::Object)
			{
				return {};
			}
			std::string message;
			double code = 0.0;
			if (const json::Value* messageValue = err.find("message"))
			{
				if (auto s = messageValue->asString())
				{
					message = *s;
				}
			}
			if (const json::Value* codeValue = err.find("code"))
			{
				if (auto n = codeValue->asNumber())
				{
					code = *n;
				}
			}
			if (message.empty() && code == 0.0)
			{
				return {};
			}
			std::ostringstream out;
			out << "JSON-RPC error " << static_cast<long>(code);
			if (!message.empty())
			{
				out << ": " << message;
			}
			return out.str();
		}

		// RAII wrapper so an early return doesn't leak the WinHTTP handles.
		struct HttpHandle
		{
			HINTERNET handle = nullptr;
			~HttpHandle() { if (handle != nullptr) WinHttpCloseHandle(handle); }
			HttpHandle() = default;
			explicit HttpHandle(HINTERNET h) : handle(h) {}
			HttpHandle(const HttpHandle&) = delete;
			HttpHandle& operator=(const HttpHandle&) = delete;
			HttpHandle(HttpHandle&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
			HttpHandle& operator=(HttpHandle&& other) noexcept
			{
				if (this != &other)
				{
					if (handle != nullptr) WinHttpCloseHandle(handle);
					handle = std::exchange(other.handle, nullptr);
				}
				return *this;
			}
		};
	}

	struct BlenderClient::CancelState
	{
		std::atomic<bool> abort{false};
		std::mutex mutex;
		std::vector<std::atomic<HINTERNET>*> slots;

		void add(std::atomic<HINTERNET>* slot)
		{
			std::lock_guard lock(mutex);
			if (abort.load())
			{
				HINTERNET handle = slot->exchange(nullptr);
				if (handle != nullptr)
				{
					WinHttpCloseHandle(handle);
				}
				return;
			}
			slots.push_back(slot);
		}

		void remove(std::atomic<HINTERNET>* slot)
		{
			std::lock_guard lock(mutex);
			slots.erase(std::remove(slots.begin(), slots.end(), slot), slots.end());
		}

		void abortAll()
		{
			abort.store(true);
			std::lock_guard lock(mutex);
			for (std::atomic<HINTERNET>* slot : slots)
			{
				HINTERNET handle = slot->exchange(nullptr);
				if (handle != nullptr)
				{
					WinHttpCloseHandle(handle);
				}
			}
			slots.clear();
		}

		struct Guard
		{
			CancelState& state;
			std::atomic<HINTERNET> handle{nullptr};

			Guard(CancelState& cancelState, HINTERNET request) : state(cancelState)
			{
				handle.store(request);
				state.add(&handle);
			}

			~Guard()
			{
				state.remove(&handle);
				HINTERNET remaining = handle.exchange(nullptr);
				if (remaining != nullptr)
				{
					WinHttpCloseHandle(remaining);
				}
			}

			Guard(const Guard&) = delete;
			Guard& operator=(const Guard&) = delete;

			[[nodiscard]] HINTERNET get() const noexcept { return handle.load(); }
		};
	};

	BlenderClient::BlenderClient(Config config)
		: config_(std::move(config))
		, cancel_(std::make_unique<CancelState>())
	{
		if (config_.bearerToken.empty())
		{
			config_.bearerToken = generateBearerToken();
		}
	}

	BlenderClient::~BlenderClient()
	{
		requestCancel();
		std::lock_guard lock(workersMutex_);
		for (auto& t : workers_)
		{
			if (t.joinable())
			{
				t.join();
			}
		}
	}

	void BlenderClient::requestCancel()
	{
		if (cancel_ != nullptr)
		{
			cancel_->abortAll();
		}
	}

	void BlenderClient::setConfig(Config config)
	{
		// Preserve the existing token rather than blanking it - a caller
		// changing the port shouldn't silently drop authentication on a
		// Blender that was already launched with the old token.
		std::string previousToken = std::move(config_.bearerToken);
		config_ = std::move(config);
		if (config_.bearerToken.empty())
		{
			config_.bearerToken =
				previousToken.empty() ? generateBearerToken() : std::move(previousToken);
		}
	}

	BlenderClient::Response BlenderClient::ping()
	{
		const std::string params =
			R"({"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"GameForgerAI-Editor","version":"0.79.0"}})";
		Response response = invoke("initialize", params);
		if (response.ok)
		{
			notify("notifications/initialized", "{}");
			return response;
		}
		// Server already initialized, or initialize is picky: tools/list is the
		// real proof the MCP HTTP port is usable.
		std::vector<ToolInfo> tools;
		Response list = listTools(tools);
		if (list.ok)
		{
			list.error.clear();
			return list;
		}
		return response;
	}

	BlenderClient::Response BlenderClient::listTools(std::vector<ToolInfo>& outTools)
	{
		outTools.clear();
		std::string cursor;
		Response last;
		for (int page = 0; page < 32; ++page)
		{
			const std::string params = cursor.empty()
				? "{}"
				: (std::string(R"({"cursor":")") + escapeJsonString(cursor) + "\"}");
			last = invoke("tools/list", params);
			if (!last.ok)
			{
				return last;
			}
			const json::Value* tools = last.result.find("tools");
			if (tools == nullptr || tools->type != json::Value::Type::Array)
			{
				last.ok = false;
				last.error = "tools/list result missing \"tools\" array.";
				return last;
			}
			for (const json::Value& entry : tools->arrayValue)
			{
				ToolInfo info;
				if (const json::Value* name = entry.find("name"))
				{
					if (auto s = name->asString()) info.name = *s;
				}
				if (const json::Value* description = entry.find("description"))
				{
					if (auto s = description->asString()) info.description = *s;
				}
				if (const json::Value* schema = entry.find("inputSchema"))
				{
					info.inputSchema = *schema;
				}
				else if (const json::Value* schemaAlt = entry.find("input_schema"))
				{
					info.inputSchema = *schemaAlt;
				}
				if (!info.name.empty())
				{
					outTools.push_back(std::move(info));
				}
			}
			const json::Value* next = last.result.find("nextCursor");
			if (next == nullptr)
			{
				break;
			}
			auto nextStr = next->asString();
			if (!nextStr || nextStr->empty())
			{
				break;
			}
			cursor = *nextStr;
		}
		last.ok = true;
		return last;
	}

	BlenderClient::Response BlenderClient::callTool(const std::string& toolName, const std::string& argumentsJson)
	{
		// MCP tools/call params: { "name": "...", "arguments": {...} }
		std::ostringstream params;
		params << R"({"name":")" << escapeJsonString(toolName) << R"(","arguments":)"
		       << (argumentsJson.empty() ? "{}" : argumentsJson) << "}";
		return invoke("tools/call", params.str());
	}

	void BlenderClient::listToolsAsync(Callback cb)
	{
		std::lock_guard lock(workersMutex_);
		workers_.emplace_back([this, callback = std::move(cb)]() mutable
		{
			std::vector<ToolInfo> unused;
			Response response = listTools(unused);
			enqueueCallback(std::move(response), std::move(callback));
		});
	}

	void BlenderClient::callToolAsync(std::string toolName, std::string argumentsJson, Callback cb)
	{
		std::lock_guard lock(workersMutex_);
		workers_.emplace_back([this, name = std::move(toolName), args = std::move(argumentsJson),
		                       callback = std::move(cb)]() mutable
		{
			Response response = callTool(name, args);
			enqueueCallback(std::move(response), std::move(callback));
		});
	}

	void BlenderClient::pumpMainThread()
	{
		std::deque<PendingCallback> drained;
		{
			std::lock_guard lock(queueMutex_);
			drained.swap(completedCallbacks_);
		}
		for (PendingCallback& pending : drained)
		{
			if (pending.callback)
			{
				pending.callback(std::move(pending.response));
			}
		}
	}

	void BlenderClient::enqueueCallback(Response response, Callback callback)
	{
		if (!callback)
		{
			return;
		}
		std::lock_guard lock(queueMutex_);
		completedCallbacks_.push_back({std::move(response), std::move(callback)});
	}

	void BlenderClient::notify(const std::string& method, const std::string& paramsJson)
	{
		(void)postHttp(buildRpcNotification(method, paramsJson));
	}

	BlenderClient::Response BlenderClient::postHttp(const std::string& body)
	{
		Response result;
		HttpHandle session(WinHttpOpen(
			L"GameForgerAI-Editor/BlenderClient",
			WINHTTP_ACCESS_TYPE_NO_PROXY,
			WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS,
			0));
		if (session.handle == nullptr)
		{
			result.error = "WinHttpOpen failed.";
			return result;
		}
		const DWORD timeoutMs = static_cast<DWORD>(config_.requestTimeout.count());
		WinHttpSetTimeouts(session.handle, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

		HttpHandle connection(WinHttpConnect(
			session.handle,
			widen(config_.host).c_str(),
			static_cast<INTERNET_PORT>(config_.port),
			0));
		if (connection.handle == nullptr)
		{
			result.error = "WinHttpConnect failed.";
			return result;
		}

		HINTERNET requestHandle = WinHttpOpenRequest(
			connection.handle,
			L"POST",
			widen(config_.path).c_str(),
			nullptr,
			WINHTTP_NO_REFERER,
			WINHTTP_DEFAULT_ACCEPT_TYPES,
			0); // No WINHTTP_FLAG_SECURE - MCP addon serves plain HTTP on loopback.
		if (requestHandle == nullptr)
		{
			result.error = "WinHttpOpenRequest failed.";
			return result;
		}

		CancelState::Guard inflight(*cancel_, requestHandle);
		requestHandle = inflight.get();
		if (requestHandle == nullptr || cancel_->abort.load())
		{
			result.error = "Cancelled.";
			return result;
		}

		std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json";
		if (!config_.bearerToken.empty())
		{
			headers += L"\r\nAuthorization: Bearer ";
			headers += widen(config_.bearerToken);
		}

		const BOOL sent = WinHttpSendRequest(
			requestHandle,
			headers.c_str(),
			static_cast<DWORD>(-1),
			const_cast<char*>(body.data()),
			static_cast<DWORD>(body.size()),
			static_cast<DWORD>(body.size()),
			0);
		if (!sent)
		{
			result.error = cancel_->abort.load()
				? "Cancelled."
				: "WinHttpSendRequest failed (is Blender running with the MCP server started?).";
			return result;
		}

		if (!WinHttpReceiveResponse(requestHandle, nullptr))
		{
			result.error = cancel_->abort.load() ? "Cancelled." : "WinHttpReceiveResponse failed.";
			return result;
		}

		DWORD statusCode = 0;
		DWORD statusSize = sizeof(statusCode);
		WinHttpQueryHeaders(
			requestHandle,
			WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			nullptr,
			&statusCode,
			&statusSize,
			nullptr);
		result.httpStatus = static_cast<long>(statusCode);

		std::string responseBody;
		DWORD available = 0;
		while (WinHttpQueryDataAvailable(requestHandle, &available) && available > 0)
		{
			std::string chunk(available, '\0');
			DWORD read = 0;
			if (!WinHttpReadData(requestHandle, chunk.data(), available, &read) || read == 0)
			{
				break;
			}
			responseBody.append(chunk.data(), read);
		}

		if (statusCode < 200 || statusCode >= 300)
		{
			std::ostringstream err;
			err << "HTTP " << statusCode;
			if (!responseBody.empty())
			{
				err << ": " << responseBody;
			}
			result.error = err.str();
			return result;
		}

		if (responseBody.empty())
		{
			result.ok = true;
			return result;
		}

		std::optional<json::Value> parsed = json::parse(responseBody);
		if (!parsed.has_value())
		{
			result.error = "Response body was not valid JSON.";
			return result;
		}

		if (const json::Value* err = parsed->find("error"))
		{
			std::string message = extractJsonRpcError(*err);
			result.error = message.empty() ? "JSON-RPC returned an unspecified error." : std::move(message);
			return result;
		}

		if (const json::Value* rpcResult = parsed->find("result"))
		{
			result.result = *rpcResult;
			result.ok = true;
			return result;
		}

		result.ok = true;
		result.result = *parsed;
		return result;
	}

	BlenderClient::Response BlenderClient::invoke(const std::string& method, const std::string& paramsJson)
	{
		const std::uint64_t id = nextRequestId_.fetch_add(1, std::memory_order_relaxed);
		return postHttp(buildRpcBody(id, method, paramsJson));
	}
}
