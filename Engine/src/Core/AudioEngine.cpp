#include "GameForger/Core/AudioEngine.hpp"

// Reverb lives in miniaudio's extras/, not the single header - see the
// Engine CMakeLists comment on ma_reverb_node.c.
#include <vector>

#include "ma_reverb_node.h"

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

	// One voice's DSP chain. The nodes must outlive the ma_sound that feeds
	// them and be uninitialised AFTER it, or the sound's mixing callback
	// writes into freed memory. Relying on member declaration order for
	// that is too subtle to be safe, so teardown is explicit and the
	// destructor just calls it.
	struct EffectChain
	{
		std::unique_ptr<ma_reverb_node> reverb;
		std::unique_ptr<ma_delay_node> delay;
		std::unique_ptr<ma_lpf_node> lowPass;
		std::unique_ptr<ma_hpf_node> highPass;

		[[nodiscard]] bool empty() const noexcept
		{
			return !reverb && !delay && !lowPass && !highPass;
		}

		// Reverse signal order: whatever is nearest the endpoint goes last.
		void uninitAll()
		{
			if (reverb)   { ma_reverb_node_uninit(reverb.get(), nullptr);   reverb.reset(); }
			if (delay)    { ma_delay_node_uninit(delay.get(), nullptr);     delay.reset(); }
			if (lowPass)  { ma_lpf_node_uninit(lowPass.get(), nullptr);     lowPass.reset(); }
			if (highPass) { ma_hpf_node_uninit(highPass.get(), nullptr);    highPass.reset(); }
		}

		~EffectChain() { uninitAll(); }

		EffectChain() = default;
		EffectChain(EffectChain&&) noexcept = default;
		EffectChain& operator=(EffectChain&&) noexcept = default;
		EffectChain(const EffectChain&) = delete;
		EffectChain& operator=(const EffectChain&) = delete;
	};

	struct AudioEngine::Impl
	{
		ma_engine engine{};
		bool engineReady = false;
		float masterVolume = 1.0F;

		// The clip each voice came from, so a script can stop or query its own
		// sound instead of every sound at once.
		struct Voice
		{
			std::unique_ptr<ma_sound, void (*)(ma_sound*)> sound{nullptr, uninitSound};
			std::string clipPath;
			EffectChain effects;

			// Sound first, then the nodes it fed. Doing this by hand rather
			// than leaving it to member order, which would tear down in
			// reverse-declaration order and free the nodes underneath a
			// still-running sound.
			~Voice()
			{
				sound.reset();
				effects.uninitAll();
			}

			Voice() = default;
			Voice(Voice&&) noexcept = default;
			Voice& operator=(Voice&&) noexcept = default;
			Voice(const Voice&) = delete;
			Voice& operator=(const Voice&) = delete;
		};
		std::vector<Voice> voices;
		std::unique_ptr<ma_sound, void (*)(ma_sound*)> preview{nullptr, uninitSound};

		void pruneFinished()
		{
			voices.erase(
				std::remove_if(
					voices.begin(),
					voices.end(),
					[](const Voice& voice)
					{
						return voice.sound == nullptr || !ma_sound_is_playing(voice.sound.get());
					}),
				voices.end());
		}
	};

	namespace
	{
		// Builds the chain and wires sound -> [reverb] -> [delay] -> [filter] ->
		// endpoint, attaching only the enabled nodes. Returns false and leaves
		// the chain empty if any node fails to initialise, so the caller can
		// fall back to playing dry rather than playing nothing.
		bool buildEffectChain(
			ma_engine& engine,
			ma_sound& sound,
			const EffectSettings& settings,
			EffectChain& chain)
		{
			ma_node_graph* graph = ma_engine_get_node_graph(&engine);
			ma_node* endpoint = ma_node_graph_get_endpoint(graph);
			const ma_uint32 channels = ma_engine_get_channels(&engine);
			const ma_uint32 sampleRate = ma_engine_get_sample_rate(&engine);

			// Collected in signal order, then linked in one pass below.
			std::vector<ma_node*> nodes;

			if (settings.reverb)
			{
				// The reverb node only supports mono and stereo.
				if (channels != 1 && channels != 2)
				{
					return false;
				}
				auto node = std::make_unique<ma_reverb_node>();
				ma_reverb_node_config config = ma_reverb_node_config_init(channels, sampleRate);
				config.roomSize = settings.reverbRoomSize;
				config.damping = settings.reverbDamping;
				config.wetVolume = settings.reverbWet;
				config.dryVolume = settings.reverbDry;
				if (ma_reverb_node_init(graph, &config, nullptr, node.get()) != MA_SUCCESS)
				{
					return false;
				}
				nodes.push_back(reinterpret_cast<ma_node*>(node.get()));
				chain.reverb = std::move(node);
			}

			if (settings.delay)
			{
				auto node = std::make_unique<ma_delay_node>();
				const auto delayFrames =
					static_cast<ma_uint32>(static_cast<float>(sampleRate) * settings.delaySeconds);
				ma_delay_node_config config =
					ma_delay_node_config_init(channels, sampleRate, delayFrames, settings.delayDecay);
				if (ma_delay_node_init(graph, &config, nullptr, node.get()) != MA_SUCCESS)
				{
					chain.uninitAll();
					return false;
				}
				nodes.push_back(reinterpret_cast<ma_node*>(node.get()));
				chain.delay = std::move(node);
			}

			if (settings.filter == EffectSettings::Filter::LowPass)
			{
				auto node = std::make_unique<ma_lpf_node>();
				ma_lpf_node_config config =
					ma_lpf_node_config_init(channels, sampleRate, settings.cutoffHz, 2);
				if (ma_lpf_node_init(graph, &config, nullptr, node.get()) != MA_SUCCESS)
				{
					chain.uninitAll();
					return false;
				}
				nodes.push_back(reinterpret_cast<ma_node*>(node.get()));
				chain.lowPass = std::move(node);
			}
			else if (settings.filter == EffectSettings::Filter::HighPass)
			{
				auto node = std::make_unique<ma_hpf_node>();
				ma_hpf_node_config config =
					ma_hpf_node_config_init(channels, sampleRate, settings.cutoffHz, 2);
				if (ma_hpf_node_init(graph, &config, nullptr, node.get()) != MA_SUCCESS)
				{
					chain.uninitAll();
					return false;
				}
				nodes.push_back(reinterpret_cast<ma_node*>(node.get()));
				chain.highPass = std::move(node);
			}

			if (nodes.empty())
			{
				return true; // nothing enabled - the sound stays on the endpoint
			}

			// sound -> first node, each node -> the next, last -> endpoint.
			if (ma_node_attach_output_bus(&sound, 0, nodes.front(), 0) != MA_SUCCESS)
			{
				chain.uninitAll();
				return false;
			}
			for (std::size_t i = 0; i + 1 < nodes.size(); ++i)
			{
				if (ma_node_attach_output_bus(nodes[i], 0, nodes[i + 1], 0) != MA_SUCCESS)
				{
					chain.uninitAll();
					return false;
				}
			}
			if (ma_node_attach_output_bus(nodes.back(), 0, endpoint, 0) != MA_SUCCESS)
			{
				chain.uninitAll();
				return false;
			}
			return true;
		}
	}

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
		// Voice is no longer an aggregate (it has an explicit destructor for
		// teardown ordering), so build it field by field.
		Impl::Voice voice;
		voice.sound = std::move(sound);
		voice.clipPath = clipRelativePath;
		impl_->voices.push_back(std::move(voice));
		return true;
	}

	bool AudioEngine::playWithEffects(
		const std::filesystem::path& projectRoot,
		const std::string& clipRelativePath,
		const float volume,
		const float pitch,
		const bool loop,
		const EffectSettings& effects)
	{
		// No effects enabled is by far the common case, and it must not pay for
		// a node graph it does not use.
		if (!effects.anyEnabled())
		{
			return play(projectRoot, clipRelativePath, volume, pitch, loop);
		}

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

		Impl::Voice voice;
		voice.clipPath = clipRelativePath;
		voice.sound = std::unique_ptr<ma_sound, void (*)(ma_sound*)>(new ma_sound{}, uninitSound);

		const ma_uint32 flags = loop ? MA_SOUND_FLAG_LOOPING : 0;
		if (ma_sound_init_from_file(
				&impl_->engine, resolved->string().c_str(), flags, nullptr, nullptr, voice.sound.get()) != MA_SUCCESS)
		{
			return false;
		}
		ma_sound_set_volume(voice.sound.get(), clampVolume(volume));
		ma_sound_set_pitch(voice.sound.get(), clampPitch(pitch));
		ma_sound_set_spatialization_enabled(voice.sound.get(), MA_FALSE);

		// A chain that fails to build is not fatal: play the sound dry rather
		// than silently dropping it, since a missing reverb is far less
		// surprising than a missing sound.
		(void)buildEffectChain(impl_->engine, *voice.sound, effects, voice.effects);

		// -1 as the start volume means "ramp from wherever it is now", which
		// for a sound that has not started yet is its set volume. Ramping from
		// 0 to the target is what a fade-in actually is.
		if (effects.fadeInSeconds > 0.0F)
		{
			ma_sound_set_fade_in_milliseconds(
				voice.sound.get(), 0.0F, clampVolume(volume),
				static_cast<ma_uint64>(effects.fadeInSeconds * 1000.0F));
		}

		if (ma_sound_start(voice.sound.get()) != MA_SUCCESS)
		{
			return false;
		}
		impl_->voices.push_back(std::move(voice));
		return true;
	}

	bool AudioEngine::play3DWithEffects(
		const std::filesystem::path& projectRoot,
		const std::string& clipRelativePath,
		const glm::vec3& worldPosition,
		const float volume,
		const float pitch,
		const bool loop,
		const float minDistance,
		const float maxDistance,
		const EffectSettings& effects)
	{
		if (!effects.anyEnabled() && !effects.anyFade())
		{
			return play3D(projectRoot, clipRelativePath, worldPosition, volume, pitch, loop, minDistance, maxDistance);
		}

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

		Impl::Voice voice;
		voice.clipPath = clipRelativePath;
		voice.sound = std::unique_ptr<ma_sound, void (*)(ma_sound*)>(new ma_sound{}, uninitSound);

		const ma_uint32 flags = loop ? MA_SOUND_FLAG_LOOPING : 0;
		if (ma_sound_init_from_file(
				&impl_->engine, resolved->string().c_str(), flags, nullptr, nullptr, voice.sound.get()) != MA_SUCCESS)
		{
			return false;
		}
		ma_sound_set_volume(voice.sound.get(), clampVolume(volume));
		ma_sound_set_pitch(voice.sound.get(), clampPitch(pitch));
		ma_sound_set_spatialization_enabled(voice.sound.get(), MA_TRUE);
		ma_sound_set_position(voice.sound.get(), worldPosition.x, worldPosition.y, worldPosition.z);
		ma_sound_set_min_distance(voice.sound.get(), std::max(0.01F, minDistance));
		ma_sound_set_max_distance(voice.sound.get(), std::max(minDistance, maxDistance));

		(void)buildEffectChain(impl_->engine, *voice.sound, effects, voice.effects);

		if (effects.fadeInSeconds > 0.0F)
		{
			ma_sound_set_fade_in_milliseconds(
				voice.sound.get(), 0.0F, clampVolume(volume),
				static_cast<ma_uint64>(effects.fadeInSeconds * 1000.0F));
		}

		if (ma_sound_start(voice.sound.get()) != MA_SUCCESS)
		{
			return false;
		}
		impl_->voices.push_back(std::move(voice));
		return true;
	}

	bool AudioEngine::playPreviewWithEffects(
		const std::filesystem::path& projectRoot,
		const std::string& clipRelativePath,
		const float volume,
		const EffectSettings& effects)
	{
		// The preview voice is deliberately a normal voice here rather than the
		// dedicated preview slot: the preview slot has no effect chain attached
		// to it, and giving it one would mean duplicating the teardown ordering
		// that Voice already gets right. stopPreview() still stops the slot;
		// this is stopped by clip path.
		stop(clipRelativePath);
		return playWithEffects(projectRoot, clipRelativePath, volume, 1.0F, false, effects);
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
		// Voice is no longer an aggregate (it has an explicit destructor for
		// teardown ordering), so build it field by field.
		Impl::Voice voice;
		voice.sound = std::move(sound);
		voice.clipPath = clipRelativePath;
		impl_->voices.push_back(std::move(voice));
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

	void AudioEngine::stop(const std::string& clipRelativePath)
	{
		if (!impl_)
		{
			return;
		}
		// Erasing the unique_ptr uninits the sound, which is the stop.
		impl_->voices.erase(
			std::remove_if(
				impl_->voices.begin(),
				impl_->voices.end(),
				[&clipRelativePath](const Impl::Voice& voice)
				{
					return voice.clipPath == clipRelativePath;
				}),
			impl_->voices.end());
	}

	void AudioEngine::stopWithFade(const std::string& clipRelativePath, const float fadeSeconds)
	{
		if (!impl_ || fadeSeconds <= 0.0F)
		{
			// No fade requested is just a stop; don't leave a voice running
			// silently forever waiting for a ramp that never happens.
			stop(clipRelativePath);
			return;
		}
		const auto fadeMs = static_cast<ma_uint64>(fadeSeconds * 1000.0F);
		// set_stop_time takes an ABSOLUTE point on the engine clock, not a
		// duration - handing it the fade length alone would schedule a stop in
		// the past and cut the sound instantly.
		const ma_uint64 stopAtMs = ma_engine_get_time_in_milliseconds(&impl_->engine) + fadeMs;
		for (Impl::Voice& voice : impl_->voices)
		{
			if (voice.clipPath != clipRelativePath || voice.sound == nullptr)
			{
				continue;
			}
			// Ramp from the current volume to silence, then schedule the stop
			// at the end of the ramp. Without the scheduled stop the voice
			// would sit at zero volume forever, never pruned, holding its
			// effect nodes alive.
			ma_sound_set_fade_in_milliseconds(voice.sound.get(), -1.0F, 0.0F, fadeMs);
			ma_sound_set_stop_time_in_milliseconds(voice.sound.get(), stopAtMs);
		}
	}

	bool AudioEngine::isAnyPlaying() const
	{
		if (!impl_)
		{
			return false;
		}
		impl_->pruneFinished();
		return !impl_->voices.empty();
	}

	bool AudioEngine::isPlaying(const std::string& clipRelativePath) const
	{
		if (!impl_)
		{
			return false;
		}
		impl_->pruneFinished();
		return std::any_of(
			impl_->voices.begin(), impl_->voices.end(),
			[&clipRelativePath](const Impl::Voice& voice)
			{
				return voice.clipPath == clipRelativePath;
			});
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
	std::vector<std::string> listAudioClips(const std::filesystem::path& projectRoot)
	{
		std::vector<std::string> clips;
		const std::filesystem::path audioRoot = projectRoot / "Game" / "Audio";
		std::error_code ec;
		if (!std::filesystem::exists(audioRoot, ec))
		{
			return clips;
		}
		for (const std::filesystem::directory_entry& entry :
			std::filesystem::directory_iterator(audioRoot, ec))
		{
			if (!entry.is_regular_file(ec))
			{
				continue;
			}
			std::error_code relativeError;
			const std::filesystem::path relative =
				std::filesystem::relative(entry.path(), projectRoot, relativeError);
			if (relativeError)
			{
				continue;
			}
			const std::string generic = relative.generic_string();
			if (core::resolveProjectFile(projectRoot, generic, "Game/Audio", core::audioClipExtensions()))
			{
				clips.push_back(generic);
			}
		}
		std::sort(clips.begin(), clips.end());
		return clips;
	}

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
