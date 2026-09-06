#pragma once

#include <string>
#include <vector>

#include "GameForger/Editor/AnimationData.hpp"

namespace gameforger::editor
{
	// A sound fired at a point along a shot's timeline. Time is in seconds
	// from the start of the shot, matching TransformKeyframe::time, so the
	// camera path and its audio share one clock.
	struct AudioCue
	{
		float time = 0.0F;
		// Relative to the project root, under Game/Audio - resolved through
		// core::resolveProjectFile at play time like every other asset path.
		std::string clipPath;
		float volume = 1.0F;
	};

	// A "shot" is a cine-camera's captured path (not a full scene snapshot),
	// plus the audio cues timed against it. The Storyboard panel lists these
	// by number and plays them back in sequence as "the movie".
	//
	// Moved out of main.cpp so it can be serialised: shots used to live only
	// in session state and were lost on restart, which made the whole
	// storyboard feature unusable for real work.
	struct CineShot
	{
		std::string name;
		EntityAnimation cameraPath;
		std::vector<AudioCue> audioCues; // kept sorted by time
	};

	// Insert keeping `audioCues` sorted, and return the index it landed at.
	// The timeline drags cues around freely, and playback assumes sorted
	// order, so every mutation goes through here rather than push_back.
	[[nodiscard]] std::size_t insertAudioCueSorted(std::vector<AudioCue>& cues, AudioCue cue);

	// Re-sort after a drag changed a cue's time. Returns the new index of the
	// cue that was at `movedIndex`, so a caller holding a selection can follow
	// it rather than silently selecting a different cue.
	[[nodiscard]] std::size_t resortAudioCues(std::vector<AudioCue>& cues, std::size_t movedIndex);
}
