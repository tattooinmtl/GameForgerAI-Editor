#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "GameForger/Core/FrameProfiler.hpp"

namespace gameforger::editor
{
	struct PerformancePanelState
	{
		// Whether the panel is shown. Every panel is closable and reopenable
		// from the Panels menu; before this they were drawn unconditionally,
		// so a panel could be neither hidden nor recovered.
		bool open = true;


		// Frame time above which a frame counts as a dip, in milliseconds.
		// 20ms = missing 50fps. Editable in the panel.
		float dipThresholdMs = 20.0F;
		// Index into the worst-frames list, or -1 for "show the live frame".
		// Selecting a dip freezes its breakdown so it can be read after the
		// fact - the whole point, since a dip is gone by the time you look.
		int selectedDip = -1;
		bool dockPlacementDone = false;
		// Path of the most recent export, echoed in the panel so the file is
		// findable without digging through the Console backlog.
		std::string lastExportPath;
	};

	using PerformanceLogFn = std::function<void(bool success, const std::string& message)>;

	// Draws the "Performance" window: live FPS, a frame-time graph, the current
	// frame's cost split by zone, and the recorded dips with the zone that
	// dominated each one.
	void drawPerformancePanel(
		core::FrameProfiler& profiler,
		const std::filesystem::path& projectRoot,
		PerformancePanelState& state,
		const PerformanceLogFn& log);
}
