#pragma once

#include <vector>

#include <glm/vec3.hpp>

// Animation data shared between the editor scene model (SceneEntity::animation)
// and the AI command surface (SetAnimationCommand). Kept in its own header to
// break the AICommand.hpp <-> EditorScene.hpp circular include: the command
// surface needs to carry std::vector<TransformKeyframe>, which requires the
// complete type, but cannot include EditorScene.hpp (which itself includes
// AICommandBus.hpp -> AICommand.hpp to get the command variant).
namespace gameforger::editor
{
	struct TransformKeyframe
	{
		float time = 0.0F;
		glm::vec3 position{0.0F};
		glm::vec3 rotationEuler{0.0F};
		glm::vec3 scale{1.0F};
	};

	struct EntityAnimation
	{
		bool enabled = false;
		bool looping = false;
		std::vector<TransformKeyframe> keyframes; // kept sorted by time
	};
}
