#include "GameForger/Editor/PerformancePanel.hpp"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <array>
#include <cstdio>
#include <vector>

#include <imgui.h>

#include "GameForger/Editor/EditorLayout.hpp"

namespace gameforger::editor
{
	namespace
	{
		// Green under budget, amber approaching it, red over.
		ImVec4 costColour(const float milliseconds, const float thresholdMs)
		{
			if (milliseconds >= thresholdMs)          return ImVec4(1.00F, 0.45F, 0.40F, 1.0F);
			if (milliseconds >= thresholdMs * 0.6F)   return ImVec4(1.00F, 0.80F, 0.40F, 1.0F);
			return ImVec4(0.55F, 0.85F, 0.60F, 1.0F);
		}

		void drawZoneTable(const core::FrameProfiler::Frame& frame, const float thresholdMs)
		{
			if (frame.zones.empty())
			{
				ImGui::TextDisabled("(no zones recorded for this frame)");
				return;
			}
			if (!ImGui::BeginTable("##Zones", 3,
					ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
			{
				return;
			}
			ImGui::TableSetupColumn("Zone", ImGuiTableColumnFlags_WidthStretch, 0.5F);
			ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthStretch, 0.2F);
			ImGui::TableSetupColumn("share", ImGuiTableColumnFlags_WidthStretch, 0.3F);
			ImGui::TableHeadersRow();

			for (const core::FrameProfiler::Zone& zone : frame.zones)
			{
				const float share = frame.totalMilliseconds > 0.0001F
					? zone.milliseconds / frame.totalMilliseconds
					: 0.0F;
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(zone.name.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::TextColored(costColour(zone.milliseconds, thresholdMs), "%.2f", static_cast<double>(zone.milliseconds));
				ImGui::TableSetColumnIndex(2);
				ImGui::ProgressBar(share, ImVec2(-1.0F, 0.0F));
			}
			ImGui::EndTable();
		}
	}

	void drawPerformancePanel(
		core::FrameProfiler& profiler,
		const std::filesystem::path& projectRoot,
		PerformancePanelState& state,
		const PerformanceLogFn& log)
	{
		// Same non-invasive placement the other new panels use: join the
		// Console's node only if this window has never been docked, and stop
		// asking the moment it is moved.
		if (!state.dockPlacementDone)
		{
			const ImGuiID consoleId = dockIdOfWindow("Console");
			const ImGuiID selfId = dockIdOfWindow("Performance");
			if (consoleId != 0)
			{
				if (selfId != 0 && selfId != consoleId)
				{
					state.dockPlacementDone = true;
				}
				else if (selfId != consoleId)
				{
					ImGui::SetNextWindowDockID(consoleId, ImGuiCond_Always);
				}
				else
				{
					state.dockPlacementDone = true;
				}
			}
		}

		if (!state.open)
		{
			return;
		}
		ImGui::Begin("Performance", &state.open);

		const float averageMs = profiler.averageMilliseconds();
		const float worstMs = profiler.worstMillisecondsInWindow();
		const core::FrameProfiler::Frame& live = profiler.lastFrame();

		ImGui::Text("%.1f FPS", static_cast<double>(profiler.averageFps()));
		ImGui::SameLine();
		ImGui::TextColored(costColour(live.totalMilliseconds, state.dipThresholdMs),
			"   this frame %.2f ms", static_cast<double>(live.totalMilliseconds));
		ImGui::SameLine();
		ImGui::TextDisabled("   avg %.2f  worst %.2f", static_cast<double>(averageMs), static_cast<double>(worstMs));

		// --- frame-time graph ------------------------------------------------
		{
			const std::deque<core::FrameProfiler::Frame>& history = profiler.history();
			std::vector<float> samples;
			samples.reserve(history.size());
			for (const core::FrameProfiler::Frame& frame : history)
			{
				samples.push_back(frame.totalMilliseconds);
			}
			if (!samples.empty())
			{
				// Scale to the worst sample so a dip is visible rather than
				// clipped flat at the top of the plot.
				const float ceiling = std::max(state.dipThresholdMs * 1.5F, worstMs * 1.1F);
				std::array<char, 64> overlay{};
				std::snprintf(overlay.data(), overlay.size(), "frame ms (last %zu frames)", samples.size());
				ImGui::PlotLines("##FrameTimes", samples.data(), static_cast<int>(samples.size()),
					0, overlay.data(), 0.0F, ceiling, ImVec2(-1.0F, 70.0F));
			}
			else
			{
				ImGui::TextDisabled("(collecting frames...)");
			}
		}

		ImGui::SetNextItemWidth(160.0F);
		if (ImGui::DragFloat("Dip threshold (ms)", &state.dipThresholdMs, 0.5F, 5.0F, 200.0F, "%.1f"))
		{
			profiler.setDipThresholdMs(state.dipThresholdMs);
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip(
				"Any frame slower than this is recorded below with its full\n"
				"breakdown. 16.7ms = 60fps, 20ms = 50fps, 33.3ms = 30fps.");
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear dips"))
		{
			profiler.clearWorst();
			state.selectedDip = -1;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%zu recorded", profiler.dipCount());
		ImGui::SameLine();
		if (ImGui::Button("Export Report"))
		{
			// Reports/ at the project root rather than under Game/: this is
			// diagnostic output about the editor, not game content, and it
			// should never end up shipped inside a build.
			const std::filesystem::path reportDir = projectRoot / "Reports";
			std::error_code ec;
			std::filesystem::create_directories(reportDir, ec);
			if (ec)
			{
				if (log) log(false, "Could not create " + reportDir.string() + ": " + ec.message());
			}
			else
			{
				std::array<char, 64> stamp{};
				const std::time_t now = std::time(nullptr);
				std::tm local{};
				localtime_s(&local, &now);
				std::strftime(stamp.data(), stamp.size(), "%Y%m%d-%H%M%S", &local);

				const std::filesystem::path file =
					reportDir / ("performance-" + std::string(stamp.data()) + ".md");
				std::ofstream out(file, std::ios::binary | std::ios::trunc);
				if (!out)
				{
					if (log) log(false, "Could not write " + file.string());
				}
				else
				{
					out << profiler.buildReport("GameForgerAI frame report");
					out.flush();
					if (out)
					{
						state.lastExportPath = file.generic_string();
						if (log) log(true, "Wrote " + state.lastExportPath);
					}
					else if (log)
					{
						log(false, "Failed while writing " + file.string());
					}
				}
			}
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip(
				"Writes Reports/performance-<timestamp>.md: summary, average cost\n"
				"per zone, every recorded dip with its full breakdown, and the raw\n"
				"frame series.");
		}
		if (!state.lastExportPath.empty())
		{
			ImGui::TextDisabled("Last export: %s", state.lastExportPath.c_str());
		}

		ImGui::Separator();

        // --- dips ------------------------------------------------------------
		const std::vector<core::FrameProfiler::Frame>& worst = profiler.worstFrames();
		ImGui::TextUnformatted("Worst frames");
		if (worst.empty())
		{
			ImGui::TextDisabled("(none over the threshold yet)");
		}
		else if (ImGui::BeginTable("##Dips", 3,
					ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("at", ImGuiTableColumnFlags_WidthStretch, 0.22F);
			ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthStretch, 0.18F);
			ImGui::TableSetupColumn("dominated by", ImGuiTableColumnFlags_WidthStretch, 0.60F);
			ImGui::TableHeadersRow();

			for (int index = 0; index < static_cast<int>(worst.size()); ++index)
			{
				const core::FrameProfiler::Frame& frame = worst[static_cast<std::size_t>(index)];
				ImGui::TableNextRow();
				ImGui::PushID(index);

				ImGui::TableSetColumnIndex(0);
				std::array<char, 32> label{};
				std::snprintf(label.data(), label.size(), "%.1fs", frame.atSeconds);
				if (ImGui::Selectable(label.data(), state.selectedDip == index, ImGuiSelectableFlags_SpanAllColumns))
				{
					// Toggle: clicking the selected row returns to the live frame.
					state.selectedDip = (state.selectedDip == index) ? -1 : index;
				}

				ImGui::TableSetColumnIndex(1);
				ImGui::TextColored(costColour(frame.totalMilliseconds, state.dipThresholdMs),
					"%.1f", static_cast<double>(frame.totalMilliseconds));

				ImGui::TableSetColumnIndex(2);
				// Zones are sorted descending, so [0] is the culprit.
				if (frame.zones.empty())
				{
					ImGui::TextDisabled("-");
				}
				else
				{
					ImGui::Text("%s  (%.1f ms)", frame.zones.front().name.c_str(),
						static_cast<double>(frame.zones.front().milliseconds));
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		ImGui::Separator();

		// --- breakdown --------------------------------------------------------
		const bool showingDip =
			state.selectedDip >= 0 && state.selectedDip < static_cast<int>(worst.size());
		if (showingDip)
		{
			const core::FrameProfiler::Frame& frame = worst[static_cast<std::size_t>(state.selectedDip)];
			ImGui::TextColored(ImVec4(1.0F, 0.75F, 0.35F, 1.0F),
				"Breakdown of the dip at %.1fs (%.2f ms) - click it again for the live frame",
				frame.atSeconds, static_cast<double>(frame.totalMilliseconds));
			drawZoneTable(frame, state.dipThresholdMs);
		}
		else
		{
			ImGui::TextDisabled("Breakdown of the live frame - click a dip above to freeze it");
			drawZoneTable(live, state.dipThresholdMs);
		}

		ImGui::TextDisabled(
			"Present includes the wait for vsync, so it is normally the largest\n"
			"zone and is not itself a problem.");

		ImGui::End();
	}
}
