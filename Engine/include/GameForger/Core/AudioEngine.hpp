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
