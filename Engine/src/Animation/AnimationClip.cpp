#include "GameForger/Animation/AnimationClip.hpp"

namespace gameforger::animation
{
    std::vector<std::string> AnimationClip::validate(const Skeleton& skeleton) const
    {
        std::vector<std::string> errors;

        if (name.empty())
        {
            errors.emplace_back("Animation clip has no name.");
        }

        if (durationSeconds <= 0.0F)
        {
            errors.emplace_back("Animation clip duration must be greater than zero.");
        }

        for (const BoneTrack& track : tracks)
        {
            if (track.bone < 0 ||
                track.bone >= static_cast<BoneIndex>(skeleton.bones().size()))
            {
                errors.emplace_back("Animation contains a track with an invalid bone index.");
            }

            float previousTime = -1.0F;
            for (const TransformKey& key : track.keys)
            {
                if (key.timeSeconds < previousTime)
                {
                    errors.emplace_back("Animation key times must be sorted.");
                    break;
                }

                if (key.timeSeconds < 0.0F || key.timeSeconds > durationSeconds)
                {
                    errors.emplace_back("Animation key time is outside the clip duration.");
                    break;
                }

                previousTime = key.timeSeconds;
            }
        }

        return errors;
    }
}
