#pragma once

#include <filesystem>
#include <string>

namespace gameforger::editor
{
	struct AIProviderRequest
	{
		std::string prompt;
		std::string systemPrompt = "You are an editor assistant. Return concise, actionable responses.";
	};

	struct AIProviderResponse
	{
		bool success = false;
		long statusCode = 0;
		std::string body;
		std::string error;
	};

	class AIProviderClient final
	{
	public:
		explicit AIProviderClient(std::filesystem::path projectRoot);

		[[nodiscard]] AIProviderResponse send(
			const std::string& providerId,
			const AIProviderRequest& request) const;

	private:
		std::filesystem::path projectRoot_;
	};
}
