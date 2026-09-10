#pragma once

#include <string>
#include <variant>
#include <vector>

#include <glm/vec2.hpp>
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
        Capsule,
        // Transform-only grouping node - no mesh, draws as a small axis
        // gizmo. Everything else (parenting, tags, scripts, colliders,
        // animation) works on it unchanged, because none of those systems
        // ever cared what shape an entity was. Added LAST on purpose: the
        // save format writes primitives by name ("cube"), not by index, so
        // position in this enum is not a compatibility concern - but every
        // switch over PrimitiveType is, and the compiler finds those.
        Empty
    };

    // Unity's three light types. A light's DIRECTION is its entity's own
    // forward vector (entityForward(), Transform.hpp) - there is no separate
    // direction field, so the normal Move/Rotate gizmo aims a light exactly
    // like it aims anything else. Position is ignored for Directional (a sun
    // has no location, only a direction), which is why the Inspector hides
    // range/cone for it.
    enum class LightType
    {
        Directional,
        Point,
        Spot
    };

    [[nodiscard]] const char* lightTypeName(LightType type) noexcept;
    [[nodiscard]] bool lightTypeFromName(const std::string& text, LightType& outType) noexcept;

    // Data for an entity with isLight=true (see below) - additive, like
    // hasCollider, so a light can also carry scripts, tags, an animation, or
    // children. Before this existed every shader inlined one hardcoded
    // direction; see ViewportRenderer's light uniform block.
    struct LightData
    {
        LightType type = LightType::Directional;
        // Linear RGB, multiplied by intensity. Default is a slightly warm
        // white so a fresh sun does not look like a fluorescent tube.
        glm::vec3 color{1.0F, 0.96F, 0.90F};
        float intensity = 1.0F;
        // Point/Spot only: world units at which the light reaches zero.
        // Attenuation is a smooth inverse-square falloff windowed to this
        // radius, so a light never contributes past its own range - that
        // bound is what lets the renderer cull lights per frame.
        float range = 25.0F;
        // Spot only, degrees from the cone axis. Full brightness inside
        // inner, smoothly falling to zero at outer. Kept as half-angles
        // (Unity's "spot angle" is the full cone, i.e. 2x outer).
        float innerConeDegrees = 20.0F;
        float outerConeDegrees = 30.0F;
        // Whether this light renders a shadow map. Directional lights use
        // cascades; spots use a single perspective map. Point lights need a
        // cube map (6 faces), so this stays opt-in for them - it is the one
        // setting here that can meaningfully cost frame time.
        bool castShadows = true;
        // Depth offset applied when sampling the shadow map, in shadow-map
        // depth units. Too low: acne (surfaces self-shadow in stripes). Too
        // high: peter-panning (contact shadows detach from the caster).
        float shadowBias = 0.0015F;
    };

    // Data for an entity with isCamera=true (see below). Distinct from
    // isCineCamera: that one is a cutscene path-follower whose "path" is its
    // own animation keyframes, with no lens settings at all. This is a real
    // game camera - the thing a crosshair or HUD parents to, and the thing the
    // Game view falls back to when no script has claimed the camera, so a
    // scene is viewable without attaching a controller script first.
    //
    // Direction is the entity's own forward vector (entityForward()), same as
    // LightData - aim it with the ordinary Rotate gizmo.
    struct CameraData
    {
        // Vertical FOV, degrees. Unity's default is 60.
        float fieldOfViewDegrees = 60.0F;
        float nearClip = 0.1F;
        float farClip = 500.0F;
        // Background drawn where nothing is rendered.
        glm::vec3 clearColor{0.09F, 0.11F, 0.15F};
        // Exactly one camera in a scene should have this set; the Game view
        // picks the first it finds. Setting it through SetPropertyCommand
        // clears the flag on every other camera (see EditorScene.cpp) so the
        // "exactly one" invariant cannot be broken from the UI or the AI.
        bool isMainCamera = false;
    };

    // Screen-space UI kinds. Deliberately a small fixed set rather than a
    // general canvas/layout system: these are drawn as ImGui overlays
    // projected into the Game view's own rect (the technique the crosshair,
    // pickup hint and detection icon already use), not as world geometry.
    enum class UIElementKind
    {
        Crosshair,
        Image,
        Text,
        Panel
    };

    [[nodiscard]] const char* uiElementKindName(UIElementKind kind) noexcept;
    [[nodiscard]] bool uiElementKindFromName(const std::string& text, UIElementKind& outKind) noexcept;

    // Where on the screen offsetPixels is measured from. Center is the
    // default because the most common UI child is a crosshair.
    enum class UIAnchor
    {
        Center,
        TopLeft,
        TopCenter,
        TopRight,
        MiddleLeft,
        MiddleRight,
        BottomLeft,
        BottomCenter,
        BottomRight
    };

    [[nodiscard]] const char* uiAnchorName(UIAnchor anchor) noexcept;
    [[nodiscard]] bool uiAnchorFromName(const std::string& text, UIAnchor& outAnchor) noexcept;

    // Data for an entity with isUIElement=true (see below) - additive, like
    // hasCollider. A UI element draws only while its parentName chain reaches
    // the active camera (see drawGameViewPanel), which is what makes
    // "parent a crosshair to the camera" work the way it does in Unity. Its
    // 3D transform is ignored for drawing; anchor + offsetPixels position it.
    struct UIElementData
    {
        UIElementKind kind = UIElementKind::Crosshair;
        UIAnchor anchor = UIAnchor::Center;
        // Pixels from the anchor point. +X right, +Y down (screen convention).
        glm::vec2 offsetPixels{0.0F, 0.0F};
        // Image/Panel: draw size. Crosshair: arm length. Text ignores it.
        glm::vec2 sizePixels{24.0F, 24.0F};
        glm::vec3 color{1.0F, 1.0F, 1.0F};
        float opacity = 1.0F;
        // Text only.
        std::string text = "Text";
        std::string fontPath; // relative to projectRoot; empty = the editor's own UI font
        float fontSizePixels = 18.0F;
        // Image only - relative to projectRoot, e.g. "Game/Icons/heart.png".
        std::string imagePath;
        // Crosshair only.
        float thicknessPixels = 2.0F;
        float gapPixels = 4.0F;
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

    // Creates a Light entity. `type` is "directional" | "point" | "spot"
    // (see lightTypeFromName, EditorScene.hpp); an unrecognised value is
    // rejected by the validator rather than silently defaulted, so a model
    // that invents a type gets a message it can act on.
    //
    // A light's DIRECTION is its entity rotation, not a field here - aim it
    // with the ordinary Rotate gizmo, or set Transform/rotation.
    struct CreateLightCommand
    {
        std::string name;
        std::string type = "directional";
        glm::vec3 position{0.0F, 5.0F, 0.0F};
        glm::vec3 rotationEuler{50.0F, -30.0F, 0.0F}; // a sensible sun angle
        glm::vec3 color{1.0F, 0.96F, 0.90F};
        float intensity = 1.0F;
    };

    // Creates a real game Camera entity (not a cine camera - see
    // CreateEntityCommand + SetPropertyCommand{"CineCamera"} for those).
    struct CreateCameraCommand
    {
        std::string name;
        glm::vec3 position{0.0F, 2.0F, -6.0F};
        glm::vec3 rotationEuler{0.0F};
        float fieldOfViewDegrees = 60.0F;
        // Whether this becomes the scene's main camera. The executor clears
        // the flag on every other camera when this is true.
        bool makeMain = true;
    };

    // Creates a screen-space UI element. `kind` is
    // "crosshair" | "image" | "text" | "panel". `parentName` is optional but
    // is the whole point of the feature - a UI element only draws while it is
    // parented under the active camera.
    struct CreateUIElementCommand
    {
        std::string name;
        std::string kind = "crosshair";
        std::string parentName;
        std::string text;      // Text kind only
        std::string imagePath; // Image kind only, relative to projectRoot
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
        CreateLightCommand,
        CreateCameraCommand,
        CreateUIElementCommand,
        SetPropertyCommand>;
}
