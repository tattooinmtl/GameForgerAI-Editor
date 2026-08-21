#pragma once

#include <glm/vec3.hpp>

#include "GameForger/Editor/EditorScene.hpp"

namespace gameforger::editor
{
	struct AnimatedPose
	{
		glm::vec3 position{0.0F};
		glm::vec3 rotationEuler{0.0F};
		glm::vec3 scale{1.0F};
	};

	// Interpolates `animation`'s keyframes (assumed sorted by time) at `time`.
	// Holds at the first/last keyframe outside the recorded range, or loops if
	// animation.looping is set. Returns a zero-ish pose if there are no
	// keyframes — callers should check EntityAnimation::keyframes.empty() first.
	[[nodiscard]] AnimatedPose sampleAnimation(const EntityAnimation& animation, float time);
}
