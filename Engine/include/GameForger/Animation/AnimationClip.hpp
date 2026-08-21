#pragma once

#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include "GameForger/Animation/Skeleton.hpp"

namespace gameforger::animation
{
    struct TransformKey
    {
        float timeSeconds = 0.0F;
        glm::vec3 translation{0.0F};
        glm::quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
        glm::vec3 scale{1.0F};
    };

    struct BoneTrack
    {
        BoneIndex bone = NoParent;
        std::vector<TransformKey> keys;
    };

    struct AnimationEvent
    {
        float timeSeconds = 0.0F;
        std::string name;
        std::string payload;
    };

    class AnimationClip final
    {
    public:
        std::string name;
        float durationSeconds = 0.0F;
        bool looping = false;
        bool usesRootMotion = false;
        std::vector<BoneTrack> tracks;
        std::vector<AnimationEvent> events;

        [[nodiscard]] std::vector<std::string> validate(const Skeleton& skeleton) const;
    };
}
