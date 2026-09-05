#include "GameForger/Editor/AIProviderClient.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

#include "GameForger/Editor/Json.hpp"

#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace gameforger::editor
{
	namespace
	{
		struct ProviderSettings
		{
			std::wstring host;
			std::wstring path;
			std::wstring model;
			std::wstring key;
			// "anthropic" | "openai-compatible" | "custom", straight from
			// Providers.json. Defaults to openai-compatible when the field is
			// absent, which is what every provider in the shipped file except
			// Anthropic itself uses.
			std::string protocol = "openai-compatible";
		};

		std::string readFile(const std::filesystem::path& path)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
			{
				return {};
			}
			std::ostringstream contents;
			contents << input.rdbuf();
			return contents.str();
		}

		constexpr int kHttpTimeoutMs = 30000;

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

		std::string narrow(const std::wstring& value)
		{
			if (value.empty())
			{
				return {};
			}
			const int size = WideCharToMultiByte(
				CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
			std::string result(static_cast<std::size_t>(size), '\0');
			WideCharToMultiByte(
				CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
			return result;
		}

		// Escape for inclusion as a JSON string literal. Properly escapes every
		// control character (U+0000-U+001F) and the structural characters
		// (backslash, double-quote). The previous version silently dropped \r
		// and emitted \t / \b / \f / \0 raw, producing invalid JSON that
		// strict provider parsers reject.
		std::string escapeJson(const std::string& value)
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

		// Walk the structured Providers.json document and resolve the
		// settings for `providerId` from the matching object alone. The
		// previous implementation used find("\"id\": \"<id>\"") and then
		// substring-searched for the next "endpoint" / "model" / ... without
		// respecting object boundaries - any value in any provider containing
		// those substrings would leak into the resolved config.
		bool parseProvider(
			const std::string& configuration,
			const std::string& providerId,
			const std::string& localSecrets,
			ProviderSettings& settings)
		{
			const std::optional<json::Value> root = json::parse(configuration);
			if (!root.has_value() || root->type != json::Value::Type::Object)
			{
				return false;
			}
			const json::Value* providersValue = root->find("providers");
			if (providersValue == nullptr || providersValue->type != json::Value::Type::Array)
			{
				return false;
			}
			const json::Value* match = nullptr;
			for (const json::Value& provider : providersValue->arrayValue)
			{
				if (provider.type != json::Value::Type::Object)
				{
					continue;
				}
				const json::Value* id = provider.find("id");
				if (id == nullptr || id->type != json::Value::Type::String || id->stringValue != providerId)
				{
					continue;
				}
				match = &provider;
				break;
			}
			if (match == nullptr)
			{
				return false;
			}
			const json::Value* endpointValue = match->find("endpoint");
			const json::Value* modelValue = match->find("model");
			const json::Value* keyNameValue = match->find("apiKeyEnvironmentVariable");
			if (endpointValue == nullptr || endpointValue->type != json::Value::Type::String ||
				modelValue == nullptr || modelValue->type != json::Value::Type::String ||
				keyNameValue == nullptr || keyNameValue->type != json::Value::Type::String)
			{
				return false;
			}
			const std::string& endpoint = endpointValue->stringValue;
			const std::string& model = modelValue->stringValue;
			const std::string& keyName = keyNameValue->stringValue;
			const std::size_t scheme = endpoint.find("://");
			const std::size_t pathStart = endpoint.find('/', scheme == std::string::npos ? 0 : scheme + 3);
			if (scheme == std::string::npos || pathStart == std::string::npos || model.empty() || keyName.empty())
			{
				return false;
			}
			const std::size_t hostStart = scheme + 3;
			settings.host = widen(endpoint.substr(hostStart, pathStart - hostStart));
			settings.path = widen(endpoint.substr(pathStart));
			settings.model = widen(model);
			if (const json::Value* protocolValue = match->find("protocol");
				protocolValue != nullptr && protocolValue->type == json::Value::Type::String &&
				!protocolValue->stringValue.empty())
			{
				settings.protocol = protocolValue->stringValue;
			}

			// Local secrets file uses the same JSON shape: an object whose
			// keys are environment variable names and whose values are the
			// matching keys. Empty file / file without this key is fine -
			// we then fall back to the OS environment.
			std::string localKey;
			if (const std::optional<json::Value> secrets = json::parse(localSecrets); secrets.has_value())
			{
				if (const json::Value* keyValue = secrets->find(keyName);
					keyValue != nullptr && keyValue->type == json::Value::Type::String)
				{
					localKey = keyValue->stringValue;
				}
			}
			if (!localKey.empty())
			{
				settings.key = widen(localKey);
			}
			else if (const char* key = std::getenv(keyName.c_str()); key != nullptr)
			{
				settings.key = widen(key);
			}
			return !settings.host.empty() && !settings.path.empty() && !settings.key.empty();
		}

		// Anthropic caps generation with a REQUIRED max_tokens; OpenAI-compatible
		// providers default it server-side. 4096 is comfortably above what any
		// send() caller asks for (a script, an animation clip, a command plan).
		constexpr int kAnthropicMaxTokens = 4096;

		std::string buildRequestBody(const ProviderSettings& settings, const AIProviderRequest& request)
		{
			const std::string model = escapeJson(narrow(settings.model));
			if (settings.protocol == "anthropic")
			{
				std::string body = "{\"model\":\"" + model +
					"\",\"max_tokens\":" + std::to_string(kAnthropicMaxTokens);
				if (!request.systemPrompt.empty())
				{
					body += ",\"system\":\"" + escapeJson(request.systemPrompt) + "\"";
				}
				body += ",\"messages\":[{\"role\":\"user\",\"content\":\"" +
					escapeJson(request.prompt) + "\"}],\"temperature\":0.2}";
				return body;
			}
			return "{\"model\":\"" + model +
				"\",\"messages\":[{\"role\":\"system\",\"content\":\"" +
				escapeJson(request.systemPrompt) + "\"},{\"role\":\"user\",\"content\":\"" +
				escapeJson(request.prompt) + "\"}],\"temperature\":0.2}";
		}

		// Both auth schemes on every request, matching what sendRaw() has always
		// done: Anthropic reads x-api-key + anthropic-version, OpenAI-compatible
		// providers read Authorization. Unknown headers are ignored, so this
		// costs nothing and removes a way for the two paths to disagree.
		std::wstring buildRequestHeaders(const ProviderSettings& settings)
		{
			return
				L"Content-Type: application/json\r\n"
				L"Accept: application/json\r\n"
				L"Authorization: Bearer " + settings.key + L"\r\n"
				L"x-api-key: " + settings.key + L"\r\n"
				L"anthropic-version: 2023-06-01";
		}
	}

	struct AIProviderClient::CancelState
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

	AIProviderClient::AIProviderClient(std::filesystem::path projectRoot)
		: projectRoot_(std::filesystem::absolute(std::move(projectRoot)))
		, cancel_(std::make_unique<CancelState>())
	{
	}

	AIProviderClient::~AIProviderClient()
	{
		requestCancel();
	}

	void AIProviderClient::requestCancel()
	{
		if (cancel_ != nullptr)
		{
			cancel_->abortAll();
		}
	}

	AIProviderResponse AIProviderClient::send(
		const std::string& providerId,
		const AIProviderRequest& request) const
	{
		ProviderSettings settings;
		const std::string configuration = readFile(projectRoot_ / "Game/AI/Providers.json");
		const std::string localSecrets = readFile(projectRoot_ / "Game/AI/Providers.local.json");
		if (configuration.empty() || !parseProvider(configuration, providerId, localSecrets, settings))
		{
			return {false, 0, {}, "Provider configuration or API key is unavailable."};
		}

		// Anthropic's Messages API is NOT OpenAI-shaped: the system prompt is a
		// top-level "system" field rather than a messages[] entry, and
		// "max_tokens" is required. Sending the OpenAI body here used to make
		// every send() caller (ScriptGenerator, AICommandPlanner,
		// AIAnimationGenerator, the Calibrate button) fail against the DEFAULT
		// provider, which is Anthropic. sendRaw() has always branched on
		// protocol; send() now does the same.
		const std::string body = buildRequestBody(settings, request);

		HINTERNET session = WinHttpOpen(
			L"GameForgerAIEditor/0.1",
			WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
			WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS,
			0);
		if (session == nullptr)
		{
			return {false, 0, {}, "Could not initialize WinHTTP."};
		}
		WinHttpSetTimeouts(session, kHttpTimeoutMs, kHttpTimeoutMs, kHttpTimeoutMs, kHttpTimeoutMs);
		HINTERNET connection = WinHttpConnect(session, settings.host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
		HINTERNET requestHandle = connection == nullptr
			? nullptr
			: WinHttpOpenRequest(connection, L"POST", settings.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
		if (requestHandle == nullptr)
		{
			if (connection != nullptr) WinHttpCloseHandle(connection);
			WinHttpCloseHandle(session);
			return {false, 0, {}, "Could not open the provider request."};
		}

		CancelState::Guard inflight(*cancel_, requestHandle);
		requestHandle = inflight.get();
		if (requestHandle == nullptr || cancel_->abort.load())
		{
			if (connection != nullptr) WinHttpCloseHandle(connection);
			WinHttpCloseHandle(session);
			return {false, 0, {}, "Cancelled."};
		}

		const std::wstring headers = buildRequestHeaders(settings);
		const BOOL sent = WinHttpSendRequest(
			requestHandle,
			headers.c_str(),
			// WinHttpSendRequest's 3rd arg is BYTE count, not wchar_t
			// count. -1 (with a null-terminated wide string) lets WinHTTP
			// compute the length itself, which stays correct if any header
			// ever gains a non-ASCII character.
			static_cast<DWORD>(-1),
			const_cast<char*>(body.data()),
			static_cast<DWORD>(body.size()),
			static_cast<DWORD>(body.size()),
			0);
		const BOOL received = sent && WinHttpReceiveResponse(requestHandle, nullptr);
		DWORD statusCode = 0;
		DWORD statusSize = sizeof(statusCode);
		if (received)
		{
			WinHttpQueryHeaders(
				requestHandle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				nullptr, &statusCode, &statusSize, nullptr);
		}

		std::string responseBody;
		if (received)
		{
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
		}

		WinHttpCloseHandle(connection);
		WinHttpCloseHandle(session);
		if (!received)
		{
			return {false, 0, {}, cancel_->abort.load() ? "Cancelled." : "Provider request failed."};
		}
		std::string error;
		if (statusCode < 200 || statusCode >= 300)
		{
			error = "HTTP " + std::to_string(statusCode);
		}
		return {statusCode >= 200 && statusCode < 300, static_cast<long>(statusCode), responseBody, error};
	}

	AIProviderResponse AIProviderClient::sendRaw(
		const std::string& providerId,
		const std::string& body,
		const std::string& overrideEndpointPath) const
	{
		ProviderSettings settings;
		const std::string configuration = readFile(projectRoot_ / "Game/AI/Providers.json");
		const std::string localSecrets = readFile(projectRoot_ / "Game/AI/Providers.local.json");
		if (configuration.empty() || !parseProvider(configuration, providerId, localSecrets, settings))
		{
			return {false, 0, {}, "Provider configuration or API key is unavailable."};
		}

		const std::wstring path = overrideEndpointPath.empty()
			? settings.path
			: widen(overrideEndpointPath);

		HINTERNET session = WinHttpOpen(
			L"GameForgerAIEditor/Cockpit",
			WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
			WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (session == nullptr) return {false, 0, {}, "Could not initialize WinHTTP."};
		WinHttpSetTimeouts(session, kHttpTimeoutMs, kHttpTimeoutMs, kHttpTimeoutMs, kHttpTimeoutMs);
		HINTERNET connection = WinHttpConnect(session, settings.host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
		HINTERNET request = connection == nullptr ? nullptr : WinHttpOpenRequest(
			connection, L"POST", path.c_str(), nullptr,
			WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
		if (request == nullptr)
		{
			if (connection != nullptr) WinHttpCloseHandle(connection);
			WinHttpCloseHandle(session);
			return {false, 0, {}, "Could not open the provider request."};
		}

		CancelState::Guard inflight(*cancel_, request);
		request = inflight.get();
		if (request == nullptr || cancel_->abort.load())
		{
			if (connection != nullptr) WinHttpCloseHandle(connection);
			WinHttpCloseHandle(session);
			return {false, 0, {}, "Cancelled."};
		}

		const std::wstring headers = buildRequestHeaders(settings);
		const BOOL sent = WinHttpSendRequest(
			request, headers.c_str(), static_cast<DWORD>(-1),
			const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
			static_cast<DWORD>(body.size()), 0);
		const BOOL received = sent && WinHttpReceiveResponse(request, nullptr);
		DWORD statusCode = 0;
		DWORD statusSize = sizeof(statusCode);
		if (received)
		{
			WinHttpQueryHeaders(request,
				WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				nullptr, &statusCode, &statusSize, nullptr);
		}
		std::string responseBody;
		if (received)
		{
			DWORD available = 0;
			while (WinHttpQueryDataAvailable(request, &available) && available > 0)
			{
				std::string chunk(available, '\0');
				DWORD read = 0;
				if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0) break;
				responseBody.append(chunk.data(), read);
			}
		}
		WinHttpCloseHandle(connection);
		WinHttpCloseHandle(session);
		if (!received) return {false, 0, {}, cancel_->abort.load() ? "Cancelled." : "Provider request failed."};
		std::string error;
		if (statusCode < 200 || statusCode >= 300)
		{
			error = "HTTP " + std::to_string(statusCode);
		}
		return {statusCode >= 200 && statusCode < 300, static_cast<long>(statusCode), responseBody, error};
	}

	// Phase B.3 helpers.
	namespace
	{
		// Turn "/v1/chat/completions" into "/v1/models". Also handles
		// providers whose endpoint already ends in /messages (Anthropic).
		// If we can't confidently derive a models path, returns an empty
		// wstring so the caller returns "not supported" rather than making
		// a bogus request.
		std::wstring deriveModelsPath(const std::wstring& chatPath)
		{
			// Locate the last /vN/ segment; the /models leaf sits under it.
			auto lastSlash = chatPath.find_last_of(L'/');
			if (lastSlash == std::wstring::npos)
			{
				return L"";
			}
			// Special-case Anthropic-native /v1/messages: their models are a
			// static list, not enumerable; return empty to signal not-supported.
			if (chatPath.rfind(L"/messages") == chatPath.size() - 9)
			{
				return L"";
			}
			return chatPath.substr(0, lastSlash + 1) + L"models";
		}
	}

	AIProviderClient::DiscoverResult AIProviderClient::discoverModels(const std::string& providerId) const
	{
		DiscoverResult result;

		ProviderSettings settings;
		const std::string configuration = readFile(projectRoot_ / "Game/AI/Providers.json");
		const std::string localSecrets = readFile(projectRoot_ / "Game/AI/Providers.local.json");
		if (configuration.empty() || !parseProvider(configuration, providerId, localSecrets, settings))
		{
			result.error = "Provider configuration or API key is unavailable.";
			return result;
		}

		const std::wstring modelsPath = deriveModelsPath(settings.path);
		if (modelsPath.empty())
		{
			result.error = "This provider does not expose a discoverable model list.";
			return result;
		}

		HINTERNET session = WinHttpOpen(
			L"GameForgerAIEditor/DiscoverModels",
			WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
			WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (session == nullptr)
		{
			result.error = "Could not initialize WinHTTP.";
			return result;
		}
		WinHttpSetTimeouts(session, kHttpTimeoutMs, kHttpTimeoutMs, kHttpTimeoutMs, kHttpTimeoutMs);
		HINTERNET connection = WinHttpConnect(session, settings.host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
		HINTERNET request = connection == nullptr ? nullptr : WinHttpOpenRequest(
			connection, L"GET", modelsPath.c_str(), nullptr,
			WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
		if (request == nullptr)
		{
			if (connection != nullptr) WinHttpCloseHandle(connection);
			WinHttpCloseHandle(session);
			result.error = "Could not open the models request.";
			return result;
		}

		CancelState::Guard inflight(*cancel_, request);
		request = inflight.get();
		if (request == nullptr || cancel_->abort.load())
		{
			if (connection != nullptr) WinHttpCloseHandle(connection);
			WinHttpCloseHandle(session);
			result.error = "Cancelled.";
			return result;
		}

		const std::wstring headers = L"Authorization: Bearer " + settings.key + L"\r\nAccept: application/json";
		const BOOL sent = WinHttpSendRequest(
			request, headers.c_str(), static_cast<DWORD>(-1),
			WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
		const BOOL received = sent && WinHttpReceiveResponse(request, nullptr);
		DWORD statusCode = 0;
		DWORD statusSize = sizeof(statusCode);
		if (received)
		{
			WinHttpQueryHeaders(request,
				WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				nullptr, &statusCode, &statusSize, nullptr);
		}
		std::string body;
		if (received)
		{
			DWORD available = 0;
			while (WinHttpQueryDataAvailable(request, &available) && available > 0)
			{
				std::string chunk(available, '\0');
				DWORD read = 0;
				if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0) break;
				body.append(chunk.data(), read);
			}
		}
		WinHttpCloseHandle(connection);
		WinHttpCloseHandle(session);

		if (!received)
		{
			result.error = cancel_->abort.load() ? "Cancelled." : "Models request failed.";
			return result;
		}
		if (statusCode < 200 || statusCode >= 300)
		{
			result.error = "HTTP " + std::to_string(statusCode);
			return result;
		}

		// OpenAI-compatible schema: { "data": [ {"id": "...", ...}, ... ] }
		std::optional<json::Value> parsed = json::parse(body);
		if (!parsed.has_value())
		{
			result.error = "Response body was not valid JSON.";
			return result;
		}
		const json::Value* data = parsed->find("data");
		if (data == nullptr || data->type != json::Value::Type::Array)
		{
			result.error = "Response did not contain a \"data\" array.";
			return result;
		}
		result.models.reserve(data->arrayValue.size());
		for (const json::Value& entry : data->arrayValue)
		{
			if (entry.type != json::Value::Type::Object) continue;
			AIModelInfo info;
			if (const json::Value* id = entry.find("id"))
			{
				if (auto s = id->asString()) info.id = *s;
			}
			if (info.id.empty()) continue;
			info.displayName = info.id;
			result.models.push_back(std::move(info));
		}
		return result;
	}

	// Phase B.5. Keyword-scored ranker - kept in one function so tweaking
	// the weights is a single-file change. The exact scores are less
	// important than the ordering: reasoning/tool-capable/code-focused
	// models bubble above chat-only defaults.
	void rankModelsByRelevance(std::vector<AIModelInfo>& models)
	{
		const auto containsCi = [](const std::string& haystack, const std::string& needle) -> bool
		{
			if (needle.empty() || haystack.size() < needle.size()) return false;
			for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i)
			{
				bool match = true;
				for (std::size_t j = 0; j < needle.size(); ++j)
				{
					char h = haystack[i + j];
					char n = needle[j];
					if (h >= 'A' && h <= 'Z') h = static_cast<char>(h - 'A' + 'a');
					if (n >= 'A' && n <= 'Z') n = static_cast<char>(n - 'A' + 'a');
					if (h != n) { match = false; break; }
				}
				if (match) return true;
			}
			return false;
		};

		for (AIModelInfo& m : models)
		{
			int score = 0;
			const std::string& s = m.id;
			if (containsCi(s, "tool") || containsCi(s, "function") || containsCi(s, "agent")) score += 3;
			if (containsCi(s, "vision") || containsCi(s, "multimodal") || containsCi(s, "-mm-") || containsCi(s, "-v-")) score += 2;
			if (containsCi(s, "code") || containsCi(s, "coder")) score += 2;
			if (containsCi(s, "reasoning") || containsCi(s, "thinking") || containsCi(s, "-o1") || containsCi(s, "-o3")) score += 1;
			// Small penalty for plainly-chat/instruct models with no other boost.
			if ((containsCi(s, "chat") || containsCi(s, "instruct")) && score == 0) score -= 2;
			// Big-name flagships get a small nudge so they land above generic entries.
			if (containsCi(s, "opus") || containsCi(s, "gpt-4o") || containsCi(s, "sonnet") || containsCi(s, "kimi") || containsCi(s, "nemotron")) score += 1;
			m.relevanceRank = score;
		}
		std::sort(models.begin(), models.end(),
			[](const AIModelInfo& a, const AIModelInfo& b)
			{
				if (a.relevanceRank != b.relevanceRank) return a.relevanceRank > b.relevanceRank;
				return a.id < b.id;
			});
	}
}
