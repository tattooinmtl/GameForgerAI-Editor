#include "GameForger/Editor/AudioSourceEffects.hpp"

namespace gameforger::editor
{
	core::EffectSettings toEngineEffectSettings(const AudioSourceData& source)
	{
		core::EffectSettings settings;
		const AudioEffects& fx = source.effects;
		settings.reverb = fx.reverb;
		settings.reverbRoomSize = fx.reverbRoomSize;
		settings.reverbDamping = fx.reverbDamping;
		settings.reverbWet = fx.reverbWet;
		settings.reverbDry = fx.reverbDry;
		settings.delay = fx.delay;
		settings.delaySeconds = fx.delaySeconds;
		settings.delayDecay = fx.delayDecay;
		switch (fx.filter)
		{
			case AudioEffects::Filter::LowPass:
				settings.filter = core::EffectSettings::Filter::LowPass;
				break;
			case AudioEffects::Filter::HighPass:
				settings.filter = core::EffectSettings::Filter::HighPass;
				break;
			case AudioEffects::Filter::None:
				settings.filter = core::EffectSettings::Filter::None;
				break;
		}
		settings.cutoffHz = fx.cutoffHz;
		settings.fadeInSeconds = source.fadeInSeconds;
		settings.fadeOutSeconds = source.fadeOutSeconds;
		return settings;
	}

	void playSourcesOnAwake(
		core::AudioEngine& audio, const std::filesystem::path& projectRoot, const EditorScene& scene)
	{
		for (const SceneEntity& entity : scene.entities())
		{
			if (!entity.hasAudioSource || !entity.audioSource.playOnAwake ||
				entity.audioSource.clipAssetPath.empty())
			{
				continue;
			}

			// Failures are swallowed on purpose. A missing clip must not stop
			// the other sources from starting, and there is no sensible place
			// to report it from here - the editor logs it through the panel,
			// and the shipped game has no console to log to.
			const core::EffectSettings effects = toEngineEffectSettings(entity.audioSource);
			if (entity.audioSource.is3D)
			{
				(void)audio.play3DWithEffects(
					projectRoot,
					entity.audioSource.clipAssetPath,
					entity.position,
					entity.audioSource.volume,
					entity.audioSource.pitch,
					entity.audioSource.loop,
					entity.audioSource.minDistance,
					entity.audioSource.maxDistance,
					effects);
			}
			else
			{
				(void)audio.playWithEffects(
					projectRoot,
					entity.audioSource.clipAssetPath,
					entity.audioSource.volume,
					entity.audioSource.pitch,
					entity.audioSource.loop,
					effects);
			}
		}
	}
}
