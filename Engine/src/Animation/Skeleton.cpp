#include "GameForger/Animation/Skeleton.hpp"

#include <utility>

namespace gameforger::animation
{
    BoneIndex Skeleton::addBone(Bone bone)
    {
        if (indicesByName_.contains(bone.name))
        {
            return indicesByName_.at(bone.name);
        }

        const auto index = static_cast<BoneIndex>(bones_.size());
        indicesByName_.emplace(bone.name, index);
        bones_.push_back(std::move(bone));
        return index;
    }

    bool Skeleton::mapHumanoidBone(const HumanoidBone semantic, const BoneIndex bone)
    {
        if (bone < 0 || bone >= static_cast<BoneIndex>(bones_.size()))
        {
            return false;
        }

        humanoidMap_[semantic] = bone;
        return true;
    }

    std::span<const Bone> Skeleton::bones() const noexcept
    {
        return bones_;
    }

    const Bone* Skeleton::findBone(const std::string_view name) const noexcept
    {
        const auto found = indicesByName_.find(std::string{name});
        if (found == indicesByName_.end())
        {
            return nullptr;
        }

        return &bones_[static_cast<std::size_t>(found->second)];
    }

    std::optional<BoneIndex> Skeleton::humanoidBone(const HumanoidBone semantic) const noexcept
    {
        const auto found = humanoidMap_.find(semantic);
        if (found == humanoidMap_.end())
        {
            return std::nullopt;
        }

        return found->second;
    }

    std::vector<std::string> Skeleton::validate() const
    {
        std::vector<std::string> errors;

        for (std::size_t index = 0; index < bones_.size(); ++index)
        {
            const Bone& bone = bones_[index];

            if (bone.name.empty())
            {
                errors.emplace_back("Bone " + std::to_string(index) + " has no name.");
            }

            // parent must be either NoParent (-1) or a strictly earlier
            // bone index. Any value < -1 is treated as NoParent by
            // computeSkinningMatrices (which checks `bone.parent >= 0`),
            // so accepting -2, -3, ... silently here would mask a
            // malformed-import bug rather than surface it.
            if (bone.parent != NoParent && bone.parent >= static_cast<BoneIndex>(index))
            {
                errors.emplace_back(
                    "Bone '" + bone.name + "' must reference NoParent (-1) or an earlier parent bone.");
            }
        }

        if (!humanoidMap_.contains(HumanoidBone::Hips))
        {
            errors.emplace_back("A humanoid skeleton must map the hips bone.");
        }

        if (!humanoidMap_.contains(HumanoidBone::Head))
        {
            errors.emplace_back("A humanoid skeleton must map the head bone.");
        }

        return errors;
    }
}
