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

	// Rewrites an EXISTING script to satisfy `instruction`, returning the whole
	// new file. The AI could previously only create scripts from nothing
	// (MissingFunctions 4.7) - so improving a script meant describing it from
	// scratch and losing whatever had been hand-tuned in it.
	//
	// Returns the complete file rather than a patch on purpose: this project
	// has no diff-apply machinery, and a half-applied patch would leave a
	// script that neither loads nor resembles what the user had. Whole-file
	// replacement is recoverable - the previous text is still in the editor
	// buffer until they save.
	//
	// Blocking, like generateEntityScript - the caller runs it off the UI thread.
	[[nodiscard]] ScriptGenerationResult modifyEntityScript(
		const AIProviderClient& client,
		const std::string& providerId,
		const std::string& entityName,
		const std::string& existingSource,
		const std::string& instruction);

	// Auto-name for a generated script: "<Purpose>_<GameTitle>.lua", e.g.
	// FPS_Controller_MyGame.lua. Game/Scripts had grown nine files where five
	// were the same thing under different names - FPSController, PlayerFPS,
	// fps_controller, player_fps, controller - because every generated script
	// took whatever name was typed. Punctuation and spaces are stripped so the
	// result is always a legal filename.
	[[nodiscard]] std::string suggestedScriptFileName(
		const std::string& purpose, const std::string& gameTitle);
}
