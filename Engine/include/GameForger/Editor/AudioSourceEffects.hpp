#pragma once

#include "GameForger/Core/AudioEngine.hpp"
#include "GameForger/Editor/EditorScene.hpp"

namespace gameforger::editor
{
	// An entity's authored audio settings -> the engine's own copy of them.
	//
	// Core deliberately knows nothing about editor::AudioSourceData, so
	// something has to translate. This lives in Engine rather than in the
	// Editor target because three callers need it and only one of them is the
	// editor: the Audio panel's Preview, the editor's Play mode, and the
	// standalone Runtime. A private copy in each is how Preview and Play came
	// to disagree in the first place.
	[[nodiscard]] core::EffectSettings toEngineEffectSettings(const AudioSourceData& source);

	// Plays every entity whose Inspector has both "Has Audio Source" and
	// "Play On Awake" ticked, with that source's effects and fades.
	//
	// Shared for the same reason: the editor's Play button and the shipped
	// game must start a scene sounding identical, and that only holds while
	// there is one implementation of what "start the scene" means.
	void playSourcesOnAwake(
		core::AudioEngine& audio, const std::filesystem::path& projectRoot, const EditorScene& scene);
}
