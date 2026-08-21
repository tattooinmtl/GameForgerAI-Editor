#include "GameForger/Editor/ScriptGenerator.hpp"

#include <cctype>
#include <utility>

#include "GameForger/Editor/AIChatResponse.hpp"

namespace gameforger::editor
{
	namespace
	{
		std::string trimCopy(std::string text)
		{
			const auto isSpace = [](const unsigned char c) { return std::isspace(c) != 0; };
			while (!text.empty() && isSpace(static_cast<unsigned char>(text.front())))
			{
				text.erase(text.begin());
			}
			while (!text.empty() && isSpace(static_cast<unsigned char>(text.back())))
			{
				text.pop_back();
			}
			return text;
		}

		// Models sometimes ignore the "no code fences" instruction; strip them defensively.
		std::string stripCodeFences(std::string text)
		{
			text = trimCopy(std::move(text));
			if (text.starts_with("```"))
			{
				const std::size_t firstNewline = text.find('\n');
				text = firstNewline == std::string::npos ? std::string{} : text.substr(firstNewline + 1);
				if (text.ends_with("```"))
				{
					text = text.substr(0, text.size() - 3);
				}
				text = trimCopy(std::move(text));
			}
			return text;
		}

		// Defends against the same over-escaping quirk AICommandPlanner.cpp
		// guards against (models occasionally writing "\\n" where "\n" was
		// correct). Lua never assigns meaning to a bare backslash outside a
		// string literal, so any \n/\t/\r found outside single/double-quoted
		// strings is unambiguously this artifact; escapes genuinely inside a
		// Lua string literal are left completely untouched.
		std::string recoverOverEscapedNewlines(const std::string& text)
		{
			std::string result;
			result.reserve(text.size());
			char stringQuote = '\0';
			for (std::size_t index = 0; index < text.size(); ++index)
			{
				const char current = text[index];
				if (stringQuote != '\0')
				{
					result += current;
					if (current == '\\' && index + 1 < text.size())
					{
						result += text[index + 1];
						++index;
						continue;
					}
					if (current == stringQuote)
					{
						stringQuote = '\0';
					}
					continue;
				}
				if (current == '"' || current == '\'')
				{
					stringQuote = current;
					result += current;
					continue;
				}
				if (current == '\\' && index + 1 < text.size() &&
					(text[index + 1] == 'n' || text[index + 1] == 't' || text[index + 1] == 'r'))
				{
					result += text[index + 1] == 'n' ? '\n' : (text[index + 1] == 't' ? '\t' : '\r');
					++index;
					continue;
				}
				result += current;
			}
			return result;
		}
	}

	ScriptGenerationResult generateEntityScript(
		const AIProviderClient& client,
		const std::string& providerId,
		const std::string& entityName,
		const std::string& description)
	{
		AIProviderRequest request;
		request.systemPrompt =
			"You are a Lua scripting assistant for the GameForgerAI game engine editor. "
			"Generate a single, complete Lua script implementing the gameplay behavior the user "
			"describes, for a game entity named '" + entityName + "'. "
			"Output ONLY raw Lua source code: no markdown code fences, no explanation before or "
			"after the code. Add a short comment only where the logic is non-obvious.\n\n"
			"The script MUST end with `return TableName` where TableName is a local table defining "
			"on_start(self) (runs once when Play starts) and/or on_update(self, delta_time) (runs every "
			"frame) as methods, e.g. `local X = {}; function X:on_start() end; function X:on_update(dt) "
			"end; return X`. A script that does not return such a table will fail to load. Before "
			"on_start() runs, the engine attaches to `self`: `self.entity` (getPosition()/"
			"setPosition({x=,y=,z=})/getRotation()/setRotation({x=,y=,z=} in degrees)/getForward()/"
			"getRight()), `self.input` (isKeyDown(name)/isKeyPressed(name)/getAxis(positiveKey,negativeKey), "
			"key names like \"W\", \"Space\", \"LeftShift\"), and `self.camera` "
			"(setMode(\"fps\"|\"third_person\")/getMode()) for controlling the Game view camera.";
		request.prompt = description;

		const AIProviderResponse response = client.send(providerId, request);
		if (!response.success)
		{
			if (!response.error.empty())
			{
				return {false, response.error};
			}
			const std::string detail = extractErrorMessage(response.body);
			return {false, detail.empty() ? "AI provider request failed." : detail};
		}

		const std::optional<std::string> content = extractChatMessageContent(response.body);
		if (!content.has_value() || content->empty())
		{
			return {false, "The AI response did not contain any script content."};
		}
		return {true, recoverOverEscapedNewlines(stripCodeFences(*content))};
	}
}
