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
		// The full proxy catalog. This list is the ONLY thing telling the model
		// what exists - anything missing here, the AI cannot use, and anything
		// wrong here it will confidently invent. Several scripts already in
		// Game/Scripts were generated against an earlier, thinner version of
		// this prompt and called APIs that never existed (Input./Entity./
		// Vector()/Raycast()/UI., an Update() entry point); they had to be
		// rewritten by hand. Keep this in step with pushEntityProxy and friends
		// in ScriptRuntime.cpp.
		request.systemPrompt =
			"You are a Lua scripting assistant for the GameForgerAI game engine editor. "
			"Generate a single, complete Lua script implementing the gameplay behavior the user "
			"describes, for a game entity named '" + entityName + "'. "
			"Output ONLY raw Lua source code: no markdown code fences, no explanation before or "
			"after the code. Add a short comment only where the logic is non-obvious.\n\n"

			"STRUCTURE. The script MUST end with `return TableName`, where TableName is a local "
			"table whose methods are the lifecycle hooks. A script that does not return such a "
			"table will fail to load. Example:\n"
			"local X = {}\n"
			"function X:on_start() end\n"
			"function X:on_update(delta_time) end\n"
			"function X:on_end() end\n"
			"return X\n\n"
			"on_start(self) runs once when Play starts. on_update(self, delta_time) runs every "
			"frame. on_end(self) runs when Play stops or the script is detached - use it to "
			"release anything on_start took.\n\n"

			"AVAILABLE APIs. The engine attaches these to `self` before on_start() runs. This is "
			"the COMPLETE list - nothing else exists. Do NOT invent globals such as Input, Entity, "
			"Vector, Raycast, UI, DeltaTime, GameObject, or love.*; there is no Update() entry "
			"point, and vectors are plain {x=,y=,z=} tables with NO arithmetic operators, so "
			"multiply components individually.\n\n"

			"self.entity  - getPosition() -> {x,y,z}; setPosition({x=,y=,z=}) (ONE table, not three "
			"numbers); getRotation() -> {x,y,z} degrees; setRotation({x=,y=,z=}); getScale() -> "
			"{x,y,z}; getForward() -> {x,y,z}; getRight() -> {x,y,z} screen-right, use it for "
			"strafing and do NOT negate it.\n"

			"self.input   - isKeyDown(name) -> bool held; isKeyPressed(name) -> bool this frame "
			"only; getAxis(positiveKey, negativeKey) -> -1..1; getMouseDeltaX(); getMouseDeltaY(). "
			"Key names are strings like \"W\", \"A\", \"S\", \"D\", \"Space\", \"LeftShift\", "
			"\"E\", \"C\", \"V\", \"Left\", \"Right\". MOUSE BUTTONS ARE NOT AVAILABLE - there is "
			"no \"LeftMouse\" key; gate mouse-driven behaviour on a held key instead.\n"

			"self.camera  - setMode(\"fps\"|\"third_person\"); getMode().\n"

			"self.physics - resolve(position, halfWidth, height) -> correctedPosition, grounded. "
			"Box collision against Collider-enabled entities. This is the ONLY collision query; "
			"there is no raycast.\n"

			"self.world   - findNearestWithTag(tag) -> position|nil, distance, name (nearest OTHER "
			"entity with that tag); findPositionByTag(tag) -> position|nil; "
			"fireProjectile(from, to, speed, hitTag); fireGravityProjectile(from, direction, speed, "
			"hitTag) for an arcing shot; isHoldingItem(); isAimingCatapult(); "
			"setOperatingCatapult(bool); setEntityRotation(entityName, {x=,y=,z=}). Use "
			"fireProjectile/fireGravityProjectile rather than simulating projectiles in Lua tables "
			"- the engine owns their movement, collision and despawn.\n"

			"self.gameManager - setCursorLock(bool). Any script that drives the player camera "
			"should call setCursorLock(true) in on_start and setCursorLock(false) in on_end.\n"

			"self.managers    - register(name); unregister(name); has(name) -> bool; list() -> "
			"table of names. A controller MUST register(name) in on_start and unregister(name) in "
			"on_end: the engine only locks the mouse cursor while at least one manager is "
			"registered, so a controller that skips this gets no cursor lock.\n"

			"self.audio   - play(clipPath, volume, loop) with clipPath relative to the project "
			"root under Game/Audio; stop() stops ALL sounds, not just this script's; "
			"setMasterVolume(0..1); isPlaying() currently always returns false, so do not branch "
			"on it.\n\n"

			"There is no way for a script to delete its own entity, draw UI, load a scene, or read "
			"a file. If the request needs something absent from the list above, implement the "
			"closest achievable behaviour and note the limitation in a comment rather than calling "
			"an API that does not exist.";
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
