#pragma once

#include <vector>

#include "GameForger/Editor/ProjectSettings.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

namespace gameforger::core
{
	inline const std::vector<std::string>& audioClipExtensions()
	{
		static const std::vector<std::string> kExtensions{".wav", ".mp3", ".flac"};
		return kExtensions;
	}

	// Playback backend (miniaudio). Confined to the .cpp behind a pimpl so the
	// ~90k-line header never lands in other translation units.
	//
	// initialize() degrades to silent mode when there is no audio device (CI,
	// remote desktop, missing drivers) instead of crashing. play() in silent
	// mode still validates the path and reports success so a missing speaker
	// cannot deadlock a boot sequence waiting on a clip.
	// Core's own copy of the effect parameters. Deliberately not
	// editor::AudioEffects: Core must not depend on the editor's scene types,
	// so the caller converts. Same fields, same meaning.
	struct EffectSettings
	{
		bool reverb = false;
		float reverbRoomSize = 0.5F;
		float reverbDamping = 0.5F;
		float reverbWet = 0.3F;
		float reverbDry = 0.7F;

		bool delay = false;
		float delaySeconds = 0.25F;
		float delayDecay = 0.4F;

		enum class Filter { None, LowPass, HighPass };
		Filter filter = Filter::None;
		float cutoffHz = 1000.0F;

		// Volume ramps on the voice itself, not nodes. Carried here because
		// this is the per-source bundle the engine already receives.
		float fadeInSeconds = 0.0F;
		float fadeOutSeconds = 0.0F;

		// NODE effects only. Fades deliberately do not count: they need no
		// node graph, and treating them as "enabled" would build one for
		// nothing on every faded sound.
		[[nodiscard]] bool anyEnabled() const noexcept
		{
			return reverb || delay || filter != Filter::None;
		}

		[[nodiscard]] bool anyFade() const noexcept
		{
			return fadeInSeconds > 0.0F || fadeOutSeconds > 0.0F;
		}
	};

	class AudioEngine
	{
	public:
		AudioEngine();
		~AudioEngine();

		AudioEngine(const AudioEngine&) = delete;
		AudioEngine& operator=(const AudioEngine&) = delete;
		AudioEngine(AudioEngine&&) noexcept;
		AudioEngine& operator=(AudioEngine&&) noexcept;

		// False means silent mode. The object is still usable.
		bool initialize();
		void shutdown();

		[[nodiscard]] bool isAvailable() const noexcept;
		[[nodiscard]] bool isSilent() const noexcept { return !isAvailable(); }

		void setMasterVolume(float volume);
		[[nodiscard]] float masterVolume() const noexcept;

		// `clipRelativePath` must resolve under Game/Audio via resolveProjectFile.
		bool play(
			const std::filesystem::path& projectRoot,
			const std::string& clipRelativePath,
			float volume = 1.0F,
			float pitch = 1.0F,
			bool loop = false);

		// Same, with a per-voice DSP chain. Split from the plain overload so
		// the common effect-free path keeps building no nodes at all.
		bool playWithEffects(
			const std::filesystem::path& projectRoot,
			const std::string& clipRelativePath,
			float volume,
			float pitch,
			bool loop,
			const EffectSettings& effects);

		// Preview with effects, on the dedicated preview voice, so tuning a
		// setting in the panel cannot interfere with in-game sound.
		bool playPreviewWithEffects(
			const std::filesystem::path& projectRoot,
			const std::string& clipRelativePath,
			float volume,
			const EffectSettings& effects);

		bool play3D(
			const std::filesystem::path& projectRoot,
			const std::string& clipRelativePath,
			const glm::vec3& worldPosition,
			float volume,
			float pitch,
			bool loop,
			float minDistance,
			float maxDistance);

		void setListener(const glm::vec3& worldPosition, const glm::vec3& forward);

		void stopAll();
		void stopPreview();

		// Stops only the voices started from `clipRelativePath`. Needed because
		// stopAll() from one script also killed every other script's sound -
		// a music manager stopping its track silenced the whole game.
		void stop(const std::string& clipRelativePath);

		// Ramps the clip's voices down over `fadeSeconds` and lets them finish
		// on their own, instead of cutting them off mid-sample. Voices that are
		// fading are still reported by isPlaying() until they actually end.
		void stopWithFade(const std::string& clipRelativePath, float fadeSeconds);

		// True while any voice is still running. Finished voices are pruned
		// first, so this reflects reality rather than what was once started.
		[[nodiscard]] bool isAnyPlaying() const;

		// True while a voice started from this clip is still running.
		[[nodiscard]] bool isPlaying(const std::string& clipRelativePath) const;

		// Preview is a single dedicated voice so the Audio panel's Play/Stop
		// cannot kill in-game sounds, and vice versa.
		bool playPreview(
			const std::filesystem::path& projectRoot,
			const std::string& clipRelativePath,
			float volume = 1.0F);

		[[nodiscard]] float clipDurationSeconds(
			const std::filesystem::path& projectRoot, const std::string& clipRelativePath) const;

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};
}

namespace gameforger::editor
{
	// Plays every hook bound to `event`. Lives in Engine, not the Editor's
	// audio panel: GameForgerRuntime fires the same hooks, and a scene has to
	// sound the same standalone as it does in Play mode.
	void fireAudioHooks(
		core::AudioEngine& audio,
		const std::filesystem::path& projectRoot,
		const std::vector<AudioHook>& hooks,
		AudioHook::Event event);
}
