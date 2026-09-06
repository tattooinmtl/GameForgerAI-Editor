#include "GameForger/Editor/Storyboard.hpp"

#include <algorithm>
#include <utility>

namespace gameforger::editor
{
	namespace
	{
		bool earlierThan(const AudioCue& a, const AudioCue& b)
		{
			return a.time < b.time;
		}
	}

	std::size_t insertAudioCueSorted(std::vector<AudioCue>& cues, AudioCue cue)
	{
		// upper_bound, not lower_bound: two cues at the same time keep their
		// insertion order, so adding a second sound at the same instant does
		// not jump ahead of the one already there.
		const auto at = std::upper_bound(cues.begin(), cues.end(), cue, earlierThan);
		const auto index = static_cast<std::size_t>(std::distance(cues.begin(), at));
		cues.insert(at, std::move(cue));
		return index;
	}

	std::size_t resortAudioCues(std::vector<AudioCue>& cues, const std::size_t movedIndex)
	{
		if (movedIndex >= cues.size())
		{
			return movedIndex;
		}
		// Tag the moved cue by address before sorting so it can be found
		// again afterwards - comparing by time would pick the wrong one when
		// several cues share a timestamp.
		const AudioCue* target = &cues[movedIndex];
		std::vector<const AudioCue*> order;
		order.reserve(cues.size());
		for (const AudioCue& cue : cues)
		{
			order.push_back(&cue);
		}
		std::stable_sort(order.begin(), order.end(),
			[](const AudioCue* a, const AudioCue* b) { return a->time < b->time; });

		std::size_t newIndex = movedIndex;
		std::vector<AudioCue> sorted;
		sorted.reserve(cues.size());
		for (std::size_t i = 0; i < order.size(); ++i)
		{
			if (order[i] == target)
			{
				newIndex = i;
			}
			sorted.push_back(*order[i]);
		}
		cues = std::move(sorted);
		return newIndex;
	}
}
