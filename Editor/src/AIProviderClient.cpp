#include "GameForger/Editor/AIProviderClient.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>

#include <windows.h>
#include <winhttp.h>

#include "GameForger/Editor/Json.hpp"

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
				escaped += "\\u";
				escaped += hex[(byte >> 12) & 0x0F];
				escaped += hex[(byte >> 8) & 0x0F];
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
	}

	AIProviderClient::AIProviderClient(std::filesystem::path projectRoot)
		: projectRoot_(std::filesystem::absolute(std::move(projectRoot)))
	{
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

		const std::string body =
			"{\"model\":\"" + escapeJson(std::string(settings.model.begin(), settings.model.end())) +
			"\",\"messages\":[{\"role\":\"system\",\"content\":\"" +
			escapeJson(request.systemPrompt) + "\"},{\"role\":\"user\",\"content\":\"" +
			escapeJson(request.prompt) + "\"}],\"temperature\":0.2}";

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
		WinHttpSetTimeouts(session, 120000, 120000, 120000, 120000);
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

		const std::wstring headers = L"Content-Type: application/json\r\nAuthorization: Bearer " + settings.key;
		const BOOL sent = WinHttpSendRequest(
			requestHandle,
			headers.c_str(),
			// WinHttpSendRequest's 3rd arg is BYTE count, not wchar_t
			// count. Today the headers are pure ASCII ("Content-Type",
			// "Authorization: Bearer ..."), so size() happens to be the
			// right answer; the moment any header name gains a non-ASCII
			// character, the server will silently truncate. -1 (and a
			// null-terminated wide string) lets WinHTTP compute the
			// length itself.
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

		WinHttpCloseHandle(requestHandle);
		WinHttpCloseHandle(connection);
		WinHttpCloseHandle(session);
		if (!received)
		{
			return {false, 0, {}, "Provider request failed."};
		}
		std::string error;
		if (statusCode < 200 || statusCode >= 300)
		{
			error = "HTTP " + std::to_string(statusCode);
		}
		return {statusCode >= 200 && statusCode < 300, static_cast<long>(statusCode), responseBody, error};
	}
}
