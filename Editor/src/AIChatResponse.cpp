#include "GameForger/Editor/AIChatResponse.hpp"

#include <algorithm>

#include "GameForger/Editor/Json.hpp"

namespace gameforger::editor
{
	namespace
	{
		// OpenAI-compatible success body:
		//   { "choices": [ { "message": { "role": "assistant", "content": "..." }, ... ], ... }
		// Walk the structured tree rather than substring-searching - the
		// previous find("\"content\"") implementation returned the first
		// literal "content" anywhere, which on a normal successful response
		// is the empty string after `choices[0].message.role` and never the
		// assistant's actual reply.
		std::optional<std::string> contentFromChoices(const json::Value& root)
		{
			const json::Value* choices = root.find("choices");
			if (choices == nullptr || choices->type != json::Value::Type::Array || choices->arrayValue.empty())
			{
				return std::nullopt;
			}
			const json::Value* message = choices->arrayValue.front().find("message");
			if (message == nullptr || message->type != json::Value::Type::Object)
			{
				return std::nullopt;
			}
			const json::Value* content = message->find("content");
			if (content == nullptr || content->type != json::Value::Type::String)
			{
				return std::nullopt;
			}
			return content->stringValue;
		}

		// Anthropic Messages success body:
		//   { "content": [ { "type": "text", "text": "..." }, ... ], ... }
		// Note the top-level "content" here is an ARRAY of blocks, not a
		// string - a response can interleave text with thinking/tool_use
		// blocks, so concatenate every text block and skip the rest.
		std::optional<std::string> contentFromBlocks(const json::Value& root)
		{
			const json::Value* content = root.find("content");
			if (content == nullptr || content->type != json::Value::Type::Array)
			{
				return std::nullopt;
			}
			std::string combined;
			for (const json::Value& block : content->arrayValue)
			{
				if (block.type != json::Value::Type::Object)
				{
					continue;
				}
				const json::Value* type = block.find("type");
				if (type == nullptr || type->type != json::Value::Type::String || type->stringValue != "text")
				{
					continue;
				}
				const json::Value* text = block.find("text");
				if (text != nullptr && text->type == json::Value::Type::String)
				{
					combined += text->stringValue;
				}
			}
			if (combined.empty())
			{
				return std::nullopt;
			}
			return combined;
		}

		std::optional<std::string> errorMessage(const json::Value& root)
		{
			const json::Value* error = root.find("error");
			if (error == nullptr)
			{
				return std::nullopt;
			}
			if (error->type == json::Value::Type::Object)
			{
				const json::Value* message = error->find("message");
				if (message != nullptr && message->type == json::Value::Type::String)
				{
					return message->stringValue;
				}
				return std::nullopt;
			}
			if (error->type == json::Value::Type::String)
			{
				return error->stringValue;
			}
			return std::nullopt;
		}
	}

	std::optional<std::string> extractChatMessageContent(const std::string& jsonBody)
	{
		const std::optional<json::Value> root = json::parse(jsonBody);
		if (!root.has_value())
		{
			return std::nullopt;
		}
		// Dispatch on response SHAPE rather than on the configured protocol:
		// the two are unambiguous (OpenAI has choices[], Anthropic has a
		// content[] array), and this keeps every caller free of provider
		// plumbing it would otherwise have to thread through.
		if (std::optional<std::string> fromChoices = contentFromChoices(*root))
		{
			return fromChoices;
		}
		return contentFromBlocks(*root);
	}

	std::string extractErrorMessage(const std::string& jsonBody)
	{
		const std::optional<json::Value> root = json::parse(jsonBody);
		if (root.has_value())
		{
			if (const std::optional<std::string> message = errorMessage(*root))
			{
				return *message;
			}
		}
		return jsonBody.substr(0, std::min<std::size_t>(jsonBody.size(), 300));
	}
}
