#include "GameForger/Core/FrameProfiler.hpp"

#include <algorithm>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace gameforger::core
{
	namespace
	{
		float millisecondsBetween(
			const std::chrono::steady_clock::time_point from,
			const std::chrono::steady_clock::time_point to)
		{
			return std::chrono::duration<float, std::milli>(to - from).count();
		}
	}

	void FrameProfiler::beginFrame()
	{
		frameStart_ = Clock::now();
		if (!epochSet_)
		{
			epoch_ = frameStart_;
			epochSet_ = true;
		}
		current_ = Frame{};
		current_.atSeconds =
			std::chrono::duration<double>(frameStart_ - epoch_).count();
		inFrame_ = true;
		openZone_.clear();
	}

	void FrameProfiler::beginZone(const std::string& name)
	{
		if (!inFrame_)
		{
			return;
		}
		// Zones are flat: opening one while another is live would double-count
		// the overlap, so close the previous first rather than nest silently.
		if (!openZone_.empty())
		{
			endZone();
		}
		openZone_ = name;
		zoneStart_ = Clock::now();
	}

	void FrameProfiler::endZone()
	{
		if (!inFrame_ || openZone_.empty())
		{
			return;
		}
		const float elapsed = millisecondsBetween(zoneStart_, Clock::now());

		// Accumulate against an existing entry so a zone entered several times
		// in one frame reports its total, not just the last visit.
		const auto found = std::find_if(
			current_.zones.begin(), current_.zones.end(),
			[this](const Zone& zone) { return zone.name == openZone_; });
		if (found != current_.zones.end())
		{
			found->milliseconds += elapsed;
		}
		else
		{
			current_.zones.push_back(Zone{openZone_, elapsed});
		}
		openZone_.clear();
	}

	void FrameProfiler::endFrame()
	{
		if (!inFrame_)
		{
			return;
		}
		if (!openZone_.empty())
		{
			endZone();
		}
		current_.totalMilliseconds = millisecondsBetween(frameStart_, Clock::now());

		// Biggest cost first - the answer to "what made this frame slow" should
		// be the first row, not something to hunt for.
		std::sort(
			current_.zones.begin(), current_.zones.end(),
			[](const Zone& a, const Zone& b) { return a.milliseconds > b.milliseconds; });

		history_.push_back(current_);
		while (history_.size() > kHistoryCapacity)
		{
			history_.pop_front();
		}

		if (current_.totalMilliseconds >= dipThresholdMs_)
		{
			++dipCount_;
			worst_.push_back(current_);
			std::sort(
				worst_.begin(), worst_.end(),
				[](const Frame& a, const Frame& b) { return a.totalMilliseconds > b.totalMilliseconds; });
			if (worst_.size() > kWorstCapacity)
			{
				worst_.resize(kWorstCapacity);
			}
		}

		lastFrame_ = std::move(current_);
		current_ = Frame{};
		inFrame_ = false;
	}

	float FrameProfiler::averageMilliseconds() const
	{
		if (history_.empty())
		{
			return 0.0F;
		}
		const float total = std::accumulate(
			history_.begin(), history_.end(), 0.0F,
			[](const float sum, const Frame& frame) { return sum + frame.totalMilliseconds; });
		return total / static_cast<float>(history_.size());
	}

	float FrameProfiler::worstMillisecondsInWindow() const
	{
		float worst = 0.0F;
		for (const Frame& frame : history_)
		{
			worst = std::max(worst, frame.totalMilliseconds);
		}
		return worst;
	}

	float FrameProfiler::averageFps() const
	{
		const float average = averageMilliseconds();
		return average > 0.0001F ? 1000.0F / average : 0.0F;
	}

	void FrameProfiler::clearWorst()
	{
		worst_.clear();
		dipCount_ = 0;
	}

	void FrameProfiler::reset()
	{
		history_.clear();
		worst_.clear();
		current_ = Frame{};
		lastFrame_ = Frame{};
		openZone_.clear();
		inFrame_ = false;
		epochSet_ = false;
		dipCount_ = 0;
	}
	std::vector<FrameProfiler::Zone> FrameProfiler::averageZoneCosts() const
	{
		std::vector<Zone> totals;
		for (const Frame& frame : history_)
		{
			for (const Zone& zone : frame.zones)
			{
				const auto found = std::find_if(
					totals.begin(), totals.end(),
					[&zone](const Zone& sum) { return sum.name == zone.name; });
				if (found != totals.end())
				{
					found->milliseconds += zone.milliseconds;
				}
				else
				{
					totals.push_back(zone);
				}
			}
		}
		if (!history_.empty())
		{
			// Divided by TOTAL frames, not by how many frames the zone appeared
			// in - a zone that only costs anything occasionally should read as
			// cheap on average, which is the honest picture.
			const auto frameCount = static_cast<float>(history_.size());
			for (Zone& zone : totals)
			{
				zone.milliseconds /= frameCount;
			}
		}
		std::sort(
			totals.begin(), totals.end(),
			[](const Zone& a, const Zone& b) { return a.milliseconds > b.milliseconds; });
		return totals;
	}

	std::string FrameProfiler::buildReport(const std::string& title) const
	{
		std::ostringstream out;
		out << std::fixed << std::setprecision(2);

		out << "# " << title << "\n\n";

		if (history_.empty())
		{
			out << "No frames were recorded.\n";
			return out.str();
		}

		const double spanSeconds = history_.back().atSeconds - history_.front().atSeconds;

		out << "## Summary\n\n";
		out << "| Metric | Value |\n|---|---|\n";
		out << "| Frames sampled | " << history_.size() << " |\n";
		out << "| Window | " << spanSeconds << " s |\n";
		out << "| Average | " << averageMilliseconds() << " ms (" << averageFps() << " FPS) |\n";
		out << "| Worst in window | " << worstMillisecondsInWindow() << " ms |\n";
		out << "| Dip threshold | " << dipThresholdMs_ << " ms |\n";
		out << "| Dips recorded | " << dipCount_ << " |\n\n";

		out << "## Average cost per zone\n\n";
		out << "Mean over every sampled frame. `Present (vsync wait)` is idle time\n"
			   "waiting for the display, not work - a large value there is normal and\n"
			   "means the frame finished early.\n\n";
		out << "| Zone | Avg ms | Share |\n|---|---|---|\n";
		const float averageTotal = averageMilliseconds();
		for (const Zone& zone : averageZoneCosts())
		{
			const float share = averageTotal > 0.0001F ? (zone.milliseconds / averageTotal) * 100.0F : 0.0F;
			out << "| " << zone.name << " | " << zone.milliseconds << " | " << share << "% |\n";
		}
		out << "\n";

		out << "## Dips\n\n";
		if (worst_.empty())
		{
			out << "No frame exceeded " << dipThresholdMs_ << " ms.\n\n";
		}
		else
		{
			out << "Slowest frames, worst first, each with the full breakdown captured\n"
				   "at the time it happened.\n\n";
			for (std::size_t index = 0; index < worst_.size(); ++index)
			{
				const Frame& frame = worst_[index];
				out << "### Dip " << (index + 1) << " - " << frame.totalMilliseconds
					<< " ms at " << frame.atSeconds << " s\n\n";
				if (frame.zones.empty())
				{
					out << "No zones recorded for this frame.\n\n";
					continue;
				}
				out << "| Zone | ms | Share |\n|---|---|---|\n";
				for (const Zone& zone : frame.zones)
				{
					const float share = frame.totalMilliseconds > 0.0001F
						? (zone.milliseconds / frame.totalMilliseconds) * 100.0F
						: 0.0F;
					out << "| " << zone.name << " | " << zone.milliseconds << " | " << share << "% |\n";
				}
				out << "\n";
			}
		}

		out << "## Raw frame times\n\n";
		out << "Milliseconds per frame, oldest first - paste into a plot if needed.\n\n";
		out << "```\n";
		for (std::size_t index = 0; index < history_.size(); ++index)
		{
			out << history_[index].totalMilliseconds;
			out << ((index + 1) % 20 == 0 ? "\n" : " ");
		}
		out << "\n```\n";

		return out.str();
	}

}
