#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace gameforger::editor
{
	// One step of the startup sequence that runs before the player gets
	// control. This is the data behind requests like "play an animation on the
	// loading screen" or "trigger an intro cutscene before the player can
	// move" - previously there was no notion of game flow anywhere in the
	// engine, only per-entity scripts and animations.
	//
	// Deliberately a small closed enum rather than free-form script: the AI
	// writes these (see the project.* Cockpit tools), and a typed step can be
	// validated before it is ever stored. A step that fails validation is
	// rejected at the command bus, not discovered at Play time.
	struct BootStep
	{
		enum class Kind
		{
			WaitSeconds,      // pause for `seconds`
			PlayAnimation,    // play `targetEntity`'s own EntityAnimation
			PlayCutscene,     // play storyboard shot `shotName`
			PlayAudio,        // fire `clipPath` once
			LockPlayerInput,  // hold player input (implicit while a sequence runs)
			UnlockPlayerInput // hand control back early
		};

		Kind kind = Kind::WaitSeconds;
		float seconds = 1.0F;
		std::string targetEntity;
		std::string shotName;
		std::string clipPath;
	};

	[[nodiscard]] const char* bootStepKindName(BootStep::Kind kind) noexcept;
	[[nodiscard]] bool bootStepKindFromName(const std::string& name, BootStep::Kind& outKind) noexcept;

	// A short human-readable summary of what a step does, for the panel's list
	// rows and for project.get_settings' reply to the AI.
	[[nodiscard]] std::string describeBootStep(const BootStep& step);

	// Game-event → clip bindings. These fire from real existing events
	// (Play start, pickup, projectile fire/hit, game over, boot play_audio)
	// rather than invented ones. Stored in Settings.json.
	struct AudioHook
	{
		enum class Event
		{
			OnPlayStart,
			OnPickup,
			OnProjectileFire,
			OnProjectileHit,
			OnGameOver,
			OnBootStep
		};

		Event event = Event::OnPlayStart;
		std::string clipPath;
		float volume = 1.0F;
	};

	[[nodiscard]] const char* audioHookEventName(AudioHook::Event event) noexcept;
	[[nodiscard]] bool audioHookEventFromName(const std::string& name, AudioHook::Event& outEvent) noexcept;

	// The typed view of Game/Project.json + Game/Settings.json. Both the
	// inspector panel and the AI go through this rather than editing raw JSON,
	// so there is exactly one place that knows the file layout.
	struct ProjectSettings
	{
		// Game/Project.json
		std::string name;
		std::string startupScene;
		std::string skeletonProfile;
		std::string aiProvidersPath;
		std::string scriptDirectory;
		std::vector<std::string> assetDirectories;
		std::vector<BootStep> bootSequence;

		// Game/Settings.json
		float mouseSensitivity = 0.15F;
		int targetFps = 60;
		std::vector<AudioHook> audioHooks;
	};

	struct ProjectSettingsIoResult
	{
		bool success = false;
		std::string message;
	};

	// Reads both files under `projectRoot`. A missing or malformed file is not
	// fatal - the corresponding fields keep their defaults and the message says
	// what was skipped, so a project with no Settings.json still opens.
	[[nodiscard]] ProjectSettingsIoResult loadProjectSettings(
		const std::filesystem::path& projectRoot, ProjectSettings& outSettings);

	// Writes both files. Unknown keys already in Project.json are PRESERVED:
	// the file is re-parsed and only the fields this struct owns are replaced,
	// so hand-added keys survive a save from the editor. Uses the same
	// tmp + .bak + rename dance as SceneSerializer::saveScene, so an
	// interrupted write cannot leave a truncated Project.json behind.
	[[nodiscard]] ProjectSettingsIoResult saveProjectSettings(
		const std::filesystem::path& projectRoot, const ProjectSettings& settings);
}
