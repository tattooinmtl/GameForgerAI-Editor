#pragma once

#include <string>
#include <variant>
#include <vector>

#include <glm/vec3.hpp>

#include "GameForger/Editor/AnimationData.hpp"

namespace gameforger::editor
{
    enum class PrimitiveType
    {
        Cube,
        Sphere,
        Cylinder,
        Cone,
        Plane,
        Capsule
    };

    struct CreateEntityCommand
    {
        std::string name;
        PrimitiveType primitive = PrimitiveType::Cube;
        glm::vec3 position{0.0F};
    };

    struct DeleteEntityCommand
    {
        std::string entityName;
    };

    struct RenameEntityCommand
    {
        std::string entityName;
        std::string newName;
    };

    struct DuplicateEntityCommand
    {
        std::string entityName;
    };

    struct SetPositionCommand
    {
        std::string entityName;
        glm::vec3 position{0.0F};
    };

    struct RequestAnimationCommand
    {
        std::string characterName;
        std::string action;
        bool looping = false;
        bool rootMotion = false;
    };

    // Replaces (or clears, when enabled is false) the EntityAnimation block on
    // an entity. Routed through AICommandBus so the undo system sees it the
    // same as every other editor mutation - the AI Animation "Apply" path used
    // to write the entity in place, leaving the undo stack silently out of
    // sync with the visible state.
    struct SetAnimationCommand
    {
        std::string entityName;
        bool enabled = false;
        bool looping = false;
        std::vector<TransformKeyframe> keyframes;
    };

    struct CreateScriptCommand
    {
        std::string path;
        std::string language = "lua";
        std::string content;
        // Explicit overwrite gate. The executor refuses to replace an
        // existing file unless this is set, so a silent overwrite can't
        // hide Lua-chunk-vs-registry drift while Play is running.
        bool overwrite = false;
    };

    struct AttachScriptCommand
    {
        std::string entityName;
        std::string scriptPath;
    };

    struct DetachScriptCommand
    {
        std::string entityName;
        std::string scriptPath;
    };

    // An entity can hold any number of tags, in priority order - the first
    // (lowest index) tag is the "primary" one for anything that needs a
    // single tag (e.g. the Hierarchy label, the AI Forge scene summary).
    struct AddTagCommand
    {
        std::string entityName;
        std::string tag;
    };

    struct RemoveTagCommand
    {
        std::string entityName;
        std::string tag;
    };

    struct CreateTerrainCommand
    {
        std::string name;
        int resolution = 65;
        float worldSize = 50.0F;
        float heightScale = 12.0F;
        glm::vec3 position{0.0F};
        // "flat" (default) or "mountain" - a radial peak centered in the
        // worldSize x worldSize footprint, cresting at heightScale/2 above
        // the baseline. See EditorScene.cpp's CreateTerrainCommand handler.
        std::string shape = "flat";
    };

    struct CreateImportedMeshCommand
    {
        std::string name;
        std::string sourcePath; // must already exist under Game/Models/, relative to projectRoot
        glm::vec3 position{0.0F};
    };

    struct CreateTextMeshCommand
    {
        std::string name;
        std::string content;
        std::string fontPath; // must already exist under Game/Fonts/, relative to projectRoot
        float fontSize = 1.0F;
        float depth = 0.2F;
        glm::vec3 position{0.0F};
    };

    using EditableValue = std::variant<bool, float, std::string, glm::vec3>;

    struct SetPropertyCommand
    {
        std::string entityName;
        std::string component;
        std::string property;
        EditableValue value = 0.0F;
    };

    using AIEditorCommand = std::variant<
        CreateEntityCommand,
        DeleteEntityCommand,
        RenameEntityCommand,
        DuplicateEntityCommand,
        SetPositionCommand,
        RequestAnimationCommand,
        SetAnimationCommand,
        CreateScriptCommand,
        AttachScriptCommand,
        DetachScriptCommand,
        AddTagCommand,
        RemoveTagCommand,
        CreateTerrainCommand,
        CreateTextMeshCommand,
        CreateImportedMeshCommand,
        SetPropertyCommand>;
}
