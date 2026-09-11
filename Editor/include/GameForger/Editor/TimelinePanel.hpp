#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "GameForger/Core/AudioEngine.hpp"
#include "GameForger/Editor/Storyboard.hpp"

namespace gameforger::editor
{
	using TimelineLogFn = std::function<void(bool success, const std::string& message)>;

	// What the timeline is currently editing. A shot owns both a camera path
	// and its audio cues, which is why cues are only editable in shot mode -
	// there is nowhere to hang a cue on a bare entity animation.
	struct TimelinePanelState
	{
		// Whether the panel is shown. Every panel is closable and reopenable
		// from the Panels menu; before this they were drawn unconditionally,
		// so a panel could be neither hidden nor recovered.
		bool open = true;


		// Index into StoryboardState::shots, or -1 for "no shot selected".
		int shotIndex = -1;

		// Seconds. Shared with the Animation panel's scrub so the two views
		// agree rather than fighting over the same playhead.
		float playheadSeconds = 0.0F;
		float visibleDurationSeconds = 10.0F;

		bool playing = false;
		// Index of the next cue to fire while playing; reset on seek so a
		// backwards scrub replays cues instead of silently skipping them.
		int nextCueToFire = 0;

		int selectedKeyframe = -1;
		int selectedCue = -1;

		// Which clip the "Add cue at playhead" button will use, as an index
		// into the Game/Audio listing rebuilt by refreshClips().
		int newCueClip = 0;
		float newCueVolume = 1.0F;

		std::vector<std::string> clipChoices;
		bool clipChoicesLoaded = false;

		bool dockPlacementDone = false;
	};

	// Draws the "Timeline" window: a time ruler with a draggable playhead, a
	// transform track showing the selected shot's camera keyframes, and an
	// audio track showing its cues. Dragging a marker retimes it; right-click
	// deletes it.
	//
	// `shots` is mutated in place (retimed keyframes, added/removed cues).
	// Saving is the scene's job - the caller persists shots through
	// saveScene() like any other scene data.
	void drawTimelinePanel(
		std::vector<CineShot>& shots,
		core::AudioEngine& audio,
		const std::filesystem::path& projectRoot,
		TimelinePanelState& state,
		float deltaTime,
		const TimelineLogFn& log);
}
