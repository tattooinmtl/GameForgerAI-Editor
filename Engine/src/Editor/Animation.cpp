#include "GameForger/Editor/Animation.hpp"

#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

namespace gameforger::editor
{
	AnimatedPose sampleAnimation(const EntityAnimation& animation, const float time)
	{
		const std::vector<TransformKeyframe>& keyframes = animation.keyframes;
		if (keyframes.empty())
		{
			return {};
		}
		if (keyframes.size() == 1)
		{
			return {keyframes.front().position, keyframes.front().rotationEuler, keyframes.front().scale};
		}

		const float firstTime = keyframes.front().time;
		const float lastTime = keyframes.back().time;
		const float duration = lastTime - firstTime;

		float sampleTime = time;
		if (animation.looping && duration > 0.0F)
		{
			sampleTime = std::fmod(time - firstTime, duration);
			if (sampleTime < 0.0F)
			{
				sampleTime += duration;
			}
			sampleTime += firstTime;
		}
		else
		{
			sampleTime = glm::clamp(time, firstTime, lastTime);
		}

		for (std::size_t index = 0; index + 1 < keyframes.size(); ++index)
		{
			const TransformKeyframe& start = keyframes[index];
			const TransformKeyframe& end = keyframes[index + 1];
			// Half-open interval [start.time, end.time) rather than
			// closed. The previous `<=` made both segment i-1 (where
			// end.time == sampleTime) and segment i (where
			// start.time == sampleTime) match when sampleTime landed on
			// a keyframe time; the loop picked the earlier one, which
			// disagrees with how timeline-scrub "land on keyframe T"
			// feels to the user (alpha should be 0.0 at T, the start of
			// the next segment).
			if (sampleTime >= start.time && sampleTime < end.time)
			{
				const float span = end.time - start.time;
				const float alpha = span > 0.0F ? (sampleTime - start.time) / span : 0.0F;
				AnimatedPose pose;
				pose.position = glm::mix(start.position, end.position, alpha);
				// Slerp rotation through quaternion space, then convert
				// back to the same XYZ Euler convention the rest of the
				// engine consumes. Linear (glm::mix) interpolation on
				// Euler angles passes through invalid intermediate
				// orientations and ignores the 360-degree wrap - a key
				// rotation from 0 to 350 degrees would spin the entity
				// the long way around instead of taking the 10-degree
				// shortcut.
				const glm::quat startRotation = glm::quat(glm::radians(start.rotationEuler));
				const glm::quat endRotation = glm::quat(glm::radians(end.rotationEuler));
				const glm::quat interpolated = glm::slerp(startRotation, endRotation, alpha);
				pose.rotationEuler = glm::degrees(glm::eulerAngles(interpolated));
				pose.scale = glm::mix(start.scale, end.scale, alpha);
				return pose;
			}
		}

		const TransformKeyframe& edge = sampleTime <= firstTime ? keyframes.front() : keyframes.back();
		return {edge.position, edge.rotationEuler, edge.scale};
	}
}
