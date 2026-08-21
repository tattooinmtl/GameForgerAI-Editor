#pragma once

#include <string>

#include "GameForger/Editor/AIProviderClient.hpp"

namespace gameforger::editor
{
	struct ScriptGenerationResult
	{
		bool success = false;
		// Lua source on success, human-readable error message on failure.
		std::string content;
	};

	// Blocking call — the caller is responsible for running this off the UI thread.
	[[nodiscard]] ScriptGenerationResult generateEntityScript(
		const AIProviderClient& client,
		const std::string& providerId,
		const std::string& entityName,
		const std::string& description);
}
