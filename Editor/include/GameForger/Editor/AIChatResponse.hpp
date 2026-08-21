#pragma once

#include <optional>
#include <string>

namespace gameforger::editor
{
	// Extracts the assistant message "content" string from an OpenAI-compatible
	// chat completion JSON response body, decoding standard JSON escapes so
	// multi-line code with embedded quotes survives intact.
	[[nodiscard]] std::optional<std::string> extractChatMessageContent(const std::string& jsonBody);

	// Best-effort human-readable error extracted from a failed provider response.
	[[nodiscard]] std::string extractErrorMessage(const std::string& jsonBody);
}
