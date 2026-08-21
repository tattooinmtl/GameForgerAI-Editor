#pragma once

#include <string>
#include <vector>

#include "GameForger/Editor/AICommand.hpp"
#include "GameForger/Editor/AIProviderClient.hpp"
#include "GameForger/Editor/EditorScene.hpp"

namespace gameforger::editor
{
	struct AIPlanResult
	{
		bool success = false;
		std::string message;
		std::vector<AIEditorCommand> commands;
		std::vector<std::string> commandDescriptions;
	};

	[[nodiscard]] std::string describeCommand(const AIEditorCommand& command);

	// Blocking call — the caller is responsible for running this off the UI thread.
	// `currentEntities` is a snapshot of the scene at the moment the prompt is
	// sent, so the model can target existing entities by name (modify/rename/
	// delete) instead of only ever creating new ones - each call is a fresh,
	// stateless request with no memory of prior AI Forge prompts, so this
	// snapshot is the only continuity the model gets.
	[[nodiscard]] AIPlanResult planEditorCommands(
		const AIProviderClient& client,
		const std::string& providerId,
		const std::string& userPrompt,
		const std::vector<SceneEntity>& currentEntities);
}
