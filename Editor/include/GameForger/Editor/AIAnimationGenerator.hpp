#pragma once

#include <string>
#include <vector>

#include "GameForger/Editor/AIProviderClient.hpp"
#include "GameForger/Editor/EditorScene.hpp"

namespace gameforger::editor
{
	struct GeneratedEntityAnimation
	{
		std::string entityName;
		bool looping = false;
		std::vector<TransformKeyframe> keyframes;
	};

	struct AIAnimationResult
	{
		bool success = false;
		std::string message;
		std::vector<GeneratedEntityAnimation> animations;
	};

	// Blocking call — the caller is responsible for running this off the UI
	// thread. `targetEntities` should be the current live state (name, shape,
	// transform, pivot) of every entity the animation may target; it's both the
	// AI's context and the fallback source for any keyframe fields it omits.
	[[nodiscard]] AIAnimationResult generateAnimation(
		const AIProviderClient& client,
		const std::string& providerId,
		const std::vector<SceneEntity>& targetEntities,
		const std::string& userPrompt);
}
