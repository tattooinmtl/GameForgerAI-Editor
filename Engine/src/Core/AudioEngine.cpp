#include "GameForger/Core/AudioEngine.hpp"

#include "GameForger/Core/ProjectPaths.hpp"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <system_error>
#include <utility>

#include <glm/geometric.hpp>

namespace gameforger::core
{
	namespace
	{
		std::optional<std::filesystem::path> resolveClip(
			const std::filesystem::path& projectRoot, const std::string& clipRelativePath)
		{
			const std::optional<std::filesystem::path> resolved =
				resolveProjectFile(projectRoot, clipRelativePath, "Game/Audio", audioClipExtensions());
			if (!resolved.has_value())
			{
				return std::nullopt;
			}
			std::error_code existsError;
			if (!std::filesystem::is_regular_file(*resolved, existsError))
			{
				return std::nullopt;
			}
			return resolved;
		}

		float clampVolume(const float volume) noexcept
		{
			if (!std::isfinite(volume))
			{
				return 1.0F;
			}
			return std::clamp(volume, 0.0F, 4.0F);
		}

		float clampPitch(const float pitch) noexcept
		{
			if (!std::isfinite(pitch) || pitch <= 0.0F)
			{
				return 1.0F;
			}
			return std::clamp(pitch, 0.1F, 4.0F);
		}

		void uninitSound(ma_sound* sound)
		{
			if (sound != nullptr)
			{
				ma_sound_uninit(sound);
				delete sound;
			}
		}
	}

	struct AudioEngine::Impl
	{
		ma_engine engine{};
		bool engineReady = false;
		float masterVolume = 1.0F;
		std::vector<std::unique_ptr<ma_sound, void (*)(ma_sound*)>> voices;
		std::unique_ptr<ma_sound, void (*)(ma_sound*)> preview{nullptr, uninitSound};

		void pruneFinished()
		{
			voices.erase(
				std::remove_if(
					voices.begin(),
					voices.end(),
					[](const std::unique_ptr<ma_sound, void (*)(ma_sound*)>& sound)
					{
						return sound == nullptr || !ma_sound_is_playing(sound.get());
					}),
				voices.end());
		}
	};

	AudioEngine::AudioEngine() = default;
	AudioEngine::~AudioEngine()
	{
		shutdown();
	}

	AudioEngine::AudioEngine(AudioEngine&&) noexcept = default;
	AudioEngine& AudioEngine::operator=(AudioEngine&&) noexcept = default;

	bool AudioEngine::initialize()
	{
		if (impl_ && impl_->engineReady)
		{
			return true;
		}
		impl_ = std::make_unique<Impl>();
		ma_engine_config config = ma_engine_config_init();
		const ma_result result = ma_engine_init(&config, &impl_->engine);
		if (result != MA_SUCCESS)
		{
			impl_->engineReady = false;
			return false;
		}
		impl_->engineReady = true;
		ma_engine_set_volume(&impl_->engine, impl_->masterVolume);
		return true;
	}

	void AudioEngine::shutdown()
	{
		if (!impl_)
		{
			return;
		}
		stopAll();
		stopPreview();
		if (impl_->engineReady)
		{
			ma_engine_uninit(&impl_->engine);
			impl_->engineReady = false;
		}
		impl_.reset();
	}

	bool AudioEngine::isAvailable() const noexcept
	{
		return impl_ && impl_->engineReady;
	}

	void AudioEngine::setMasterVolume(const float volume)
	{
		if (!impl_)
		{
			return;
		}
		impl_->masterVolume = clampVolume(volume);
		if (impl_->engineReady)
		{
			ma_engine_set_volume(&impl_->engine, impl_->masterVolume);
		}
	}

	float AudioEngine::masterVolume() const noexcept
	{
		return impl_ ? impl_->masterVolume : 1.0F;
	}

	bool AudioEngine::play(
		const std::filesystem::path& projectRoot,
		const std::string& clipRelativePath,
		const float volume,
		const float pitch,
		const bool loop)
	{
		const std::optional<std::filesystem::path> resolved = resolveClip(projectRoot, clipRelativePath);
		if (!resolved.has_value())
		{
			return false;
		}
		if (!isAvailable())
		{
			return true;
		}
		impl_->pruneFinished();
		auto sound = std::unique_ptr<ma_sound, void (*)(ma_sound*)>(new ma_sound{}, uninitSound);
		const ma_uint32 flags = loop ? MA_SOUND_FLAG_LOOPING : 0;
		if (ma_sound_init_from_file(
				&impl_->engine, resolved->string().c_str(), flags, nullptr, nullptr, sound.get()) != MA_SUCCESS)
		{
			return false;
		}
		ma_sound_set_volume(sound.get(), clampVolume(volume));
		ma_sound_set_pitch(sound.get(), clampPitch(pitch));
		ma_sound_set_spatialization_enabled(sound.get(), MA_FALSE);
		if (ma_sound_start(sound.get()) != MA_SUCCESS)
		{
			return false;
		}
		impl_->voices.push_back(std::move(sound));
		return true;
	}

	bool AudioEngine::play3D(
		const std::filesystem::path& projectRoot,
		const std::string& clipRelativePath,
		const glm::vec3& worldPosition,
		const float volume,
		const float pitch,
		const bool loop,
		const float minDistance,
		const float maxDistance)
	{
		const std::optional<std::filesystem::path> resolved = resolveClip(projectRoot, clipRelativePath);
		if (!resolved.has_value())
		{
			return false;
		}
		if (!isAvailable())
		{
			return true;
		}
		impl_->pruneFinished();
		auto sound = std::unique_ptr<ma_sound, void (*)(ma_sound*)>(new ma_sound{}, uninitSound);
		const ma_uint32 flags = loop ? MA_SOUND_FLAG_LOOPING : 0;
		if (ma_sound_init_from_file(
				&impl_->engine, resolved->string().c_str(), flags, nullptr, nullptr, sound.get()) != MA_SUCCESS)
		{
			return false;
		}
		ma_sound_set_volume(sound.get(), clampVolume(volume));
		ma_sound_set_pitch(sound.get(), clampPitch(pitch));
		ma_sound_set_spatialization_enabled(sound.get(), MA_TRUE);
		ma_sound_set_position(sound.get(), worldPosition.x, worldPosition.y, worldPosition.z);
		ma_sound_set_min_distance(sound.get(), std::max(0.01F, minDistance));
		ma_sound_set_max_distance(sound.get(), std::max(minDistance, maxDistance));
		if (ma_sound_start(sound.get()) != MA_SUCCESS)
		{
			return false;
		}
		impl_->voices.push_back(std::move(sound));
		return true;
	}

	void AudioEngine::setListener(const glm::vec3& worldPosition, const glm::vec3& forward)
	{
		if (!isAvailable())
		{
			return;
		}
		ma_engine_listener_set_position(&impl_->engine, 0, worldPosition.x, worldPosition.y, worldPosition.z);
		glm::vec3 dir = forward;
		if (glm::dot(dir, dir) < 0.0001F)
		{
			dir = glm::vec3(0.0F, 0.0F, 1.0F);
		}
		else
		{
			dir = glm::normalize(dir);
		}
		ma_engine_listener_set_direction(&impl_->engine, 0, dir.x, dir.y, dir.z);
	}

	void AudioEngine::stopAll()
	{
		if (!impl_)
		{
			return;
		}
		impl_->voices.clear();
	}

	void AudioEngine::stopPreview()
	{
		if (!impl_)
		{
			return;
		}
		impl_->preview.reset();
	}

	bool AudioEngine::playPreview(
		const std::filesystem::path& projectRoot, const std::string& clipRelativePath, const float volume)
	{
		stopPreview();
		const std::optional<std::filesystem::path> resolved = resolveClip(projectRoot, clipRelativePath);
		if (!resolved.has_value())
		{
			return false;
		}
		if (!isAvailable())
		{
			return true;
		}
		auto sound = std::unique_ptr<ma_sound, void (*)(ma_sound*)>(new ma_sound{}, uninitSound);
		if (ma_sound_init_from_file(
				&impl_->engine, resolved->string().c_str(), 0, nullptr, nullptr, sound.get()) != MA_SUCCESS)
		{
			return false;
		}
		ma_sound_set_volume(sound.get(), clampVolume(volume));
		ma_sound_set_spatialization_enabled(sound.get(), MA_FALSE);
		if (ma_sound_start(sound.get()) != MA_SUCCESS)
		{
			return false;
		}
		impl_->preview = std::move(sound);
		return true;
	}

	float AudioEngine::clipDurationSeconds(
		const std::filesystem::path& projectRoot, const std::string& clipRelativePath) const
	{
		const std::optional<std::filesystem::path> resolved = resolveClip(projectRoot, clipRelativePath);
		if (!resolved.has_value())
		{
			return 0.0F;
		}
		ma_decoder decoder{};
		if (ma_decoder_init_file(resolved->string().c_str(), nullptr, &decoder) != MA_SUCCESS)
		{
			return 0.0F;
		}
		ma_uint64 frameCount = 0;
		const ma_result lengthResult = ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount);
		const ma_uint32 sampleRate = decoder.outputSampleRate;
		ma_decoder_uninit(&decoder);
		if (lengthResult != MA_SUCCESS || sampleRate == 0)
		{
			return 0.0F;
		}
		return static_cast<float>(frameCount) / static_cast<float>(sampleRate);
	}
}

namespace gameforger::editor
{
	void fireAudioHooks(
		core::AudioEngine& audio,
		const std::filesystem::path& projectRoot,
		const std::vector<AudioHook>& hooks,
		const AudioHook::Event event)
	{
		for (const AudioHook& hook : hooks)
		{
			if (hook.event == event && !hook.clipPath.empty())
			{
				(void)audio.play(projectRoot, hook.clipPath, hook.volume, 1.0F, hook.loop);
			}
		}
	}
}
