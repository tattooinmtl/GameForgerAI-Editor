#include "GameForger/Editor/TimelinePanel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <system_error>

#include <imgui.h>

#include "GameForger/Editor/EditorLayout.hpp"

namespace gameforger::editor
{
	namespace
	{
		constexpr float kRulerHeight = 22.0F;
		constexpr float kTrackHeight = 34.0F;
		constexpr float kMarkerHalfWidth = 5.0F;
		// Anything shorter and the ruler degenerates into a single pixel.
		constexpr float kMinVisibleSeconds = 0.5F;

		bool isAudioFile(const std::filesystem::path& path)
		{
			std::string extension = path.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(),
				[](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return extension == ".wav" || extension == ".mp3" || extension == ".flac";
		}

		void refreshClips(TimelinePanelState& state, const std::filesystem::path& projectRoot)
		{
			state.clipChoices.clear();
			std::error_code ec;
			for (const std::filesystem::directory_entry& entry :
				std::filesystem::directory_iterator(projectRoot / "Game" / "Audio", ec))
			{
				if (entry.is_regular_file(ec) && isAudioFile(entry.path()))
				{
					std::error_code relativeError;
					const std::filesystem::path relative =
						std::filesystem::relative(entry.path(), projectRoot, relativeError);
					if (!relativeError)
					{
						state.clipChoices.push_back(relative.generic_string());
					}
				}
			}
			std::sort(state.clipChoices.begin(), state.clipChoices.end());
			state.clipChoicesLoaded = true;
		}

		// Longest keyframe/cue time in the shot, so the ruler always covers
		// the whole thing plus a little headroom to drag into.
		float shotContentDuration(const CineShot& shot)
		{
			float longest = 0.0F;
			if (!shot.cameraPath.keyframes.empty())
			{
				longest = std::max(longest, shot.cameraPath.keyframes.back().time);
			}
			for (const AudioCue& cue : shot.audioCues)
			{
				longest = std::max(longest, cue.time);
			}
			return longest;
		}

		struct TrackGeometry
		{
			float left = 0.0F;
			float width = 0.0F;
			float duration = 1.0F;

			[[nodiscard]] float timeToX(const float seconds) const
			{
				return left + (seconds / duration) * width;
			}

			[[nodiscard]] float xToTime(const float x) const
			{
				const float raw = ((x - left) / width) * duration;
				return std::clamp(raw, 0.0F, duration);
			}
		};

		void drawRuler(ImDrawList* draw, const TrackGeometry& geometry, const ImVec2 topLeft)
		{
			const ImU32 lineColour = IM_COL32(120, 130, 150, 160);
			const ImU32 textColour = IM_COL32(150, 160, 180, 220);

			// One label per second while that stays readable, otherwise
			// every 5 or 10 - a tick per second at 120s is unreadable mush.
			float step = 1.0F;
			if (geometry.duration > 60.0F)      step = 10.0F;
			else if (geometry.duration > 20.0F) step = 5.0F;

			for (float t = 0.0F; t <= geometry.duration + 0.001F; t += step)
			{
				const float x = geometry.timeToX(t);
				draw->AddLine(ImVec2(x, topLeft.y), ImVec2(x, topLeft.y + kRulerHeight), lineColour);
				std::array<char, 24> label{};
				std::snprintf(label.data(), label.size(), "%.0fs", static_cast<double>(t));
				draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
					ImVec2(x + 3.0F, topLeft.y + 3.0F), textColour, label.data());
			}
		}

		void drawTrackBackground(
			ImDrawList* draw, const TrackGeometry& geometry, const float y, const char* label)
		{
			const ImU32 background = IM_COL32(30, 34, 44, 255);
			const ImU32 border = IM_COL32(70, 78, 96, 255);
			draw->AddRectFilled(
				ImVec2(geometry.left, y), ImVec2(geometry.left + geometry.width, y + kTrackHeight), background, 3.0F);
			draw->AddRect(
				ImVec2(geometry.left, y), ImVec2(geometry.left + geometry.width, y + kTrackHeight), border, 3.0F);
			draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
				ImVec2(geometry.left + 6.0F, y + 4.0F), IM_COL32(130, 140, 160, 200), label);
		}

		// A diamond, so a marker reads as a keyframe rather than as a button.
		void drawMarker(ImDrawList* draw, const float x, const float centreY, const ImU32 colour, const bool selected)
		{
			const float half = kMarkerHalfWidth;
			const ImVec2 points[4] = {
				ImVec2(x, centreY - half),
				ImVec2(x + half, centreY),
				ImVec2(x, centreY + half),
				ImVec2(x - half, centreY),
			};
			draw->AddConvexPolyFilled(points, 4, colour);
			if (selected)
			{
				draw->AddPolyline(points, 4, IM_COL32(255, 255, 255, 235), ImDrawFlags_Closed, 2.0F);
			}
		}
	}

	void drawTimelinePanel(
		std::vector<CineShot>& shots,
		core::AudioEngine& audio,
		const std::filesystem::path& projectRoot,
		TimelinePanelState& state,
		const float deltaTime,
		const TimelineLogFn& log)
	{
		// Joining the node has to happen BEFORE Begin. DockBuilderDockWindow
		// alone does not place a window that has never been docked - it only
		// re-seats one that already has a DockId - which is why the Timeline
		// stayed floating while Project Settings (docked in an earlier
		// session, so already carrying a DockId in the ini) appeared to work.
		if (!state.dockPlacementDone)
		{
			const ImGuiID storyboardId = dockIdOfWindow("Storyboard");
			const ImGuiID timelineId = dockIdOfWindow("Timeline");
			if (storyboardId != 0)
			{
				if (timelineId != 0 && timelineId != storyboardId)
				{
					// User dragged it somewhere else. Leave it alone.
					state.dockPlacementDone = true;
				}
				else if (timelineId != storyboardId)
				{
					ImGui::SetNextWindowDockID(storyboardId, ImGuiCond_Always);
				}
			}
		}

		ImGui::Begin("Timeline");

		if (!state.clipChoicesLoaded)
		{
			refreshClips(state, projectRoot);
		}

		// Keep the selection valid across scene loads, which replace shots
		// wholesale and would otherwise leave this indexing into nothing.
		if (state.shotIndex >= static_cast<int>(shots.size()))
		{
			state.shotIndex = shots.empty() ? -1 : 0;
			state.selectedKeyframe = -1;
			state.selectedCue = -1;
		}
		if (state.shotIndex < 0 && !shots.empty())
		{
			state.shotIndex = 0;
		}

		if (shots.empty())
		{
			ImGui::TextDisabled(
				"No storyboard shots yet. Record a Cine Camera path, then use the "
				"Storyboard panel's \"Save Current Path as New Shot\".");
			ImGui::End();
			return;
		}

		// --- shot picker ---------------------------------------------------
		const std::string currentName = shots[static_cast<std::size_t>(state.shotIndex)].name;
		ImGui::SetNextItemWidth(200.0F);
		if (ImGui::BeginCombo("##Shot", currentName.c_str()))
		{
			for (int index = 0; index < static_cast<int>(shots.size()); ++index)
			{
				const bool selected = index == state.shotIndex;
				if (ImGui::Selectable(shots[static_cast<std::size_t>(index)].name.c_str(), selected))
				{
					state.shotIndex = index;
					state.selectedKeyframe = -1;
					state.selectedCue = -1;
					state.playheadSeconds = 0.0F;
					state.nextCueToFire = 0;
				}
			}
			ImGui::EndCombo();
		}

		CineShot& shot = shots[static_cast<std::size_t>(state.shotIndex)];

		ImGui::SameLine();
		if (ImGui::Button(state.playing ? "Stop" : "Play"))
		{
			state.playing = !state.playing;
			if (state.playing)
			{
				// Restart from the top when the playhead is already at the
				// end, so Play always plays something.
				if (state.playheadSeconds >= shotContentDuration(shot))
				{
					state.playheadSeconds = 0.0F;
				}
				state.nextCueToFire = 0;
			}
			else
			{
				audio.stopPreview();
			}
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%.2fs", static_cast<double>(state.playheadSeconds));
		if (audio.isSilent())
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0F, 0.75F, 0.35F, 1.0F), "(no audio device - cues are silent)");
		}

		// --- advance playback ----------------------------------------------
		const float contentDuration = shotContentDuration(shot);
		if (state.playing)
		{
			state.playheadSeconds += deltaTime;
			// Fire every cue the playhead just crossed. A loop rather than a
			// single check because a long frame can step over several.
			while (state.nextCueToFire < static_cast<int>(shot.audioCues.size()) &&
				shot.audioCues[static_cast<std::size_t>(state.nextCueToFire)].time <= state.playheadSeconds)
			{
				const AudioCue& cue = shot.audioCues[static_cast<std::size_t>(state.nextCueToFire)];
				(void)audio.playPreview(projectRoot, cue.clipPath, cue.volume);
				++state.nextCueToFire;
			}
			if (state.playheadSeconds >= contentDuration)
			{
				state.playheadSeconds = contentDuration;
				state.playing = false;
			}
		}

		// Ruler always shows the whole shot plus headroom to drag into.
		state.visibleDurationSeconds = std::max(kMinVisibleSeconds, contentDuration + 2.0F);

		// --- geometry -------------------------------------------------------
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float available = ImGui::GetContentRegionAvail().x;
		TrackGeometry geometry;
		geometry.left = origin.x + 4.0F;
		geometry.width = std::max(80.0F, available - 12.0F);
		geometry.duration = state.visibleDurationSeconds;

		const float rulerY = origin.y;
		const float transformY = rulerY + kRulerHeight + 4.0F;
		const float audioY = transformY + kTrackHeight + 6.0F;
		const float totalHeight = (audioY + kTrackHeight) - rulerY + 8.0F;

		ImDrawList* draw = ImGui::GetWindowDrawList();
		drawRuler(draw, geometry, ImVec2(geometry.left, rulerY));
		drawTrackBackground(draw, geometry, transformY, "Camera");
		drawTrackBackground(draw, geometry, audioY, "Audio");

		// One invisible button covers the whole strip so the tracks can be
		// interacted with without ImGui items fighting the custom drawing.
		ImGui::InvisibleButton("##TimelineCanvas", ImVec2(geometry.width, totalHeight));
		const bool canvasHovered = ImGui::IsItemHovered();
		const ImVec2 mouse = ImGui::GetIO().MousePos;

		// --- markers ---------------------------------------------------------
		const float transformCentre = transformY + kTrackHeight * 0.5F;
		const float audioCentre = audioY + kTrackHeight * 0.5F;

		int hoveredKeyframe = -1;
		for (int index = 0; index < static_cast<int>(shot.cameraPath.keyframes.size()); ++index)
		{
			const float x = geometry.timeToX(shot.cameraPath.keyframes[static_cast<std::size_t>(index)].time);
			const bool hovered = canvasHovered &&
				std::abs(mouse.x - x) <= kMarkerHalfWidth + 2.0F &&
				std::abs(mouse.y - transformCentre) <= kTrackHeight * 0.5F;
			if (hovered)
			{
				hoveredKeyframe = index;
			}
			drawMarker(draw, x, transformCentre,
				hovered ? IM_COL32(150, 210, 255, 255) : IM_COL32(90, 170, 240, 235),
				index == state.selectedKeyframe);
		}

		int hoveredCue = -1;
		for (int index = 0; index < static_cast<int>(shot.audioCues.size()); ++index)
		{
			const float x = geometry.timeToX(shot.audioCues[static_cast<std::size_t>(index)].time);
			const bool hovered = canvasHovered &&
				std::abs(mouse.x - x) <= kMarkerHalfWidth + 2.0F &&
				std::abs(mouse.y - audioCentre) <= kTrackHeight * 0.5F;
			if (hovered)
			{
				hoveredCue = index;
			}
			drawMarker(draw, x, audioCentre,
				hovered ? IM_COL32(255, 210, 140, 255) : IM_COL32(240, 165, 70, 235),
				index == state.selectedCue);
		}

		// --- playhead ---------------------------------------------------------
		const float playheadX = geometry.timeToX(state.playheadSeconds);
		draw->AddLine(ImVec2(playheadX, rulerY), ImVec2(playheadX, audioY + kTrackHeight),
			IM_COL32(255, 90, 90, 230), 1.5F);

		// --- interaction ------------------------------------------------------
		if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			if (hoveredKeyframe >= 0)
			{
				state.selectedKeyframe = hoveredKeyframe;
				state.selectedCue = -1;
			}
			else if (hoveredCue >= 0)
			{
				state.selectedCue = hoveredCue;
				state.selectedKeyframe = -1;
			}
			else
			{
				// Clicking empty track scrubs. Rewinding must replay cues, so
				// recompute which cue comes next rather than leaving the
				// counter where playback left it.
				state.playheadSeconds = geometry.xToTime(mouse.x);
				state.nextCueToFire = 0;
				while (state.nextCueToFire < static_cast<int>(shot.audioCues.size()) &&
					shot.audioCues[static_cast<std::size_t>(state.nextCueToFire)].time < state.playheadSeconds)
				{
					++state.nextCueToFire;
				}
			}
		}

		// Dragging a selected marker retimes it.
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && canvasHovered)
		{
			const float draggedTime = geometry.xToTime(mouse.x);
			if (state.selectedKeyframe >= 0 &&
				state.selectedKeyframe < static_cast<int>(shot.cameraPath.keyframes.size()))
			{
				shot.cameraPath.keyframes[static_cast<std::size_t>(state.selectedKeyframe)].time = draggedTime;
				// Keyframes must stay ordered by time; find where this one
				// landed so the selection follows it rather than jumping to
				// whatever else now sits at that index.
				TransformKeyframe moved = shot.cameraPath.keyframes[static_cast<std::size_t>(state.selectedKeyframe)];
				std::stable_sort(shot.cameraPath.keyframes.begin(), shot.cameraPath.keyframes.end(),
					[](const TransformKeyframe& a, const TransformKeyframe& b) { return a.time < b.time; });
				for (int i = 0; i < static_cast<int>(shot.cameraPath.keyframes.size()); ++i)
				{
					if (std::abs(shot.cameraPath.keyframes[static_cast<std::size_t>(i)].time - moved.time) < 1e-6F)
					{
						state.selectedKeyframe = i;
						break;
					}
				}
			}
			else if (state.selectedCue >= 0 && state.selectedCue < static_cast<int>(shot.audioCues.size()))
			{
				shot.audioCues[static_cast<std::size_t>(state.selectedCue)].time = draggedTime;
				state.selectedCue = static_cast<int>(
					resortAudioCues(shot.audioCues, static_cast<std::size_t>(state.selectedCue)));
			}
		}

		// Right-click deletes whatever is under the cursor.
		if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			if (hoveredCue >= 0)
			{
				shot.audioCues.erase(shot.audioCues.begin() + hoveredCue);
				state.selectedCue = -1;
				state.nextCueToFire = 0;
				if (log) log(true, "Removed audio cue.");
			}
			else if (hoveredKeyframe >= 0)
			{
				shot.cameraPath.keyframes.erase(shot.cameraPath.keyframes.begin() + hoveredKeyframe);
				state.selectedKeyframe = -1;
				if (log) log(true, "Removed camera keyframe.");
			}
		}

		ImGui::Dummy(ImVec2(0.0F, 4.0F));

		// --- add a cue --------------------------------------------------------
		ImGui::Separator();
		if (state.clipChoices.empty())
		{
			ImGui::TextDisabled("Put .wav/.mp3/.flac files in Game/Audio to add cues.");
			if (ImGui::SmallButton("Rescan Game/Audio"))
			{
				refreshClips(state, projectRoot);
			}
		}
		else
		{
			state.newCueClip = std::clamp(state.newCueClip, 0, static_cast<int>(state.clipChoices.size()) - 1);
			ImGui::SetNextItemWidth(220.0F);
			if (ImGui::BeginCombo("##CueClip", state.clipChoices[static_cast<std::size_t>(state.newCueClip)].c_str()))
			{
				for (int index = 0; index < static_cast<int>(state.clipChoices.size()); ++index)
				{
					if (ImGui::Selectable(state.clipChoices[static_cast<std::size_t>(index)].c_str(),
							index == state.newCueClip))
					{
						state.newCueClip = index;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(90.0F);
			ImGui::DragFloat("##CueVolume", &state.newCueVolume, 0.02F, 0.0F, 4.0F, "vol %.2f");
			ImGui::SameLine();
			if (ImGui::Button("Add Cue at Playhead"))
			{
				AudioCue cue;
				cue.time = state.playheadSeconds;
				cue.clipPath = state.clipChoices[static_cast<std::size_t>(state.newCueClip)];
				cue.volume = state.newCueVolume;
				state.selectedCue = static_cast<int>(insertAudioCueSorted(shot.audioCues, std::move(cue)));
				state.nextCueToFire = 0;
				if (log) log(true, "Added audio cue. Save the scene to keep it.");
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Rescan"))
			{
				refreshClips(state, projectRoot);
			}
		}

		ImGui::TextDisabled(
			"Click a track to scrub - drag a marker to retime it - right-click a marker to delete it.");

		ImGui::End();

		// Ordering only - joining the node happened before Begin above. Once
		// the tab sits directly after Storyboard this stops running.
		if (!state.dockPlacementDone)
		{
			const ImGuiID storyboardId = dockIdOfWindow("Storyboard");
			if (storyboardId != 0 && dockIdOfWindow("Timeline") == storyboardId &&
				dockWindowAfter("Timeline", "Storyboard"))
			{
				state.dockPlacementDone = true;
			}
		}
	}
}
