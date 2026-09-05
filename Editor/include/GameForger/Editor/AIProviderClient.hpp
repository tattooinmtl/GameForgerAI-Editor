#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

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

	// Phase B.3: one entry returned by discoverModels(). Rank is filled in by
	// the caller's relevance ranker, not by discovery itself.
	struct AIModelInfo
	{
		std::string id;          // e.g. "gpt-4o", "claude-opus-4-7"
		std::string displayName; // Same as id if the provider gives no separate name.
		int relevanceRank = 0;   // Higher = better fit for game modelling/creation.
	};

	class AIProviderClient final
	{
	public:
		explicit AIProviderClient(std::filesystem::path projectRoot);
		~AIProviderClient();

		AIProviderClient(const AIProviderClient&) = delete;
		AIProviderClient& operator=(const AIProviderClient&) = delete;

		// Abort every in-flight WinHTTP request. Safe to call from the UI
		// thread while workers are blocked in send()/sendRaw()/discoverModels().
		// Closing the request handle unblocks those calls so join() does not
		// wait out the 30s timeout on quit.
		void requestCancel();

		[[nodiscard]] AIProviderResponse send(
			const std::string& providerId,
			const AIProviderRequest& request) const;

		// Phase C: same transport as send() but the caller supplies a fully
		// pre-composed request body. The Cockpit uses this to send provider-
		// specific tool-use shapes (Anthropic messages, OpenAI tool_calls)
		// that the simple send() path doesn't know how to build.
		//
		// If `overrideEndpointPath` is non-empty (e.g. "/v1/messages" for
		// Anthropic when Providers.json still lists "/v1/messages"), the
		// endpoint path from Providers.json is used unchanged. Otherwise the
		// caller can pass a leaf like "/v1/models" to hit a different path
		// on the same host with the same key. Empty = use the Providers.json path.
		[[nodiscard]] AIProviderResponse sendRaw(
			const std::string& providerId,
			const std::string& body,
			const std::string& overrideEndpointPath = "") const;

		// Phase B.3: fetch the model list from the provider. Only works for
		// providers with an OpenAI-compatible /v1/models endpoint; Anthropic
		// and custom providers return an empty list (caller can then fall
		// back to the hardcoded default or a user-typed value).
		//
		// Returns { models, error }. On error, models is empty and error
		// carries a human-readable reason.
		struct DiscoverResult
		{
			std::vector<AIModelInfo> models;
			std::string error;
		};
		[[nodiscard]] DiscoverResult discoverModels(const std::string& providerId) const;

	private:
		struct CancelState;
		std::filesystem::path projectRoot_;
		std::unique_ptr<CancelState> cancel_;
	};

	// Phase B.5: score-based sort. Higher-relevance models bubble to the top.
	// Deterministic given the same inputs. Called by callers that display a
	// model dropdown - a name like "gpt-4o-coder" outranks "gpt-3.5-turbo".
	void rankModelsByRelevance(std::vector<AIModelInfo>& models);
}
