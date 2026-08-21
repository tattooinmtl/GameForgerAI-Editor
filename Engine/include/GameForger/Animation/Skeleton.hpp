#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>

namespace gameforger::animation
{
    using BoneIndex = std::int32_t;
    inline constexpr BoneIndex NoParent = -1;

    enum class HumanoidBone
    {
        RootMotion,
        Hips,
        Spine,
        Chest,
        Neck,
        Head,
        ShoulderLeft,
        UpperArmLeft,
        LowerArmLeft,
        HandLeft,
        ShoulderRight,
        UpperArmRight,
        LowerArmRight,
        HandRight,
        UpperLegLeft,
        LowerLegLeft,
        FootLeft,
        ToeLeft,
        UpperLegRight,
        LowerLegRight,
        FootRight,
        ToeRight
    };

    struct Bone
    {
        std::string name;
        BoneIndex parent = NoParent;
        glm::mat4 inverseBindMatrix{1.0F};
        glm::mat4 localBindTransform{1.0F};
    };

    class Skeleton final
    {
    public:
        BoneIndex addBone(Bone bone);
        bool mapHumanoidBone(HumanoidBone semantic, BoneIndex bone);

        [[nodiscard]] std::span<const Bone> bones() const noexcept;
        [[nodiscard]] const Bone* findBone(std::string_view name) const noexcept;
        [[nodiscard]] std::optional<BoneIndex> humanoidBone(HumanoidBone semantic) const noexcept;
        [[nodiscard]] std::vector<std::string> validate() const;

    private:
        std::vector<Bone> bones_;
        std::unordered_map<std::string, BoneIndex> indicesByName_;
        std::unordered_map<HumanoidBone, BoneIndex> humanoidMap_;
    };
}
