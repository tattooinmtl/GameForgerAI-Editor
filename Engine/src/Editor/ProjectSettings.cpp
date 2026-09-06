#include "GameForger/Editor/ProjectSettings.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

#include "GameForger/Editor/Json.hpp"

namespace gameforger::editor
{
	namespace
	{
		constexpr const char* kProjectFile = "Game/Project.json";
		constexpr const char* kSettingsFile = "Game/Settings.json";

		struct KindName
		{
			BootStep::Kind kind;
			const char* name;
		};

		// Serialised as these strings rather than as the enum's integer value:
		// a reordered enum must not silently reinterpret every saved project,
		// and the AI writes these names directly in project.add_boot_step.
		constexpr std::array<KindName, 6> kKindNames{{
			{BootStep::Kind::WaitSeconds, "wait_seconds"},
			{BootStep::Kind::PlayAnimation, "play_animation"},
			{BootStep::Kind::PlayCutscene, "play_cutscene"},
			{BootStep::Kind::PlayAudio, "play_audio"},
			{BootStep::Kind::LockPlayerInput, "lock_player_input"},
			{BootStep::Kind::UnlockPlayerInput, "unlock_player_input"},
		}};

		struct HookEventName
		{
			AudioHook::Event event;
			const char* name;
		};

		constexpr std::array<HookEventName, 6> kHookEventNames{{
			{AudioHook::Event::OnPlayStart, "on_play_start"},
			{AudioHook::Event::OnPickup, "on_pickup"},
			{AudioHook::Event::OnProjectileFire, "on_projectile_fire"},
			{AudioHook::Event::OnProjectileHit, "on_projectile_hit"},
			{AudioHook::Event::OnGameOver, "on_game_over"},
			{AudioHook::Event::OnBootStep, "on_boot_step"},
		}};

		std::string readFile(const std::filesystem::path& path)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
			{
				return {};
			}
			std::ostringstream contents;
			contents << input.rdbuf();
			std::string text = contents.str();
			// Project.json currently ships with a UTF-8 BOM; the parser treats
			// it as trailing garbage and rejects the whole document.
			if (text.size() >= 3 &&
				static_cast<unsigned char>(text[0]) == 0xEF &&
				static_cast<unsigned char>(text[1]) == 0xBB &&
				static_cast<unsigned char>(text[2]) == 0xBF)
			{
				text.erase(0, 3);
			}
			return text;
		}

		// Same atomic write SceneSerializer::saveScene uses: full content into
		// a .tmp, copy the previous file aside as .bak, then rename. A crash or
		// AV lock mid-write leaves the old file intact rather than a truncated
		// Project.json that the editor can no longer open.
		bool writeFileAtomically(const std::filesystem::path& path, const std::string& contents, std::string& outError)
		{
			const std::filesystem::path tempPath = path.string() + ".tmp";
			{
				std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
				if (!output)
				{
					outError = "Could not write to " + tempPath.string();
					return false;
				}
				output << contents;
				output.flush();
				if (!output)
				{
					outError = "Failed while writing " + tempPath.string();
					return false;
				}
			}

			std::error_code ec;
			if (std::filesystem::exists(path, ec))
			{
				std::filesystem::copy(
					path, std::filesystem::path(path.string() + ".bak"),
					std::filesystem::copy_options::overwrite_existing, ec);
				std::filesystem::remove(path, ec);
			}
			std::filesystem::rename(tempPath, path, ec);
			if (ec)
			{
				std::filesystem::remove(tempPath, ec);
				outError = "Could not replace " + path.string() + ": " + ec.message();
				return false;
			}
			return true;
		}

		std::string readString(const json::Value& object, const char* key, std::string fallback)
		{
			if (const json::Value* value = object.find(key))
			{
				if (std::optional<std::string> text = value->asString())
				{
					return *text;
				}
			}
			return fallback;
		}

		void readBootSequence(const json::Value& root, std::vector<BootStep>& outSteps)
		{
			const json::Value* steps = root.find("bootSequence");
			if (steps == nullptr || steps->type != json::Value::Type::Array)
			{
				return;
			}
			for (const json::Value& entry : steps->arrayValue)
			{
				if (entry.type != json::Value::Type::Object)
				{
					continue;
				}
				BootStep step;
				if (!bootStepKindFromName(readString(entry, "kind", ""), step.kind))
				{
					// An unknown kind means a newer editor wrote this file.
					// Skipping the step is better than guessing at it.
					continue;
				}
				if (const json::Value* seconds = entry.find("seconds"))
				{
					if (std::optional<double> number = seconds->asNumber())
					{
						step.seconds = static_cast<float>(*number);
					}
				}
				step.targetEntity = readString(entry, "targetEntity", "");
				step.shotName = readString(entry, "shotName", "");
				step.clipPath = readString(entry, "clipPath", "");
				outSteps.push_back(std::move(step));
			}
		}

		json::Value bootSequenceToJson(const std::vector<BootStep>& steps)
		{
			std::vector<json::Value> items;
			items.reserve(steps.size());
			for (const BootStep& step : steps)
			{
				std::vector<std::pair<std::string, json::Value>> fields;
				fields.emplace_back("kind", json::makeString(bootStepKindName(step.kind)));
				// Only write the fields this kind actually uses, so the file
				// does not fill up with empty strings the reader ignores.
				switch (step.kind)
				{
					case BootStep::Kind::WaitSeconds:
						fields.emplace_back("seconds", json::makeNumber(step.seconds));
						break;
					case BootStep::Kind::PlayAnimation:
						fields.emplace_back("targetEntity", json::makeString(step.targetEntity));
						break;
					case BootStep::Kind::PlayCutscene:
						fields.emplace_back("shotName", json::makeString(step.shotName));
						break;
					case BootStep::Kind::PlayAudio:
						fields.emplace_back("clipPath", json::makeString(step.clipPath));
						break;
					case BootStep::Kind::LockPlayerInput:
					case BootStep::Kind::UnlockPlayerInput:
						break;
				}
				items.push_back(json::makeObject(std::move(fields)));
			}
			return json::makeArray(std::move(items));
		}

		void readAudioHooks(const json::Value& root, std::vector<AudioHook>& outHooks)
		{
			const json::Value* hooks = root.find("audioHooks");
			if (hooks == nullptr || hooks->type != json::Value::Type::Array)
			{
				return;
			}
			for (const json::Value& entry : hooks->arrayValue)
			{
				if (entry.type != json::Value::Type::Object)
				{
					continue;
				}
				AudioHook hook;
				if (!audioHookEventFromName(readString(entry, "event", ""), hook.event))
				{
					continue;
				}
				hook.clipPath = readString(entry, "clipPath", "");
				if (const json::Value* volume = entry.find("volume"))
				{
					if (std::optional<double> number = volume->asNumber())
					{
						hook.volume = static_cast<float>(*number);
					}
				}
				// Absent in files written before looping existed - defaults to
				// false, so an existing one-shot hook behaves exactly as before.
				if (const json::Value* loop = entry.find("loop"))
				{
					if (std::optional<bool> flag = loop->asBool())
					{
						hook.loop = *flag;
					}
				}
				outHooks.push_back(std::move(hook));
			}
		}

		json::Value audioHooksToJson(const std::vector<AudioHook>& hooks)
		{
			std::vector<json::Value> items;
			items.reserve(hooks.size());
			for (const AudioHook& hook : hooks)
			{
				items.push_back(json::makeObject({
					{"event", json::makeString(audioHookEventName(hook.event))},
					{"clipPath", json::makeString(hook.clipPath)},
					{"volume", json::makeNumber(hook.volume)},
					{"loop", json::makeBool(hook.loop)},
				}));
			}
			return json::makeArray(std::move(items));
		}

		// Replace `key` in `object` if present, otherwise append it. Keeps the
		// existing key order so a save does not reshuffle the whole file.
		void setMemberPreservingOrder(json::Value& object, const std::string& key, json::Value value)
		{
			for (std::pair<std::string, json::Value>& entry : object.objectValue)
			{
				if (entry.first == key)
				{
					entry.second = std::move(value);
					return;
				}
			}
			object.objectValue.emplace_back(key, std::move(value));
		}
	}

	const char* bootStepKindName(const BootStep::Kind kind) noexcept
	{
		for (const KindName& entry : kKindNames)
		{
			if (entry.kind == kind)
			{
				return entry.name;
			}
		}
		return "wait_seconds";
	}

	bool bootStepKindFromName(const std::string& name, BootStep::Kind& outKind) noexcept
	{
		for (const KindName& entry : kKindNames)
		{
			if (name == entry.name)
			{
				outKind = entry.kind;
				return true;
			}
		}
		return false;
	}

	const char* audioHookEventName(const AudioHook::Event event) noexcept
	{
		for (const HookEventName& entry : kHookEventNames)
		{
			if (entry.event == event)
			{
				return entry.name;
			}
		}
		return "on_play_start";
	}

	bool audioHookEventFromName(const std::string& name, AudioHook::Event& outEvent) noexcept
	{
		for (const HookEventName& entry : kHookEventNames)
		{
			if (name == entry.name)
			{
				outEvent = entry.event;
				return true;
			}
		}
		return false;
	}

	std::string describeBootStep(const BootStep& step)
	{
		switch (step.kind)
		{
			case BootStep::Kind::WaitSeconds:
			{
				std::array<char, 64> buffer{};
				std::snprintf(buffer.data(), buffer.size(), "Wait %.2fs", static_cast<double>(step.seconds));
				return buffer.data();
			}
			case BootStep::Kind::PlayAnimation:
				return "Play animation on '" + step.targetEntity + "'";
			case BootStep::Kind::PlayCutscene:
				return "Play cutscene '" + step.shotName + "'";
			case BootStep::Kind::PlayAudio:
				return "Play audio '" + step.clipPath + "'";
			case BootStep::Kind::LockPlayerInput:
				return "Lock player input";
			case BootStep::Kind::UnlockPlayerInput:
				return "Unlock player input";
		}
		return "Unknown step";
	}

	ProjectSettingsIoResult loadProjectSettings(
		const std::filesystem::path& projectRoot, ProjectSettings& outSettings)
	{
		outSettings = ProjectSettings{};
		std::vector<std::string> skipped;

		const std::string projectText = readFile(projectRoot / kProjectFile);
		if (const std::optional<json::Value> root = json::parse(projectText);
			root.has_value() && root->type == json::Value::Type::Object)
		{
			outSettings.name = readString(*root, "name", "");
			outSettings.startupScene = readString(*root, "startupScene", "");
			outSettings.skeletonProfile = readString(*root, "skeletonProfile", "");
			outSettings.aiProvidersPath = readString(*root, "aiProviders", "");
			outSettings.scriptDirectory = readString(*root, "scriptDirectory", "");
			if (const json::Value* dirs = root->find("assetDirectories");
				dirs != nullptr && dirs->type == json::Value::Type::Array)
			{
				for (const json::Value& dir : dirs->arrayValue)
				{
					if (std::optional<std::string> text = dir.asString())
					{
						outSettings.assetDirectories.push_back(std::move(*text));
					}
				}
			}
			readBootSequence(*root, outSettings.bootSequence);
		}
		else
		{
			skipped.emplace_back(kProjectFile);
		}

		const std::string settingsText = readFile(projectRoot / kSettingsFile);
		if (const std::optional<json::Value> root = json::parse(settingsText);
			root.has_value() && root->type == json::Value::Type::Object)
		{
			if (const json::Value* sensitivity = root->find("mouseSensitivity"))
			{
				if (std::optional<double> number = sensitivity->asNumber())
				{
					outSettings.mouseSensitivity = static_cast<float>(*number);
				}
			}
			if (const json::Value* fps = root->find("targetFps"))
			{
				if (std::optional<double> number = fps->asNumber())
				{
					outSettings.targetFps = static_cast<int>(*number);
				}
			}
			readAudioHooks(*root, outSettings.audioHooks);
		}
		else
		{
			skipped.emplace_back(kSettingsFile);
		}

		if (skipped.empty())
		{
			return {true, "Loaded project settings."};
		}
		// Missing/malformed is survivable - defaults stand in - but say which,
		// so a typo in Project.json does not look like the editor losing data.
		std::string message = "Using defaults for: ";
		for (std::size_t i = 0; i < skipped.size(); ++i)
		{
			if (i > 0) message += ", ";
			message += skipped[i];
		}
		return {true, message};
	}

	ProjectSettingsIoResult saveProjectSettings(
		const std::filesystem::path& projectRoot, const ProjectSettings& settings)
	{
		// Re-parse the existing file and overwrite only the fields this struct
		// owns, so a key someone added by hand is not silently dropped by a
		// save from the inspector.
		json::Value document;
		if (const std::optional<json::Value> existing = json::parse(readFile(projectRoot / kProjectFile));
			existing.has_value() && existing->type == json::Value::Type::Object)
		{
			document = *existing;
		}
		else
		{
			document = json::makeObject({
				{"format", json::makeString("GameForgerProject")},
				{"version", json::makeNumber(1)},
			});
		}

		setMemberPreservingOrder(document, "name", json::makeString(settings.name));
		setMemberPreservingOrder(document, "aiProviders", json::makeString(settings.aiProvidersPath));
		setMemberPreservingOrder(document, "startupScene", json::makeString(settings.startupScene));
		setMemberPreservingOrder(document, "skeletonProfile", json::makeString(settings.skeletonProfile));
		{
			std::vector<json::Value> dirs;
			dirs.reserve(settings.assetDirectories.size());
			for (const std::string& dir : settings.assetDirectories)
			{
				dirs.push_back(json::makeString(dir));
			}
			setMemberPreservingOrder(document, "assetDirectories", json::makeArray(std::move(dirs)));
		}
		setMemberPreservingOrder(document, "scriptDirectory", json::makeString(settings.scriptDirectory));
		setMemberPreservingOrder(document, "bootSequence", bootSequenceToJson(settings.bootSequence));

		std::string error;
		if (!writeFileAtomically(projectRoot / kProjectFile, json::serializePretty(document), error))
		{
			return {false, error};
		}

		json::Value settingsDocument;
		if (const std::optional<json::Value> existing = json::parse(readFile(projectRoot / kSettingsFile));
			existing.has_value() && existing->type == json::Value::Type::Object)
		{
			settingsDocument = *existing;
		}
		else
		{
			settingsDocument = json::makeObject({});
		}
		setMemberPreservingOrder(
			settingsDocument, "mouseSensitivity", json::makeNumber(settings.mouseSensitivity));
		setMemberPreservingOrder(settingsDocument, "targetFps", json::makeNumber(settings.targetFps));
		setMemberPreservingOrder(settingsDocument, "audioHooks", audioHooksToJson(settings.audioHooks));
		if (!writeFileAtomically(projectRoot / kSettingsFile, json::serializePretty(settingsDocument), error))
		{
			return {false, error};
		}

		return {true, "Saved project settings."};
	}
}
