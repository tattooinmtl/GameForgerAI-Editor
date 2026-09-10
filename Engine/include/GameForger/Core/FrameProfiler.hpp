#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <deque>
#include <string>
#include <vector>

namespace gameforger::core
{
	// Per-frame CPU timing, split into named zones, with the worst frames kept
	// so a dip can be inspected after it has happened.
	//
	// A bare FPS number tells you a dip occurred but not what caused it; by the
	// time you look, the frame is gone. This records the breakdown of every
	// frame in a rolling window and permanently keeps the slowest ones, so the
	// question "what made that stutter" is answerable from the panel.
	//
	// Zones are FLAT and must not overlap - a zone's time is accumulated
	// against its name for the current frame, so nesting two open zones would
	// count the inner one twice. Instrument sibling phases, not a hierarchy.
	//
	// Cost is one steady_clock read per zone boundary. That is a few tens of
	// nanoseconds; it does not meaningfully perturb what it measures at the
	// handful of zones this project uses.
	class FrameProfiler
	{
	public:
		struct Zone
		{
			std::string name;
			float milliseconds = 0.0F;
		};

		struct Frame
		{
			float totalMilliseconds = 0.0F;
			// Seconds since the profiler started, for labelling a dip.
			double atSeconds = 0.0;
			std::vector<Zone> zones; // descending by time
		};

		// Frames kept for the rolling graph. 600 is ~10s at 60fps - long
		// enough to still contain a dip you only noticed after it happened.
		static constexpr std::size_t kHistoryCapacity = 600;
		// Worst frames retained. Small on purpose: a long list of near
		// identical stutters is noise, the outliers are the signal.
		static constexpr std::size_t kWorstCapacity = 12;

		void beginFrame();
		void endFrame();

		void beginZone(const std::string& name);
		void endZone();

		[[nodiscard]] const std::deque<Frame>& history() const noexcept { return history_; }
		[[nodiscard]] const std::vector<Frame>& worstFrames() const noexcept { return worst_; }
		[[nodiscard]] const Frame& lastFrame() const noexcept { return lastFrame_; }

		// Mean and worst over the rolling window.
		[[nodiscard]] float averageMilliseconds() const;
		[[nodiscard]] float worstMillisecondsInWindow() const;
		[[nodiscard]] float averageFps() const;

		// A frame slower than this is recorded as a dip. Default 20ms, i.e.
		// anything that misses 50fps.
		void setDipThresholdMs(const float ms) noexcept { dipThresholdMs_ = ms; }
		[[nodiscard]] float dipThresholdMs() const noexcept { return dipThresholdMs_; }

		[[nodiscard]] std::size_t dipCount() const noexcept { return dipCount_; }

		void clearWorst();
		void reset();

		// Mean cost of each zone across the whole rolling window, descending.
		// The per-frame view answers "what made THAT frame slow"; this answers
		// "what costs the most all the time", which is a different question and
		// usually the one worth acting on.
		[[nodiscard]] std::vector<Zone> averageZoneCosts() const;

		// A self-contained Markdown report: summary, average zone costs, every
		// recorded dip with its full breakdown, and the raw frame series.
		//
		// Returns a string rather than writing a file so it can be unit tested
		// and so the caller decides where it lands.
		[[nodiscard]] std::string buildReport(const std::string& title = "GameForgerAI frame report") const;

	private:
		using Clock = std::chrono::steady_clock;

		std::deque<Frame> history_;
		std::vector<Frame> worst_;
		Frame current_;
		Frame lastFrame_;

		Clock::time_point frameStart_{};
		Clock::time_point zoneStart_{};
		std::string openZone_;
		bool inFrame_ = false;

		Clock::time_point epoch_{};
		bool epochSet_ = false;

		float dipThresholdMs_ = 20.0F;
		std::size_t dipCount_ = 0;
	};

	// RAII wrapper. Prefer this over manual begin/endZone so an early return
	// cannot leave a zone open and silently attribute the rest of the frame
	// to it.
	class ProfileZone
	{
	public:
		ProfileZone(FrameProfiler& profiler, const std::string& name)
			: profiler_(&profiler)
		{
			profiler_->beginZone(name);
		}
		~ProfileZone() { profiler_->endZone(); }

		ProfileZone(const ProfileZone&) = delete;
		ProfileZone& operator=(const ProfileZone&) = delete;

	private:
		FrameProfiler* profiler_;
	};
}
