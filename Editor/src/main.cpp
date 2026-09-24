#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <commdlg.h>
#include <shellapi.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <ImGuizmo.h>
#include <stb_image.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec3.hpp>

#include "GameForger/Core/Engine.hpp"
#include "GameForger/Editor/AIAnimationGenerator.hpp"
#include "GameForger/Editor/AICommandBus.hpp"
#include "GameForger/Editor/AICommandPlanner.hpp"
#include "GameForger/Editor/AIProviderClient.hpp"
#include "GameForger/Editor/Animation.hpp"
#include "GameForger/Editor/EditorScene.hpp"
#include "GameForger/Editor/FpsRigBuilder.hpp"
#include "GameForger/Editor/ImGuiInputSource.hpp"
#include "GameForger/Editor/ModelImport.hpp"
#include "GameForger/Editor/SceneSerializer.hpp"
#include "GameForger/Editor/ScriptGenerator.hpp"
#include "GameForger/Editor/ScriptRuntime.hpp"
#include "GameForger/Editor/SplashScreen.hpp"
#include "GameForger/Editor/Terrain.hpp"
#include "GameForger/Editor/TerrainTexture.hpp"
#include "GameForger/Editor/TextMesh.hpp"
#include "GameForger/Editor/Version.hpp"
#include "GameForger/Editor/Transform.hpp"
#include "GameForger/Editor/ViewportRenderer.hpp"
#include "GameForger/Runtime/GameCamera.hpp"
#include "GameForger/Runtime/FpsDemoKit.hpp"
#include "GameForger/Runtime/GameplayHud.hpp"
#include "GameForger/Runtime/GameplayLoop.hpp"

namespace
{
    using gameforger::editor::AICommandBus;
    using gameforger::editor::AICommandResult;
    using gameforger::editor::AIAnimationResult;
    using gameforger::editor::AIEditorCommand;
    using gameforger::editor::AIPlanResult;
    using gameforger::editor::AIProviderClient;
    using gameforger::editor::AIProviderRequest;
    using gameforger::editor::AIProviderResponse;
    using gameforger::editor::AnimatedPose;
    using gameforger::editor::AddTagCommand;
    using gameforger::editor::AttachScriptCommand;
    using gameforger::editor::CreateEntityCommand;
    using gameforger::editor::CreateScriptCommand;
    using gameforger::editor::DeleteEntityCommand;
    using gameforger::editor::DetachScriptCommand;
    using gameforger::editor::DuplicateEntityCommand;
    using gameforger::editor::applyParentConstraints;
    using gameforger::editor::cameraLookingAt;
    using gameforger::editor::describeCommand;
    using gameforger::editor::EditorScene;
    using gameforger::editor::EntityAnimation;
    using gameforger::editor::GameCameraState;
    using gameforger::editor::GameplayState;
    using gameforger::editor::generateAnimation;
    using gameforger::editor::generateEntityScript;
    using gameforger::editor::GeneratedEntityAnimation;
    using gameforger::editor::ImGuiInputSource;
    using gameforger::editor::planEditorCommands;
    using gameforger::editor::PrimitiveType;
    using gameforger::editor::RemoveTagCommand;
    using gameforger::editor::RenameEntityCommand;
    using gameforger::editor::sampleAnimation;
    using gameforger::editor::ScriptRuntime;
    using gameforger::editor::loadScene;
    using gameforger::editor::saveScene;
    using gameforger::editor::buildTextMesh;
    using gameforger::editor::loadModelMesh;
    using gameforger::editor::ModelImportResult;
    using gameforger::editor::CreateImportedMeshCommand;
    using gameforger::editor::CreateTerrainCommand;
    using gameforger::editor::CreateTextMeshCommand;
    using gameforger::editor::EntityCameraRig;
    using gameforger::editor::SceneEntity;
    using gameforger::editor::SceneLoadResult;
    using gameforger::editor::SceneSaveResult;
    using gameforger::editor::ScriptGenerationResult;
    using gameforger::editor::SetPositionCommand;
    using gameforger::editor::SetAnimationCommand;
    using gameforger::editor::SetPropertyCommand;
    using gameforger::editor::showSplashScreen;
    using gameforger::editor::TextMeshBuildResult;
    using gameforger::editor::TerrainData;
    using gameforger::editor::TerrainLayerData;
    using gameforger::editor::loadTextureImage;
    using gameforger::editor::LoadedTexture;
    using gameforger::editor::TextMeshData;
    using gameforger::editor::tickPlayModeAnimations;
    using gameforger::editor::tickProjectiles;
    using gameforger::editor::tickScripts;
    using gameforger::editor::TransformKeyframe;
    using gameforger::editor::scriptedPlayCamera;
    using gameforger::editor::yawPitchForward;
    using gameforger::editor::composeEntityPivotFrame;
    using gameforger::editor::buildFpsPlayerRig;
    using gameforger::editor::createGameManager;
    using gameforger::editor::fpsDemoPlayerScripts;
    using gameforger::editor::dropInventoryItem;
    using gameforger::editor::ensureInventorySlots;
    using gameforger::editor::entityProvidesInventory;
    using gameforger::editor::findPickupItemCandidate;
    using gameforger::editor::FpsRigOptions;
    using gameforger::editor::FpsRigBuildResult;
    using gameforger::editor::hasScript;
    using gameforger::editor::hasTag;
    using gameforger::editor::kHotbarSlotCount;
    using gameforger::editor::kInventorySlotCount;
    using gameforger::editor::kItemScriptPath;
    using gameforger::editor::pickUpItem;
    using gameforger::editor::pickupDisplayName;
    using gameforger::editor::tickEffects;
    using gameforger::editor::drawGameplayHud;
    using gameforger::editor::HudCanvas;
    using gameforger::editor::HudColor;
    using gameforger::editor::HudFrame;
    using gameforger::editor::composeEntityTransform;

    enum class CameraDragMode
    {
        None,
        Orbit,
        Pan,
        // First-person look: rotating the mouse turns the camera in place
        // (the eye stays put) instead of swinging around a pivot point.
        Fly
    };

    enum class LogLevel
    {
        Info,
        Warning,
        Error
    };

    struct LogEntry
    {
        LogLevel level = LogLevel::Info;
        std::string timestamp;
        std::string message;
    };

    struct ConsoleState
    {
        std::vector<LogEntry> entries;
        bool showInfo = true;
        bool showWarnings = true;
        bool showErrors = true;
        bool autoScroll = true;
    };

    struct ProjectBrowserState
    {
        // Empty until first drawn, then set to <projectRoot>/Game.
        std::filesystem::path currentDirectory;
        // For highlighting the last-clicked file; not tied to entity selection.
        std::filesystem::path selectedFile;
    };

    // Toolbox > Text Mesh: state for the creation popup (Add Script's font
    // Browse.../content/size/depth fields are separate instances of the same
    // idea, reused for editing an existing entity in the Inspector).
    struct TextMeshToolState
    {
        bool requestOpen = false;
        std::string fontRelativePath; // relative to projectRoot, e.g. "Game/Fonts/Roboto-Regular.ttf"
        std::array<char, 512> content{};
        float fontSize = 1.0F;
        float depth = 0.2F;
        std::string status;
        bool statusSuccess = false;
    };

    std::string currentTimestamp()
    {
        const std::time_t now = std::time(nullptr);
        std::tm localTime{};
        localtime_s(&localTime, &now);
        std::array<char, 16> buffer{};
        std::strftime(buffer.data(), buffer.size(), "%H:%M:%S", &localTime);
        return buffer.data();
    }

    void logMessage(ConsoleState& console, const LogLevel level, std::string message)
    {
        console.entries.push_back(LogEntry{level, currentTimestamp(), std::move(message)});
        constexpr std::size_t maxEntries = 500;
        if (console.entries.size() > maxEntries)
        {
            console.entries.erase(console.entries.begin(), console.entries.begin() +
                static_cast<std::ptrdiff_t>(console.entries.size() - maxEntries));
        }
    }

    struct EditorCameraState
    {
        float yaw = 0.7F;
        float pitch = 0.35F;
        float distance = 4.0F;
        glm::vec3 target{0.0F};
        CameraDragMode dragMode = CameraDragMode::None;
    };

    struct SelectionState
    {
        // Click order; empty = nothing selected. This is the source of truth for
        // multi-select (Scene panel highlighting, Viewport outlines, AI animation
        // targets).
        std::vector<int> multiSelectedIds;
        // The "active"/primary entity - always multiSelectedIds.back() when non-
        // empty, kept in sync by applySelectionClick()/clearSelection() below.
        // Everything that only makes sense for a single object (Inspector, gizmo,
        // keyboard Delete/Duplicate) keeps reading this, unchanged.
        std::optional<int> selectedEntityId;
        // Reference point for Shift range-select.
        std::optional<int> shiftAnchorId;

        [[nodiscard]] bool contains(const int id) const noexcept
        {
            return std::find(multiSelectedIds.begin(), multiSelectedIds.end(), id) != multiSelectedIds.end();
        }
    };

    void syncPrimarySelection(SelectionState& selection)
    {
        selection.selectedEntityId = selection.multiSelectedIds.empty()
            ? std::nullopt
            : std::optional<int>(selection.multiSelectedIds.back());
    }

    // Selects exactly one entity, replacing any existing selection - equivalent
    // to an unmodified click. Used for programmatic selection (auto-select on
    // create/duplicate, Play-mode restore) as well as plain clicks.
    void selectOnly(SelectionState& selection, const int id)
    {
        selection.multiSelectedIds.assign(1, id);
        selection.shiftAnchorId = id;
        syncPrimarySelection(selection);
    }

    void clearSelection(SelectionState& selection)
    {
        selection.multiSelectedIds.clear();
        selection.shiftAnchorId.reset();
        syncPrimarySelection(selection);
    }

    // Explorer-style click semantics: plain click selects only this item; Ctrl
    // toggles it in/out of the selection; Shift range-selects from the last
    // click/ctrl-click anchor to this item, in `orderedIds` order.
    void applySelectionClick(
        SelectionState& selection,
        const std::vector<int>& orderedIds,
        const int clickedId,
        const bool ctrlHeld,
        const bool shiftHeld)
    {
        const auto clickIt = std::find(orderedIds.begin(), orderedIds.end(), clickedId);
        if (shiftHeld && selection.shiftAnchorId.has_value() && clickIt != orderedIds.end())
        {
            const auto anchorIt = std::find(orderedIds.begin(), orderedIds.end(), *selection.shiftAnchorId);
            if (anchorIt != orderedIds.end())
            {
                auto lo = anchorIt;
                auto hi = clickIt;
                if (lo > hi)
                {
                    std::swap(lo, hi);
                }
                selection.multiSelectedIds.assign(lo, hi + 1);
            }
            else
            {
                selectOnly(selection, clickedId);
                return;
            }
        }
        else if (ctrlHeld)
        {
            const auto existing = std::find(
                selection.multiSelectedIds.begin(), selection.multiSelectedIds.end(), clickedId);
            if (existing != selection.multiSelectedIds.end())
            {
                selection.multiSelectedIds.erase(existing);
            }
            else
            {
                selection.multiSelectedIds.push_back(clickedId);
            }
            selection.shiftAnchorId = clickedId;
        }
        else
        {
            selectOnly(selection, clickedId);
            return;
        }

        syncPrimarySelection(selection);
    }

    // Undo/redo (snapshot-based: each entry is a full copy of the entity list,
    // which EditorScene::replaceEntities already supports for Play mode) plus
    // the in-app clipboard for copy/paste, and which script row (if any) is
    // currently focused in the Inspector's Scripts list.
    struct EditHistoryState
    {
        static constexpr std::size_t maxDepth = 5;
        // A single mouse drag (gizmo, an Inspector DragFloat3, animation record
        // mode) fires many small commands; without coalescing, one drag would
        // eat the whole undo stack as dozens of near-identical steps.
        static constexpr double coalesceWindowSeconds = 0.6;

        std::vector<std::vector<SceneEntity>> undoStack;
        std::vector<std::vector<SceneEntity>> redoStack;
        double lastActionTime = -1000.0;

        std::vector<SceneEntity> clipboardEntities;
        std::string clipboardScriptPath;
        bool clipboardHoldsScript = false;

        std::optional<int> focusedScriptEntityId;
        std::string focusedScriptPath;
    };

    void pruneSelectionToExisting(const EditorScene& scene, SelectionState& selection)
    {
        std::vector<int> stillValid;
        for (const int id : selection.multiSelectedIds)
        {
            if (scene.findEntity(id) != nullptr)
            {
                stillValid.push_back(id);
            }
        }
        selection.multiSelectedIds = std::move(stillValid);
        selection.shiftAnchorId.reset();
        syncPrimarySelection(selection);
    }

    // Returns true if a new snapshot was actually pushed (false when this call
    // was coalesced into the previous one) - callers need this to know whether
    // it's safe to pop the stack back if the command turns out to fail.
    bool pushUndoSnapshot(EditHistoryState& history, const EditorScene& scene, const double now)
    {
        if (now - history.lastActionTime < EditHistoryState::coalesceWindowSeconds)
        {
            history.lastActionTime = now;
            return false;
        }
        history.undoStack.push_back(scene.entities());
        if (history.undoStack.size() > EditHistoryState::maxDepth)
        {
            history.undoStack.erase(history.undoStack.begin());
        }
        history.redoStack.clear();
        history.lastActionTime = now;
        return true;
    }

    void performUndo(EditHistoryState& history, EditorScene& scene, SelectionState& selection)
    {
        if (history.undoStack.empty())
        {
            return;
        }
        history.redoStack.push_back(scene.entities());
        if (history.redoStack.size() > EditHistoryState::maxDepth)
        {
            history.redoStack.erase(history.redoStack.begin());
        }
        scene.replaceEntities(std::move(history.undoStack.back()));
        history.undoStack.pop_back();
        pruneSelectionToExisting(scene, selection);
    }

    void performRedo(EditHistoryState& history, EditorScene& scene, SelectionState& selection)
    {
        if (history.redoStack.empty())
        {
            return;
        }
        history.undoStack.push_back(scene.entities());
        if (history.undoStack.size() > EditHistoryState::maxDepth)
        {
            history.undoStack.erase(history.undoStack.begin());
        }
        scene.replaceEntities(std::move(history.redoStack.back()));
        history.redoStack.pop_back();
        pruneSelectionToExisting(scene, selection);
    }

    // Play mode is a non-destructive sandbox: entering it snapshots the scene and
    // hands out a separate camera to look around with; leaving it restores the
    // snapshot verbatim, the same way Unity discards Play-mode changes on Stop.
    // It does not (yet) execute attached Lua scripts or simulate gameplay rules —
    // there is no script runtime wired into the engine yet.
    struct PlayModeState
    {
        bool isPlaying = false;
        bool isPaused = false;
        bool stepOneFrame = false;
        std::vector<SceneEntity> savedEntities;
        std::vector<int> savedMultiSelectedIds;
        EditorCameraState savedCamera;
        EditorCameraState playCamera;
        float playElapsedTime = 0.0F;

        // Mouse-look for the scripted Game view camera (self.camera:setMode):
        // hold right mouse over the Game view to look around, same gesture as
        // the editable Viewport's flythrough. In FPS mode the yaw is applied
        // directly to the entity (see drawGameViewPanel) since the character's
        // facing IS the view direction there, so only pitch is tracked here;
        // in third-person mode both are pure camera state that never touches
        // the entity, so the camera can orbit independently of it.
        bool gameCameraDragging = false;
        float gameCameraLookYawDegrees = 0.0F;
        float gameCameraLookPitchDegrees = 0.0F;

        bool inventoryWindowOpen = false;
        // Editor-only cursor-lock override. Runtime's main loop has no
        // menu/pause and computes wantsCursorLock from `!menuOpen`
        // directly. The Editor's drawGameViewPanel uses this flag to
        // temporarily release the cursor when the user opens an
        // in-game inventory or pause UI, then restore it on close.
        bool cursorLockSuppressed = false;

        // The genuine runtime/session state (projectiles, inventory,
        // cursor-lock tracking, play-elapsed-time) - moved into a shared
        // Engine-side struct (GameForger/Runtime/GameplayLoop.hpp) so
        // GameForgerRuntime can drive the identical gameplay tick loop
        // standalone. Everything else in PlayModeState above/below is
        // Editor-Play-button bookkeeping with no Runtime equivalent
        // (saved pre-Play snapshots, the Editor's own separate Play-mode
        // viewport camera, etc).
        GameplayState gameplay;
    };

    // Height sculpting for a selected isTerrain entity - while active,
    // left-click-drag in the Viewport paints instead of the normal
    // select/gizmo behavior (see the Viewport mouse handling in
    // drawEditorPanels). Lower fires when EITHER lowerMode is toggled on in
    // the Inspector OR Shift is held (Unity's own convention) - both stack
    // via XOR, so Shift still works as a quick temporary override of
    // whichever mode is currently active.
    struct TerrainSculptState
    {
        bool active = false;
        bool lowerMode = false;
        float brushRadius = 4.0F;
        float brushStrength = 3.0F;
        // When true, left-click-drag paints splat weight into
        // `paintLayerIndex` instead of sculpting height - mutually
        // exclusive with Raise/Lower (see the Inspector's radio group).
        bool paintMode = false;
        int paintLayerIndex = 0; // 0..2, which of TerrainData::layers to paint
    };

    struct AnimationPanelState
    {
        float scrubTime = 0.0F;
        bool previewPlaying = false;
        float previewTime = 0.0F;
        // While on, any pose the entity is left in (gizmo or Inspector) is
        // continuously saved as the keyframe at scrubTime, every frame.
        bool recordMode = false;
    };

    // A "shot" is just a cine-camera's own captured EntityAnimation path (not
    // a full scene snapshot) - see StoryboardState below and the Storyboard
    // panel, which lists these by number and can play them back in sequence
    // as "the movie".
    struct CineShot
    {
        std::string name;
        EntityAnimation cameraPath;
    };

    struct StoryboardState
    {
        std::vector<CineShot> shots;
        bool windowOpen = false;

        // Live playback - either previewing a single shot on demand, or (when
        // playingMovie is set) stepping through shots[] in order. Both drive
        // the same Cine Camera Preview window via the same elapsed-time tick.
        bool isPlaying = false;
        float playTime = 0.0F;
        int previewShotIndex = -1;

        bool playingMovie = false;
        int movieShotIndex = -1;
    };

    struct AIAnimationState
    {
        std::array<char, 2048> prompt{};
        std::string status;
        bool statusSuccess = false;

        std::atomic<bool> generating{false};
        std::thread worker;
        std::mutex resultMutex;
        bool hasResult = false;
        AIAnimationResult result;
    };

    struct RenameState
    {
        bool requestOpen = false;
        int entityId = -1;
        std::array<char, 128> buffer{};
    };

    struct ScriptCreatorState
    {
        bool requestOpen = false;
        std::string targetEntityName;

        std::array<char, 128> existingPath{};
        std::string attachStatus;
        bool attachStatusSuccess = false;

        std::array<char, 64> scriptName{};
        std::array<char, 2048> description{};

        std::atomic<bool> generating{false};
        std::thread worker;
        std::mutex resultMutex;
        bool hasResult = false;
        bool resultSuccess = false;
        std::string resultContent;

        std::array<char, 8192> previewBuffer{};
        bool previewSynced = false;
        bool resultLogged = false;
    };

    struct ScriptEditorState
    {
        bool requestOpen = false;
        std::string scriptPath;
        std::array<char, 16384> buffer{};
        std::string status;
        bool statusSuccess = false;
    };

    struct Ray
    {
        glm::vec3 origin;
        glm::vec3 direction;
    };

    struct AIProvider
    {
        const char* id;
        const char* displayName;
        const char* endpoint;
        const char* model;
        const char* keySource;
    };

    struct AISetupState
    {
        int selectedProvider = 0;
        std::array<char, 256> endpoint{};
        std::array<char, 128> model{};
        bool calibrated = false;
        bool setupChanged = false;
        // Filled in by the Test provider button: true if a probe request
        // succeeded, plus a short redacted message describing what the
        // provider actually reported. Empty until the user presses the button.
        bool tested = false;
        bool testSucceeded = false;
        std::string testMessage;
    };

    enum class ThemeChoice
    {
        DefaultDark,
        GameForgerAI
    };

    struct AppearanceState
    {
        ThemeChoice theme = ThemeChoice::GameForgerAI;
    };

    // Placement only, per explicit request - no translated strings exist yet.
    struct LanguageState
    {
        int selectedLanguage = 0; // 0 = English, 1 = Francais (soon), 2 = Mandarin (soon)
    };

    struct SettingsState
    {
        bool open = false;
        int selectedCategory = 0;
    };

    struct AIForgeState
    {
        std::vector<AIEditorCommand> pendingCommands;
        std::vector<std::string> pendingDescriptions;
        std::string status;
        bool statusSuccess = false;

        std::atomic<bool> planning{false};
        std::thread worker;
        std::mutex resultMutex;
        bool hasPlanResult = false;
        AIPlanResult planResult;
    };

    constexpr std::array<AIProvider, 2> providers{
        AIProvider{
            "nvidia",
            "NVIDIA NIM",
            "https://integrate.api.nvidia.com/v1/chat/completions",
            "nvidia/nemotron-3-nano-30b-a3b",
            "NVIDIA_API_KEY"},
        AIProvider{
            "agnes-ai",
            "Agnes-AI",
            "https://apihub.agnes-ai.com/v1/chat/completions",
            "agnes-2.0-flash",
            "AGNES_AI_API_KEY"}};

    const char* primitiveTypeName(const PrimitiveType type)
    {
        switch (type)
        {
            case PrimitiveType::Cube: return "Cube";
            case PrimitiveType::Sphere: return "Sphere";
            case PrimitiveType::Cylinder: return "Cylinder";
            case PrimitiveType::Cone: return "Cone";
            case PrimitiveType::Plane: return "Plane";
            case PrimitiveType::Capsule: return "Capsule";
        }
        return "Unknown";
    }

    std::string makeMenuEntityName(const EditorScene& scene, const std::string& baseName)
    {
        if (scene.findEntity(baseName) == nullptr)
        {
            return baseName;
        }
        int suffix = 1;
        std::string candidate;
        do
        {
            candidate = baseName + " (" + std::to_string(suffix) + ")";
            ++suffix;
        } while (scene.findEntity(candidate) != nullptr);
        return candidate;
    }

    std::string sanitizeScriptFileName(const std::string& rawName)
    {
        std::string result;
        for (const char character : rawName)
        {
            if (std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_' || character == '-')
            {
                result += character;
            }
            else if (character == ' ')
            {
                result += '_';
            }
        }
        return result.empty() ? "Script" : result;
    }

    std::string makeUniqueScriptPath(const std::filesystem::path& projectRoot, const std::string& baseName)
    {
        const std::string sanitized = sanitizeScriptFileName(baseName);
        std::string candidate = "Game/Scripts/" + sanitized + ".lua";
        int suffix = 2;
        while (std::filesystem::exists(projectRoot / candidate))
        {
            candidate = "Game/Scripts/" + sanitized + "_" + std::to_string(suffix) + ".lua";
            ++suffix;
        }
        return candidate;
    }

    void spawnPrimitive(
        EditorScene& scene,
        AICommandBus& commandBus,
        SelectionState& selection,
        const PrimitiveType primitive,
        const char* baseName)
    {
        CreateEntityCommand command;
        command.primitive = primitive;
        command.name = makeMenuEntityName(scene, baseName);
        const AICommandResult result = commandBus.execute(command);
        if (result.success)
        {
            if (const SceneEntity* created = scene.findEntity(command.name))
            {
                selectOnly(selection, created->id);
            }
        }
    }

    // Like spawnPrimitive, but parents the new entity to `parentName` right
    // away (Hierarchy panel's "Add Child") - defaults Local Position to
    // (0,1,0) (SceneEntity::localPosition's own default) so it doesn't
    // spawn fully overlapping its parent's origin.
    void spawnChildPrimitive(
        EditorScene& scene,
        AICommandBus& commandBus,
        SelectionState& selection,
        const PrimitiveType primitive,
        const char* baseName,
        const std::string& parentName)
    {
        CreateEntityCommand command;
        command.primitive = primitive;
        command.name = makeMenuEntityName(scene, baseName);
        const AICommandResult result = commandBus.execute(command);
        if (!result.success)
        {
            return;
        }
        commandBus.execute(SetPropertyCommand{command.name, "Parent", "parentName", parentName});
        if (const SceneEntity* created = scene.findEntity(command.name))
        {
            selectOnly(selection, created->id);
        }
    }

    void duplicateSelected(EditorScene& scene, AICommandBus& commandBus, SelectionState& selection)
    {
        if (!selection.selectedEntityId.has_value())
        {
            return;
        }
        const SceneEntity* entity = scene.findEntity(*selection.selectedEntityId);
        if (entity == nullptr)
        {
            return;
        }
        const AICommandResult result = commandBus.execute(DuplicateEntityCommand{entity->name});
        if (result.success && !scene.entities().empty())
        {
            // DuplicateEntityCommand always appends the new entity at the end.
            selectOnly(selection, scene.entities().back().id);
        }
    }

    // Recreates full copies of `clipboardEntities` (new ids/unique names, offset
    // position, everything else - color/tag/pivot/scripts/animation - kept) and
    // selects the new copies. The clipboard holds a snapshot of the entities at
    // copy time, independent of whatever happens to the originals afterward.
    void pasteClipboardEntities(
        EditorScene& scene,
        AICommandBus& commandBus,
        SelectionState& selection,
        const std::vector<SceneEntity>& clipboardEntities)
    {
        std::vector<int> pastedIds;
        for (const SceneEntity& source : clipboardEntities)
        {
            CreateEntityCommand createCommand;
            createCommand.primitive = source.primitive;
            createCommand.name = makeMenuEntityName(scene, source.name);
            createCommand.position = source.position + glm::vec3(0.5F, 0.0F, 0.5F);
            const AICommandResult result = commandBus.execute(createCommand);
            if (!result.success || scene.entities().empty())
            {
                continue;
            }
            const int newId = scene.entities().back().id;
            if (SceneEntity* created = scene.findEntityMutable(newId))
            {
                const int keepId = created->id;
                const std::string keepName = created->name;
                const glm::vec3 keepPosition = created->position;
                *created = source;
                created->id = keepId;
                created->name = keepName;
                created->position = keepPosition;
            }
            pastedIds.push_back(newId);
        }
        if (!pastedIds.empty())
        {
            selection.multiSelectedIds = pastedIds;
            selection.shiftAnchorId.reset();
            syncPrimarySelection(selection);
        }
    }

    // Targeted text replace of just the "startupScene" value in
    // Game/Project.json, not a full parse+reserialize - this project's json::
    // namespace only supports parsing, no generic writer (SceneSerializer
    // hand-builds its own JSON text for the same reason), and Project.json's
    // structure/formatting is otherwise hand-authored and should stay
    // untouched by this. Returns false (leaving the file alone) if the
    // expected "startupScene": "..." substring isn't found, rather than
    // guessing at a different shape.
    bool updateProjectStartupScene(const std::filesystem::path& projectJsonPath, const std::string& newRelativePath)
    {
        std::ifstream inFile(projectJsonPath, std::ios::binary);
        if (!inFile)
        {
            return false;
        }
        std::string text((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
        inFile.close();

        const std::string key = "\"startupScene\": \"";
        const std::size_t keyStart = text.find(key);
        if (keyStart == std::string::npos)
        {
            return false;
        }
        const std::size_t valueStart = keyStart + key.size();
        const std::size_t valueEnd = text.find('"', valueStart);
        if (valueEnd == std::string::npos)
        {
            return false;
        }
        text.replace(valueStart, valueEnd - valueStart, newRelativePath);

        // Atomic write, same pattern as SceneSerializer::saveScene: write to a sibling
        // temp file, flush, keep a .bak of the previous contents, then rename over the
        // destination - a crash or power loss mid-write can no longer truncate
        // Project.json to 0 bytes.
        const std::filesystem::path tempPath = projectJsonPath.string() + ".tmp";
        {
            std::ofstream outFile(tempPath, std::ios::binary | std::ios::trunc);
            if (!outFile)
            {
                return false;
            }
            outFile << text;
            if (!outFile)
            {
                return false;
            }
            outFile.flush();
            if (!outFile)
            {
                return false;
            }
        }
        const std::filesystem::path backupPath = projectJsonPath.string() + ".bak";
        std::error_code ec;
        std::filesystem::copy(
            projectJsonPath, backupPath,
            std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(projectJsonPath, ec);
        std::filesystem::rename(tempPath, projectJsonPath, ec);
        if (ec)
        {
            std::filesystem::remove(tempPath, ec);
            return false;
        }
        return true;
    }

    void saveSceneAndLog(const EditorScene& scene, const std::filesystem::path& scenePath, ConsoleState& console)
    {
        const SceneSaveResult result = saveScene(scenePath, scene.entities());
        logMessage(console, result.success ? LogLevel::Info : LogLevel::Error, result.message);
    }

    // Loads `filePath` and, on success, replaces the scene and clears
    // selection and undo/redo history - the old entity ids no longer refer to
    // anything once the scene is swapped out.
    bool loadSceneAndLog(
        EditorScene& scene,
        const std::filesystem::path& filePath,
        SelectionState& selection,
        EditHistoryState& history,
        ConsoleState& console)
    {
        SceneLoadResult result = loadScene(filePath);
        logMessage(console, result.success ? LogLevel::Info : LogLevel::Error, result.message);
        if (!result.success)
        {
            return false;
        }
        scene.loadEntities(std::move(result.entities));
        clearSelection(selection);
        history.undoStack.clear();
        history.redoStack.clear();
        return true;
    }

    // Native "Save As" dialog. Uses OFN_NOCHANGEDIR so picking a location
    // elsewhere doesn't change the process's working directory - every
    // relative path in this app (icons, scripts, the quick-save scene path)
    // is resolved against it.
    std::optional<std::filesystem::path> showSaveSceneDialog(
        const HWND owner, const std::filesystem::path& initialDirectory)
    {
        std::array<wchar_t, MAX_PATH> fileBuffer{};
        wcscpy_s(fileBuffer.data(), fileBuffer.size(), L"Scene.gfprod");

        const std::wstring initialDirectoryWide = initialDirectory.wstring();
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner;
        dialog.lpstrFilter = L"GameForger Project (*.gfprod)\0*.gfprod\0All Files (*.*)\0*.*\0";
        dialog.lpstrFile = fileBuffer.data();
        dialog.nMaxFile = static_cast<DWORD>(fileBuffer.size());
        dialog.lpstrInitialDir = initialDirectoryWide.c_str();
        dialog.lpstrDefExt = L"gfprod";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
        if (GetSaveFileNameW(&dialog) == FALSE)
        {
            return std::nullopt;
        }
        return std::filesystem::path(fileBuffer.data());
    }

    // Native "Open" dialog, same OFN_NOCHANGEDIR reasoning as above.
    std::optional<std::filesystem::path> showOpenSceneDialog(
        const HWND owner, const std::filesystem::path& initialDirectory)
    {
        std::array<wchar_t, MAX_PATH> fileBuffer{};
        const std::wstring initialDirectoryWide = initialDirectory.wstring();
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner;
        dialog.lpstrFilter = L"GameForger Project (*.gfprod)\0*.gfprod\0All Files (*.*)\0*.*\0";
        dialog.lpstrFile = fileBuffer.data();
        dialog.nMaxFile = static_cast<DWORD>(fileBuffer.size());
        dialog.lpstrInitialDir = initialDirectoryWide.c_str();
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&dialog) == FALSE)
        {
            return std::nullopt;
        }
        return std::filesystem::path(fileBuffer.data());
    }

    // Native "Open" dialog filtered to font files, for the Toolbox's Text
    // Mesh tool and the Inspector's "Change Font..." button.
    std::optional<std::filesystem::path> showOpenFontDialog(
        const HWND owner, const std::filesystem::path& initialDirectory)
    {
        std::array<wchar_t, MAX_PATH> fileBuffer{};
        const std::wstring initialDirectoryWide = initialDirectory.wstring();
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner;
        dialog.lpstrFilter = L"Fonts (*.ttf;*.otf)\0*.ttf;*.otf\0All Files (*.*)\0*.*\0";
        dialog.lpstrFile = fileBuffer.data();
        dialog.nMaxFile = static_cast<DWORD>(fileBuffer.size());
        dialog.lpstrInitialDir = initialDirectoryWide.c_str();
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&dialog) == FALSE)
        {
            return std::nullopt;
        }
        return std::filesystem::path(fileBuffer.data());
    }

    // Native "Open" dialog filtered to common raster image formats, for the
    // Terrain Inspector section's "Import Heightmap..." button. Unlike fonts
    // and scripts, an imported heightmap isn't kept as a referenced project
    // asset - it's read once and baked directly into the terrain's own
    // heights array, so there's no matching importIntoProject step.
    std::optional<std::filesystem::path> showOpenImageDialog(
        const HWND owner, const std::filesystem::path& initialDirectory)
    {
        std::array<wchar_t, MAX_PATH> fileBuffer{};
        const std::wstring initialDirectoryWide = initialDirectory.wstring();
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner;
        dialog.lpstrFilter =
            L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.tga)\0*.png;*.jpg;*.jpeg;*.bmp;*.tga\0All Files (*.*)\0*.*\0";
        dialog.lpstrFile = fileBuffer.data();
        dialog.nMaxFile = static_cast<DWORD>(fileBuffer.size());
        dialog.lpstrInitialDir = initialDirectoryWide.c_str();
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&dialog) == FALSE)
        {
            return std::nullopt;
        }
        return std::filesystem::path(fileBuffer.data());
    }

    // Loads a grayscale heightmap image and resamples it (nearest-neighbor)
    // into a resolution x resolution heights array, each value 0..1. Returns
    // an empty vector on failure.
    std::vector<float> loadHeightmapImage(const std::filesystem::path& imagePath, const int resolution)
    {
        int imageWidth = 0;
        int imageHeight = 0;
        int imageChannels = 0;
        unsigned char* pixels = stbi_load(imagePath.string().c_str(), &imageWidth, &imageHeight, &imageChannels, 1);
        if (pixels == nullptr || imageWidth <= 0 || imageHeight <= 0)
        {
            return {};
        }
        std::vector<float> heights(static_cast<std::size_t>(resolution) * static_cast<std::size_t>(resolution));
        for (int row = 0; row < resolution; ++row)
        {
            const int sourceY = std::clamp(
                row * imageHeight / resolution, 0, imageHeight - 1);
            for (int col = 0; col < resolution; ++col)
            {
                const int sourceX = std::clamp(col * imageWidth / resolution, 0, imageWidth - 1);
                const unsigned char value = pixels[static_cast<std::size_t>(sourceY) * imageWidth + sourceX];
                heights[static_cast<std::size_t>(row) * resolution + col] = static_cast<float>(value) / 255.0F;
            }
        }
        stbi_image_free(pixels);
        return heights;
    }

    // Classic 2D Perlin noise (Ken Perlin's reference algorithm, permutation
    // table seeded/shuffled per-call so "Generate" is reproducible for a
    // given seed) - no new dependency, ~50 lines is the whole algorithm.
    struct PerlinPermutation
    {
        std::array<int, 512> table{};
        explicit PerlinPermutation(const unsigned int seed)
        {
            std::array<int, 256> base{};
            for (int i = 0; i < 256; ++i)
            {
                base[static_cast<std::size_t>(i)] = i;
            }
            std::mt19937 rng(seed);
            std::shuffle(base.begin(), base.end(), rng);
            for (int i = 0; i < 512; ++i)
            {
                table[static_cast<std::size_t>(i)] = base[static_cast<std::size_t>(i & 255)];
            }
        }
    };

    float perlinFade(const float t) noexcept
    {
        return t * t * t * (t * (t * 6.0F - 15.0F) + 10.0F);
    }

    float perlinGrad(const int hash, const float x, const float y) noexcept
    {
        const int h = hash & 7;
        const float u = h < 4 ? x : y;
        const float v = h < 4 ? y : x;
        return ((h & 1) != 0 ? -u : u) + ((h & 2) != 0 ? -2.0F * v : 2.0F * v);
    }

    float perlinNoise2D(const PerlinPermutation& perm, const float x, const float y) noexcept
    {
        const int xi = static_cast<int>(std::floor(x)) & 255;
        const int yi = static_cast<int>(std::floor(y)) & 255;
        const float xf = x - std::floor(x);
        const float yf = y - std::floor(y);
        const float u = perlinFade(xf);
        const float v = perlinFade(yf);
        const auto p = [&perm](const int i) { return perm.table[static_cast<std::size_t>(i)]; };
        const int aa = p(p(xi) + yi);
        const int ab = p(p(xi) + yi + 1);
        const int ba = p(p(xi + 1) + yi);
        const int bb = p(p(xi + 1) + yi + 1);
        const float lerpX1 = std::lerp(perlinGrad(aa, xf, yf), perlinGrad(ba, xf - 1.0F, yf), u);
        const float lerpX2 = std::lerp(perlinGrad(ab, xf, yf - 1.0F), perlinGrad(bb, xf - 1.0F, yf - 1.0F), u);
        return std::lerp(lerpX1, lerpX2, v);
    }

    // Sums several octaves of perlinNoise2D for a more natural, less
    // uniform-looking result, normalized back to roughly -1..1.
    float fractalPerlinNoise2D(
        const PerlinPermutation& perm, const float x, const float y, const int octaves, const float persistence)
    {
        float total = 0.0F;
        float frequency = 1.0F;
        float amplitude = 1.0F;
        float maxAmplitude = 0.0F;
        for (int octave = 0; octave < octaves; ++octave)
        {
            total += perlinNoise2D(perm, x * frequency, y * frequency) * amplitude;
            maxAmplitude += amplitude;
            amplitude *= persistence;
            frequency *= 2.0F;
        }
        return maxAmplitude > 0.0F ? total / maxAmplitude : 0.0F;
    }

    // Native "Open" dialog filtered to the model formats ModelImport.cpp
    // (Assimp) actually supports - see CMakeLists.txt's
    // ASSIMP_BUILD_*_IMPORTER flags.
    std::optional<std::filesystem::path> showOpenModelDialog(
        const HWND owner, const std::filesystem::path& initialDirectory)
    {
        std::array<wchar_t, MAX_PATH> fileBuffer{};
        const std::wstring initialDirectoryWide = initialDirectory.wstring();
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner;
        dialog.lpstrFilter =
            L"3D Models (*.glb;*.gltf;*.fbx;*.3ds;*.obj;*.blend)\0*.glb;*.gltf;*.fbx;*.3ds;*.obj;*.blend\0"
            L"All Files (*.*)\0*.*\0";
        dialog.lpstrFile = fileBuffer.data();
        dialog.nMaxFile = static_cast<DWORD>(fileBuffer.size());
        dialog.lpstrInitialDir = initialDirectoryWide.c_str();
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&dialog) == FALSE)
        {
            return std::nullopt;
        }
        return std::filesystem::path(fileBuffer.data());
    }

    // Copies a model picked from anywhere on disk into Game/Models/ - same
    // "become a real, stable project asset" reasoning as fonts/scripts.
    // Returns the new path relative to projectRoot, or nullopt on failure.
    std::optional<std::string> importModelIntoProject(
        const std::filesystem::path& sourceModelPath, const std::filesystem::path& projectRoot)
    {
        const std::filesystem::path modelsDirectory = projectRoot / "Game" / "Models";
        std::error_code directoryError;
        std::filesystem::create_directories(modelsDirectory, directoryError);
        if (directoryError)
        {
            return std::nullopt;
        }
        const std::filesystem::path destination = modelsDirectory / sourceModelPath.filename();
        if (!std::filesystem::exists(destination))
        {
            std::error_code copyError;
            std::filesystem::copy_file(sourceModelPath, destination, copyError);
            if (copyError)
            {
                return std::nullopt;
            }
        }
        std::error_code relativeError;
        const std::filesystem::path relative = std::filesystem::relative(destination, projectRoot, relativeError);
        if (relativeError)
        {
            return std::nullopt;
        }
        return relative.generic_string();
    }

    // Hierarchy panel's "Add Child > Upload Model..." - mirrors the
    // Character menu's "Import Model..." handler just above (file dialog +
    // import + CreateImportedMeshCommand) but also parents the result, and
    // deliberately skips that handler's camera auto-frame-on-import step
    // (framing on a child's bounds while the parent may be far away/large
    // would just be disorienting here, not a meaningful loss - the parent
    // is presumably already framed since it's what was right-clicked).
    void spawnChildModel(
        EditorScene& scene,
        AICommandBus& commandBus,
        SelectionState& selection,
        ConsoleState& console,
        const std::filesystem::path& projectRoot,
        const HWND nativeWindowHandle,
        const std::string& parentName)
    {
        const std::filesystem::path modelsDirectory = projectRoot / "Game" / "Models";
        const std::optional<std::filesystem::path> picked =
            showOpenModelDialog(nativeWindowHandle, modelsDirectory);
        if (!picked.has_value())
        {
            return;
        }
        const std::optional<std::string> imported = importModelIntoProject(*picked, projectRoot);
        if (!imported.has_value())
        {
            logMessage(console, LogLevel::Error, "Could not import that model into the project.");
            return;
        }

        CreateImportedMeshCommand command;
        command.name = makeMenuEntityName(scene, picked->stem().string());
        command.sourcePath = *imported;
        const AICommandResult result = commandBus.execute(command);
        logMessage(console, result.success ? LogLevel::Info : LogLevel::Error, result.message);
        if (!result.success)
        {
            return;
        }
        commandBus.execute(SetPropertyCommand{command.name, "Parent", "parentName", parentName});
        if (const SceneEntity* created = scene.findEntity(command.name))
        {
            selectOnly(selection, created->id);
        }
    }

    // Copies a font picked from anywhere on disk into Game/Fonts/ (creating
    // the folder if needed) so it becomes a real, stable project asset - the
    // same reasoning as scripts living under Game/Scripts/. Returns the new
    // path relative to projectRoot, or nullopt on failure. If a file with the
    // same name already exists there, reuses it as-is rather than
    // overwriting (most likely the same font imported before).
    std::optional<std::string> importFontIntoProject(
        const std::filesystem::path& sourceFontPath, const std::filesystem::path& projectRoot)
    {
        const std::filesystem::path fontsDirectory = projectRoot / "Game" / "Fonts";
        std::error_code directoryError;
        std::filesystem::create_directories(fontsDirectory, directoryError);
        if (directoryError)
        {
            return std::nullopt;
        }
        const std::filesystem::path destination = fontsDirectory / sourceFontPath.filename();
        if (!std::filesystem::exists(destination))
        {
            std::error_code copyError;
            std::filesystem::copy_file(sourceFontPath, destination, copyError);
            if (copyError)
            {
                return std::nullopt;
            }
        }
        std::error_code relativeError;
        const std::filesystem::path relative = std::filesystem::relative(destination, projectRoot, relativeError);
        if (relativeError)
        {
            return std::nullopt;
        }
        return relative.generic_string();
    }

    // Copies a picked texture file into Game/Textures/ (mirrors
    // importFontIntoProject exactly) - used by the Terrain layer texture
    // pickers' "Upload from PC..." option, so an uploaded map becomes a
    // stable project asset like every other imported file rather than a
    // dangling absolute path.
    std::optional<std::string> importTextureIntoProject(
        const std::filesystem::path& sourceTexturePath, const std::filesystem::path& projectRoot)
    {
        const std::filesystem::path texturesDirectory = projectRoot / "Game" / "Textures";
        std::error_code directoryError;
        std::filesystem::create_directories(texturesDirectory, directoryError);
        if (directoryError)
        {
            return std::nullopt;
        }
        const std::filesystem::path destination = texturesDirectory / sourceTexturePath.filename();
        if (!std::filesystem::exists(destination))
        {
            std::error_code copyError;
            std::filesystem::copy_file(sourceTexturePath, destination, copyError);
            if (copyError)
            {
                return std::nullopt;
            }
        }
        std::error_code relativeError;
        const std::filesystem::path relative = std::filesystem::relative(destination, projectRoot, relativeError);
        if (relativeError)
        {
            return std::nullopt;
        }
        return relative.generic_string();
    }

    // Same idea as importTextureIntoProject, targeting Game/Icons/ instead
    // of Game/Textures/ - used by the Pickup Item Inspector section's icon
    // picker "Import Icon from PC..." option.
    std::optional<std::string> importIconIntoProject(
        const std::filesystem::path& sourceIconPath, const std::filesystem::path& projectRoot)
    {
        const std::filesystem::path iconsDirectory = projectRoot / "Game" / "Icons";
        std::error_code directoryError;
        std::filesystem::create_directories(iconsDirectory, directoryError);
        if (directoryError)
        {
            return std::nullopt;
        }
        const std::filesystem::path destination = iconsDirectory / sourceIconPath.filename();
        if (!std::filesystem::exists(destination))
        {
            std::error_code copyError;
            std::filesystem::copy_file(sourceIconPath, destination, copyError);
            if (copyError)
            {
                return std::nullopt;
            }
        }
        std::error_code relativeError;
        const std::filesystem::path relative = std::filesystem::relative(destination, projectRoot, relativeError);
        if (relativeError)
        {
            return std::nullopt;
        }
        return relative.generic_string();
    }

    // Loads (once per distinct path, cached in a function-local static -
    // there are only ever a few dozen distinct icons in play at once, so
    // this simple persistent cache is enough) and uploads an icon image as
    // a small GL texture for ImGui::Image - used by both the Inspector's
    // icon picker (thumbnails + preview) and the inventory grid (Task 18).
    // Returns 0 on failure (caller should just skip drawing the image).
    GLuint ensureIconTextureGpu(const std::string& relativePath, const std::filesystem::path& projectRoot)
    {
        static std::unordered_map<std::string, GLuint> cache;
        const auto found = cache.find(relativePath);
        if (found != cache.end())
        {
            return found->second;
        }

        GLuint textureId = 0;
        // Top-down rows: ImGui::Image samples with a top-left origin.
        const LoadedTexture image = gameforger::editor::loadTextureImageTopDown(projectRoot / relativePath);
        if (image.success)
        {
            glGenTextures(1, &textureId);
            glBindTexture(GL_TEXTURE_2D, textureId);
            glTexImage2D(
                GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                image.rgba.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        cache[relativePath] = textureId;
        return textureId;
    }

    // The shared gameplay HUD (GameplayHud.hpp) drawn onto an ImGui draw
    // list - the Game view's implementation; GameForgerRuntime has its own
    // GL one (RuntimeHud), so both show the same effects/HUD.
    class ImGuiHudCanvas final : public HudCanvas
    {
    public:
        ImGuiHudCanvas(ImDrawList* drawList, std::filesystem::path projectRoot)
            : drawList_(drawList), projectRoot_(std::move(projectRoot))
        {
        }

        void line(const glm::vec2& from, const glm::vec2& to, const HudColor& color, const float thickness) override
        {
            drawList_->AddLine(ImVec2(from.x, from.y), ImVec2(to.x, to.y), toU32(color), thickness);
        }
        void rectFilled(const glm::vec2& min, const glm::vec2& max, const HudColor& color, const float rounding) override
        {
            drawList_->AddRectFilled(ImVec2(min.x, min.y), ImVec2(max.x, max.y), toU32(color), rounding);
        }
        void rect(const glm::vec2& min, const glm::vec2& max, const HudColor& color, const float rounding,
            const float thickness) override
        {
            drawList_->AddRect(ImVec2(min.x, min.y), ImVec2(max.x, max.y), toU32(color), rounding, 0, thickness);
        }
        void circleFilled(const glm::vec2& center, const float radius, const HudColor& color) override
        {
            drawList_->AddCircleFilled(ImVec2(center.x, center.y), radius, toU32(color));
        }
        void text(const glm::vec2& position, const HudColor& color, const std::string& text, const float scale) override
        {
            drawList_->AddText(
                ImGui::GetFont(), ImGui::GetFontSize() * scale, ImVec2(position.x, position.y), toU32(color), text.c_str());
        }
        [[nodiscard]] glm::vec2 textSize(const std::string& text, const float scale) override
        {
            const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(
                ImGui::GetFontSize() * scale, std::numeric_limits<float>::max(), 0.0F, text.c_str());
            return {size.x, size.y};
        }
        void image(const std::string& projectRelativePath, const glm::vec2& min, const glm::vec2& max) override
        {
            const GLuint texture = ensureIconTextureGpu(projectRelativePath, projectRoot_);
            if (texture != 0)
            {
                drawList_->AddImage(static_cast<ImTextureID>(texture), ImVec2(min.x, min.y), ImVec2(max.x, max.y));
            }
        }

    private:
        static ImU32 toU32(const HudColor& color)
        {
            return ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a));
        }

        ImDrawList* drawList_;
        std::filesystem::path projectRoot_;
    };

    // Scripts grouped by their `-- @preset <Name> | <role>` tag (see
    // ScriptRuntime::ScriptPresetTag), e.g. "FPS Demo" -> fps_player.lua
    // (player), health.lua (damageable), items.lua (item)... Re-scanned at
    // most once a second.
    struct PresetScript
    {
        std::string path; // relative to projectRoot
        std::string role;
    };
    const std::map<std::string, std::vector<PresetScript>>& scriptPresets(const std::filesystem::path& projectRoot)
    {
        static std::map<std::string, std::vector<PresetScript>> presets;
        static double lastScanTime = -1000.0;
        const double now = ImGui::GetTime();
        if (now - lastScanTime < 1.0)
        {
            return presets;
        }
        lastScanTime = now;
        presets.clear();
        std::error_code error;
        const std::filesystem::path scriptsRoot = projectRoot / "Game" / "Scripts";
        for (const auto& entry : std::filesystem::directory_iterator(scriptsRoot, error))
        {
            if (entry.path().extension() != ".lua")
            {
                continue;
            }
            const ScriptRuntime::ScriptPresetTag tag = ScriptRuntime::cachedScriptPreset(entry.path());
            if (!tag.name.empty())
            {
                presets[tag.name].push_back(
                    {"Game/Scripts/" + entry.path().filename().string(), tag.role});
            }
        }
        return presets;
    }

    // The preset scripts that belong on the same object as a "player" /
    // "damageable" script (everything but items/managers), in the order the
    // FPS Demo preset lists them when it's that preset.
    std::vector<std::string> presetLinkableScripts(
        const std::filesystem::path& projectRoot, const std::string& presetName)
    {
        std::vector<std::string> paths;
        const auto& presets = scriptPresets(projectRoot);
        const auto found = presets.find(presetName);
        if (found == presets.end())
        {
            return paths;
        }
        for (const std::string& known : fpsDemoPlayerScripts())
        {
            for (const PresetScript& script : found->second)
            {
                if (script.path == known && (script.role == "player" || script.role == "damageable"))
                {
                    paths.push_back(script.path);
                }
            }
        }
        for (const PresetScript& script : found->second)
        {
            if ((script.role == "player" || script.role == "damageable") &&
                std::find(paths.begin(), paths.end(), script.path) == paths.end())
            {
                paths.push_back(script.path);
            }
        }
        return paths;
    }

    // Copies a picked file into <projectRoot>/<relativeFolder> (keeping an
    // existing same-named file), returns the new project-relative path.
    std::optional<std::string> importFileIntoFolder(
        const std::filesystem::path& source, const std::filesystem::path& projectRoot, const std::string& relativeFolder)
    {
        const std::filesystem::path folder = projectRoot / relativeFolder;
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        if (error)
        {
            return std::nullopt;
        }
        const std::filesystem::path destination = folder / source.filename();
        if (!std::filesystem::exists(destination))
        {
            std::filesystem::copy_file(source, destination, error);
            if (error)
            {
                return std::nullopt;
            }
        }
        const std::filesystem::path relative = std::filesystem::relative(destination, projectRoot, error);
        if (error)
        {
            return std::nullopt;
        }
        return relative.generic_string();
    }

    // Every image under the built-in icon pack and Game/Icons/ (where
    // "Change Icon..." imports land), relative to projectRoot. Re-scanned at
    // most once a second - it's called from the Inspector every frame
    // while an icon combo is open.
    const std::vector<std::string>& projectIconPaths(const std::filesystem::path& projectRoot)
    {
        static std::vector<std::string> paths;
        static double lastScanTime = -1000.0;
        const double now = ImGui::GetTime();
        if (now - lastScanTime < 1.0)
        {
            return paths;
        }
        lastScanTime = now;
        paths.clear();
        const std::array<std::filesystem::path, 2> iconRoots{
            projectRoot / "Game" / "Icons", projectRoot / "Game" / "Models" / "iconpack1" / "128"};
        for (const std::filesystem::path& iconRoot : iconRoots)
        {
            std::error_code walkError;
            if (!std::filesystem::exists(iconRoot, walkError))
            {
                continue;
            }
            for (const std::filesystem::directory_entry& fileEntry :
                std::filesystem::recursive_directory_iterator(iconRoot, walkError))
            {
                if (!fileEntry.is_regular_file())
                {
                    continue;
                }
                std::string extension = fileEntry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                    [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (extension != ".png" && extension != ".jpg" && extension != ".jpeg" && extension != ".bmp" &&
                    extension != ".tga")
                {
                    continue;
                }
                std::error_code relativeError;
                const std::string relative =
                    std::filesystem::relative(fileEntry.path(), projectRoot, relativeError).generic_string();
                if (!relativeError)
                {
                    paths.push_back(relative);
                }
            }
        }
        return paths;
    }

    void deleteSelected(EditorScene& scene, AICommandBus& commandBus, SelectionState& selection)
    {
        if (!selection.selectedEntityId.has_value())
        {
            return;
        }
        const SceneEntity* entity = scene.findEntity(*selection.selectedEntityId);
        if (entity == nullptr)
        {
            return;
        }
        commandBus.execute(DeleteEntityCommand{entity->name});
        clearSelection(selection);
    }

    void selectProvider(AISetupState& state, const int providerIndex)
    {
        state.selectedProvider = providerIndex;
        const AIProvider& provider = providers[static_cast<std::size_t>(providerIndex)];
        std::snprintf(state.endpoint.data(), state.endpoint.size(), "%s", provider.endpoint);
        std::snprintf(state.model.data(), state.model.size(), "%s", provider.model);
        state.calibrated = false;
        state.setupChanged = true;
    }

    void glfwErrorCallback(const int error, const char* description)
    {
        std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
    }

    // "GameForgerAI - Editor Version Alpha 0.60" - stage/major/minor come
    // from Version.hpp (CMake-generated from this project's own
    // project(VERSION x.y.z), see Editor/CMakeLists.txt's configure_file
    // call), so this never drifts from what CMakeLists.txt/README.md's
    // Version History say - one source of truth, not a 4th place to
    // remember to bump.
    std::string windowTitle()
    {
        return "GameForgerAI - Editor Version " + std::string(gameforger::editor::kVersionStage) + " " +
            std::to_string(gameforger::editor::kVersionMajor) + "." +
            std::to_string(gameforger::editor::kVersionMinor);
    }

    GLFWwindow* createWindow()
    {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_SAMPLES, 4);

        return glfwCreateWindow(
            1600,
            900,
            windowTitle().c_str(),
            nullptr,
            nullptr);
    }

    // Create > FPS Player / FPS Demo Arena - builds the player, hands and
    // weapons rig (FpsRigBuilder.cpp) where the editor camera is looking,
    // and selects the player.
    void createFpsPlayer(
        EditorScene& scene,
        AICommandBus& commandBus,
        SelectionState& selection,
        ConsoleState& console,
        const EditorCameraState& camera,
        const bool demoArena)
    {
        FpsRigOptions options;
        options.position = glm::vec3(camera.target.x, 0.0F, camera.target.z);
        options.includeDemoContent = demoArena;
        options.includeGround = demoArena;
        const FpsRigBuildResult result = buildFpsPlayerRig(scene, commandBus, options);
        logMessage(console, result.success ? LogLevel::Info : LogLevel::Warning, result.message);
        if (const SceneEntity* player = scene.findEntity(result.playerName))
        {
            selectOnly(selection, player->id);
        }
        logMessage(console, LogLevel::Info,
            "Press Play: WASD move, mouse look, E pick up, I inventory, 1-8 weapons, Left Mouse attack, "
            "Right Mouse aim, R reload, C camera.");
    }

    void drawMainMenu(
        EditorScene& scene,
        AICommandBus& commandBus,
        SelectionState& selection,
        EditorCameraState& camera,
        PlayModeState& playMode,
        ScriptRuntime& scriptRuntime,
        ImGuiInputSource& imguiInputSource,
        const std::filesystem::path& projectRoot,
        ConsoleState& console,
        SettingsState& settings,
        EditHistoryState& history,
        std::filesystem::path& currentScenePath,
        const HWND nativeWindowHandle,
        bool& resetLayout)
    {
        const std::filesystem::path scenesDirectory = projectRoot / "Game" / "Scenes";

        const auto newScene = [&]()
        {
            scene.loadEntities({});
            clearSelection(selection);
            history.undoStack.clear();
            history.redoStack.clear();
            logMessage(console, LogLevel::Info, "New scene created.");
        };
        const auto openScene = [&]()
        {
            if (const std::optional<std::filesystem::path> picked =
                    showOpenSceneDialog(nativeWindowHandle, scenesDirectory))
            {
                if (loadSceneAndLog(scene, *picked, selection, history, console))
                {
                    currentScenePath = *picked;
                }
            }
        };

        if (!playMode.isPlaying && !ImGui::GetIO().WantCaptureKeyboard)
        {
            const ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N))
            {
                newScene();
            }
            else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O))
            {
                openScene();
            }
            else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
            {
                saveSceneAndLog(scene, currentScenePath, console);
            }
            else if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z))
            {
                performUndo(history, scene, selection);
            }
            else if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z))
            {
                performRedo(history, scene, selection);
            }
            else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y))
            {
                performRedo(history, scene, selection);
            }
        }

        if (!ImGui::BeginMainMenuBar())
        {
            return;
        }

        if (playMode.isPlaying)
        {
            ImGui::BeginDisabled();
        }

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New Scene", "Ctrl+N"))
            {
                newScene();
            }
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O"))
            {
                openScene();
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
            {
                saveSceneAndLog(scene, currentScenePath, console);
            }
            if (ImGui::MenuItem("Save Scene As..."))
            {
                if (const std::optional<std::filesystem::path> picked =
                        showSaveSceneDialog(nativeWindowHandle, scenesDirectory))
                {
                    saveSceneAndLog(scene, *picked, console);
                    currentScenePath = *picked;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Import FPS Demo Kit into This Project"))
            {
                // The kit's source: the engine folder this editor was built
                // from (found by walking up from the editor executable).
                std::array<wchar_t, MAX_PATH> exePath{};
                GetModuleFileNameW(nullptr, exePath.data(), static_cast<DWORD>(exePath.size()));
                const std::optional<std::filesystem::path> kitSource =
                    gameforger::editor::findFpsDemoKitSource(std::filesystem::path(exePath.data()).parent_path());
                if (!kitSource.has_value())
                {
                    logMessage(console, LogLevel::Error,
                        "Could not find the FPS Demo kit (Game/Scripts/FPSDemo) next to or above the editor.");
                }
                else
                {
                    const gameforger::editor::FpsDemoKitImportResult imported =
                        gameforger::editor::importFpsDemoKit(*kitSource, projectRoot, false);
                    logMessage(console, imported.success ? LogLevel::Info : LogLevel::Error, imported.message);
                }
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Copies the FPS Demo scripts (Game/Scripts/FPSDemo) and their icons "
                    "(Game/Icons/FPSDemo) into this project. Files already here are kept.");
            }
            if (ImGui::MenuItem("Build Game..."))
            {
                // "Build" = export the current scene to the .gfai extension
                // the standalone Runtime ("the engine") loads, and point
                // Game/Project.json's startupScene at it - .gfprod stays
                // the editor's own working file, untouched. Named after
                // currentScenePath's stem so re-Building after further
                // edits overwrites the same .gfai rather than piling up
                // new ones.
                const std::filesystem::path builtPath =
                    currentScenePath.parent_path() / (currentScenePath.stem().string() + ".gfai");
                saveSceneAndLog(scene, builtPath, console);
                const std::filesystem::path relativeBuiltPath =
                    std::filesystem::relative(builtPath, projectRoot);
                std::string relativeBuiltPathText = relativeBuiltPath.generic_string();
                if (updateProjectStartupScene(projectRoot / "Game" / "Project.json", relativeBuiltPathText))
                {
                    logMessage(
                        console, LogLevel::Info,
                        "Built " + relativeBuiltPathText + " and set it as Project.json's startupScene.");
                }
                else
                {
                    logMessage(
                        console, LogLevel::Error,
                        "Built " + relativeBuiltPathText +
                            " but could not update Project.json's startupScene - check it by hand.");
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            const bool canUndo = !history.undoStack.empty();
            const bool canRedo = !history.redoStack.empty();
            if (!canUndo)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::MenuItem("Undo", "Ctrl+Z"))
            {
                performUndo(history, scene, selection);
            }
            if (!canUndo)
            {
                ImGui::EndDisabled();
            }
            if (!canRedo)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", nullptr, canRedo))
            {
                performRedo(history, scene, selection);
            }
            if (!canRedo)
            {
                ImGui::EndDisabled();
            }
            ImGui::Separator();
            const bool hasSelection = selection.selectedEntityId.has_value();
            if (!hasSelection)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::MenuItem("Duplicate Selected", "Ctrl+D"))
            {
                duplicateSelected(scene, commandBus, selection);
            }
            if (ImGui::MenuItem("Delete Selected", "Del"))
            {
                deleteSelected(scene, commandBus, selection);
            }
            if (!hasSelection)
            {
                ImGui::EndDisabled();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Editor Layout"))
            {
                resetLayout = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("GameObject"))
        {
            if (ImGui::MenuItem("Cube"))
            {
                spawnPrimitive(scene, commandBus, selection, PrimitiveType::Cube, "Cube");
            }
            if (ImGui::MenuItem("Sphere"))
            {
                spawnPrimitive(scene, commandBus, selection, PrimitiveType::Sphere, "Sphere");
            }
            if (ImGui::MenuItem("Cylinder"))
            {
                spawnPrimitive(scene, commandBus, selection, PrimitiveType::Cylinder, "Cylinder");
            }
            if (ImGui::MenuItem("Cone"))
            {
                spawnPrimitive(scene, commandBus, selection, PrimitiveType::Cone, "Cone");
            }
            if (ImGui::MenuItem("Plane"))
            {
                spawnPrimitive(scene, commandBus, selection, PrimitiveType::Plane, "Plane");
            }
            if (ImGui::MenuItem("Capsule"))
            {
                spawnPrimitive(scene, commandBus, selection, PrimitiveType::Capsule, "Capsule");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("FPS Player (Hands + Weapons)"))
            {
                createFpsPlayer(scene, commandBus, selection, console, camera, false);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "A first-person player with fps_player.lua and animated hands holding 8 weapons "
                    "(sword, axe, hammer, pickaxe, pistol, AK-47, taser, chain lightning).");
            }
            if (ImGui::MenuItem("Game Manager"))
            {
                const std::string managerName = createGameManager(scene, commandBus, camera.target);
                if (const SceneEntity* manager = scene.findEntity(managerName))
                {
                    selectOnly(selection, manager->id);
                    logMessage(console, LogLevel::Info,
                        "Created '" + managerName + "' - set the game title and splash logo in the Inspector.");
                }
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("The built game's title, splash logo and intro message (game_manager.lua).");
            }
            if (ImGui::MenuItem("FPS Demo Arena (Player, Weapon Pickups, Targets)"))
            {
                createFpsPlayer(scene, commandBus, selection, console, camera, true);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "The FPS Player plus a ground, one items.lua pickup of every weapon, training dummies, "
                    "a chasing enemy and a rock to mine - press Play and try it.");
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Character"))
        {
            if (ImGui::MenuItem("Import Model (GLB/glTF/FBX/3DS/OBJ/Blend)..."))
            {
                const std::filesystem::path modelsDirectory = projectRoot / "Game" / "Models";
                if (const std::optional<std::filesystem::path> picked =
                        showOpenModelDialog(nativeWindowHandle, modelsDirectory))
                {
                    if (const std::optional<std::string> imported = importModelIntoProject(*picked, projectRoot))
                    {
                        CreateImportedMeshCommand command;
                        command.name = makeMenuEntityName(scene, picked->stem().string());
                        command.sourcePath = *imported;
                        const AICommandResult result = commandBus.execute(command);
                        logMessage(console, result.success ? LogLevel::Info : LogLevel::Error, result.message);
                        if (result.success && !scene.entities().empty())
                        {
                            const SceneEntity& created = scene.entities().back();
                            selectOnly(selection, created.id);

                            // Real-world model files are rarely authored at
                            // this editor's small primitive-sized scale (a
                            // "chair" or "plant" can easily be dozens of
                            // units across) - frame the camera on the
                            // model's actual loaded bounds right away,
                            // rather than leaving it centered on the origin
                            // at the default ~4-unit distance where a large
                            // or off-origin import can be effectively
                            // invisible. ensureImportedMeshGpu() reloads
                            // this same file lazily on the next render()
                            // anyway, so loading it again here isn't wasted
                            // - it's the same one-time cost, just moved earlier.
                            const ModelImportResult built =
                                loadModelMesh(projectRoot / created.importedMesh.sourcePath);
                            if (built.success && !built.vertices.empty())
                            {
                                glm::vec3 minBounds(std::numeric_limits<float>::max());
                                glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
                                const std::size_t stride = static_cast<std::size_t>(built.vertexStride);
                                for (std::size_t i = 0; i < built.vertices.size(); i += stride)
                                {
                                    const glm::vec3 point(
                                        built.vertices[i], built.vertices[i + 1], built.vertices[i + 2]);
                                    minBounds = glm::min(minBounds, point);
                                    maxBounds = glm::max(maxBounds, point);
                                }
                                camera.target = (minBounds + maxBounds) * 0.5F;
                                camera.distance = std::clamp(
                                    glm::length(maxBounds - minBounds) * 1.25F, 2.0F, 500.0F);
                            }
                            else
                            {
                                logMessage(
                                    console,
                                    LogLevel::Error,
                                    "Model entity created, but no renderable mesh data could be loaded from "
                                    "it: " + built.message);
                            }
                        }
                    }
                    else
                    {
                        logMessage(console, LogLevel::Error, "Could not import that model file.");
                    }
                }
            }
            // No skeletal-animation pipeline yet (see Known gaps) - these two
            // stay inert placeholders until that's built. Render disabled
            // (greyed out) so it's obvious to the user that the menu items
            // exist but can't be activated yet, rather than appearing
            // interactive and silently doing nothing on click.
            ImGui::BeginDisabled();
            ImGui::MenuItem("Map Humanoid Skeleton...");
            ImGui::MenuItem("Animation Library...");
            ImGui::EndDisabled();
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Settings"))
        {
            settings.open = true;
        }

        if (playMode.isPlaying)
        {
            ImGui::EndDisabled();
        }

        // The Play/Stop/Pause/Step controls live outside the disabled block above: Stop must
        // stay clickable while playing.
        constexpr float playButtonWidth = 70.0F;
        constexpr float pauseButtonWidth = 70.0F;
        constexpr float stepButtonWidth = 60.0F;
        const float totalControlsWidth = playMode.isPlaying ? (playButtonWidth + pauseButtonWidth + stepButtonWidth + 24.0F) : playButtonWidth;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - totalControlsWidth) * 0.5F);
        if (!playMode.isPlaying)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20F, 0.55F, 0.25F, 1.0F));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25F, 0.65F, 0.30F, 1.0F));
            if (ImGui::Button("Play", ImVec2(playButtonWidth, 0.0F)))
            {
                playMode.isPaused = false;
                playMode.stepOneFrame = false;
                playMode.savedEntities = scene.entities();
                playMode.savedMultiSelectedIds = selection.multiSelectedIds;
                playMode.savedCamera = camera;
                playMode.playCamera = camera;
                playMode.playCamera.dragMode = CameraDragMode::None;
                playMode.gameplay.playElapsedTime = 0.0F;
                playMode.gameCameraDragging = false;
                playMode.gameCameraLookYawDegrees = 0.0F;
                playMode.gameCameraLookPitchDegrees = 0.0F;
                playMode.cursorLockSuppressed = false;
                playMode.gameplay.inventoryItems.clear();
                ensureInventorySlots(playMode.gameplay);
                playMode.gameplay.selectedSlot = 0;
                playMode.gameplay.inventoryOpen = false;
                playMode.gameplay.beams.clear();
                playMode.gameplay.flashes.clear();
                playMode.gameplay.floatingTexts.clear();
                playMode.gameplay.hudBars.clear();
                playMode.gameplay.messageText.clear();
                playMode.gameplay.messageSecondsRemaining = 0.0F;
                playMode.inventoryWindowOpen = false;
                playMode.gameplay.projectiles.clear();
                playMode.gameplay.heldItemEntityName.clear();
                playMode.gameplay.playerOperatingCatapult = false;
                playMode.gameplay.enemyCatapultFireTimerSeconds = 8.0F;
                playMode.gameplay.gameOverMessage.clear();
                playMode.isPlaying = true;
                clearSelection(selection);

                scriptRuntime.initialize(
                    scene,
                    commandBus,
                    imguiInputSource,
                    [&console](const bool isError, const std::string& message)
                    {
                        logMessage(console, isError ? LogLevel::Error : LogLevel::Info, message);
                    },
                    [&playMode](
                        const glm::vec3& from, const glm::vec3& to, const float speed, const std::string& hitTag)
                    {
                        const glm::vec3 direction = to - from;
                        const float distance = glm::length(direction);
                        const glm::vec3 velocity =
                            distance > 0.0001F ? (direction / distance) * speed : glm::vec3(0.0F, 0.0F, speed);
                        playMode.gameplay.projectiles.push_back(GameplayState::Projectile{from, velocity, hitTag, 4.0F});
                    },
                    [&playMode]() { return !playMode.gameplay.heldItemEntityName.empty(); },
                    [&playMode]() { return playMode.gameplay.playerOperatingCatapult; },
                    [&playMode](const bool value) { playMode.gameplay.playerOperatingCatapult = value; },
                    [&playMode](
                        const glm::vec3& from, const glm::vec3& direction, const float speed,
                        const std::string& hitTag)
                    {
                        const float length = glm::length(direction);
                        const glm::vec3 velocity =
                            length > 0.0001F ? (direction / length) * speed : glm::vec3(0.0F, 0.0F, speed);
                        playMode.gameplay.projectiles.push_back(
                            GameplayState::Projectile{from, velocity, hitTag, 6.0F, true});
                    });
                // After initialize() (which resets it): backs self.inventory,
                // self.camera:getPitch/getAim and the beam/flash effects.
                scriptRuntime.setGameplayState(&playMode.gameplay);
                for (const SceneEntity& entity : scene.entities())
                {
                    for (const std::string& scriptPath : entity.scripts)
                    {
                        scriptRuntime.startScript(entity.id, scriptPath, projectRoot);
                    }
                }
            }
            ImGui::PopStyleColor(2);
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65F, 0.20F, 0.20F, 1.0F));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75F, 0.25F, 0.25F, 1.0F));
            if (ImGui::Button("Stop", ImVec2(playButtonWidth, 0.0F)))
            {
                scriptRuntime.shutdown();
                scene.replaceEntities(playMode.savedEntities);
                selection.multiSelectedIds = playMode.savedMultiSelectedIds;
                selection.shiftAnchorId.reset();
                syncPrimarySelection(selection);
                camera = playMode.savedCamera;
                playMode.isPlaying = false;
                playMode.isPaused = false;
                playMode.stepOneFrame = false;
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();
            if (playMode.isPaused)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75F, 0.55F, 0.10F, 1.0F));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85F, 0.65F, 0.15F, 1.0F));
                if (ImGui::Button("Resume", ImVec2(pauseButtonWidth, 0.0F)))
                {
                    playMode.isPaused = false;
                }
                ImGui::PopStyleColor(2);
            }
            else
            {
                if (ImGui::Button("Pause", ImVec2(pauseButtonWidth, 0.0F)))
                {
                    playMode.isPaused = true;
                }
            }

            ImGui::SameLine();
            if (!playMode.isPaused)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Step", ImVec2(stepButtonWidth, 0.0F)))
            {
                playMode.stepOneFrame = true;
            }
            if (!playMode.isPaused)
            {
                ImGui::EndDisabled();
            }

            ImGui::SameLine();
            ImGui::TextColored(
                ImVec4(1.0F, 0.6F, 0.1F, 1.0F),
                playMode.isPaused ? "PAUSED" : "PLAYING - changes will not be saved");
        }

        ImGui::EndMainMenuBar();
    }

    void drawAiSetupSettingsContent(
        AISetupState& state,
        const AIProviderClient& providerClient,
        ConsoleState& console)
    {
        ImGui::TextUnformatted("Provider and model configuration");
        ImGui::Separator();

        if (ImGui::BeginCombo("Provider", providers[static_cast<std::size_t>(state.selectedProvider)].displayName))
        {
            for (int index = 0; index < static_cast<int>(providers.size()); ++index)
            {
                const bool selected = state.selectedProvider == index;
                if (ImGui::Selectable(providers[static_cast<std::size_t>(index)].displayName, selected))
                {
                    selectProvider(state, index);
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::InputText("Endpoint", state.endpoint.data(), state.endpoint.size());
        ImGui::InputText("Model", state.model.data(), state.model.size());
        ImGui::Text("API key source: %s", providers[static_cast<std::size_t>(state.selectedProvider)].keySource);
        ImGui::TextUnformatted("Secrets: Game/AI/Providers.local.json");

        // The Calibrate / Test provider button used to flip a flag without
        // sending anything - the user saw "Configuration ready" while the
        // editor never asked the provider if it was reachable. Send a tiny
        // probe (a minimal completion request) and surface the real result.
        if (ImGui::Button("Calibrate / Test provider"))
        {
            const std::string providerId =
                providers[static_cast<std::size_t>(state.selectedProvider)].id;
            const AIProviderResponse probe = providerClient.send(
                providerId,
                AIProviderRequest{"Reply with the single word: pong", ""});
            state.tested = true;
            state.testSucceeded = probe.success;
            // Redact: don't echo back the full body (which can echo the prompt
            // or carry provider-side debug info). Just report status + truncated
            // error.
            std::string message;
            if (probe.success)
            {
                message = "OK (HTTP " + std::to_string(probe.statusCode) + ")";
            }
            else
            {
                const std::string err = probe.error.empty() ? "no error message" : probe.error;
                const std::size_t maxLen = 200;
                message = "HTTP " + std::to_string(probe.statusCode) + " - " +
                    (err.size() > maxLen ? err.substr(0, maxLen) + "..." : err);
            }
            state.testMessage = message;
            state.calibrated = probe.success;
            state.setupChanged = false;
            logMessage(
                console,
                probe.success ? LogLevel::Info : LogLevel::Error,
                "Provider test (" + providerId + "): " + message);
        }
        if (state.tested)
        {
            ImGui::SameLine();
            ImGui::TextColored(
                state.testSucceeded ? ImVec4(0.35F, 0.85F, 0.45F, 1.0F)
                                    : ImVec4(0.95F, 0.35F, 0.35F, 1.0F),
                "%s",
                state.testMessage.c_str());
        }
        else if (state.setupChanged)
        {
            ImGui::TextDisabled("Changes are local to this editor session until the AI client is connected.");
        }

        ImGui::TextWrapped(
            "NVIDIA is primary. Agnes-AI is available as fallback. "
            "The provider client will validate keys and tool capabilities before sending prompts.");
    }

    void drawProjectSettingsContent(const std::filesystem::path& projectRoot, const EditorScene& scene)
    {
        ImGui::TextUnformatted("Project");
        ImGui::Separator();
        ImGui::Text("Root: %s", projectRoot.string().c_str());
        ImGui::Text("Objects in scene: %d", static_cast<int>(scene.entities().size()));
        ImGui::TextDisabled(
            "More project settings (name, target platform, build options) will land here as they're added.");
    }

    void applyDefaultDarkTheme()
    {
        ImGui::StyleColorsDark();
    }

    // Palette sampled directly from Game/Branding/logo.jpg: bright orange
    // flame (#F98703), ember red-orange, silver-grey "G", near-black background.
    void applyGameForgerTheme()
    {
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        const ImVec4 orange(0.976F, 0.529F, 0.012F, 1.0F);
        const ImVec4 orangeHover(1.0F, 0.62F, 0.15F, 1.0F);
        const ImVec4 orangeActive(0.80F, 0.42F, 0.02F, 1.0F);
        const ImVec4 silver(0.55F, 0.58F, 0.61F, 1.0F);
        const ImVec4 charcoalBg(0.02F, 0.02F, 0.024F, 1.0F);
        const ImVec4 charcoalPanel(0.08F, 0.085F, 0.095F, 1.0F);
        const ImVec4 charcoalFrame(0.11F, 0.115F, 0.13F, 1.0F);

        colors[ImGuiCol_Text] = ImVec4(0.92F, 0.92F, 0.90F, 1.0F);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.50F, 0.52F, 0.55F, 1.0F);
        colors[ImGuiCol_WindowBg] = charcoalBg;
        colors[ImGuiCol_ChildBg] = ImVec4(0.0F, 0.0F, 0.0F, 0.0F);
        colors[ImGuiCol_PopupBg] = ImVec4(0.03F, 0.03F, 0.035F, 0.98F);
        colors[ImGuiCol_Border] = ImVec4(silver.x, silver.y, silver.z, 0.25F);
        colors[ImGuiCol_FrameBg] = charcoalFrame;
        colors[ImGuiCol_FrameBgHovered] = ImVec4(orange.x, orange.y, orange.z, 0.25F);
        colors[ImGuiCol_FrameBgActive] = ImVec4(orange.x, orange.y, orange.z, 0.40F);
        colors[ImGuiCol_TitleBg] = ImVec4(0.02F, 0.02F, 0.024F, 1.0F);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.14F, 0.08F, 0.02F, 1.0F);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.02F, 0.02F, 0.024F, 0.75F);
        colors[ImGuiCol_MenuBarBg] = charcoalPanel;
        colors[ImGuiCol_ScrollbarBg] = charcoalBg;
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(silver.x, silver.y, silver.z, 0.35F);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(silver.x, silver.y, silver.z, 0.55F);
        colors[ImGuiCol_ScrollbarGrabActive] = orange;
        colors[ImGuiCol_CheckMark] = orange;
        colors[ImGuiCol_SliderGrab] = orange;
        colors[ImGuiCol_SliderGrabActive] = orangeHover;
        colors[ImGuiCol_Button] = charcoalFrame;
        colors[ImGuiCol_ButtonHovered] = ImVec4(orange.x, orange.y, orange.z, 0.55F);
        colors[ImGuiCol_ButtonActive] = orangeActive;
        colors[ImGuiCol_Header] = ImVec4(orange.x, orange.y, orange.z, 0.30F);
        colors[ImGuiCol_HeaderHovered] = ImVec4(orange.x, orange.y, orange.z, 0.55F);
        colors[ImGuiCol_HeaderActive] = ImVec4(orange.x, orange.y, orange.z, 0.75F);
        colors[ImGuiCol_Separator] = ImVec4(silver.x, silver.y, silver.z, 0.25F);
        colors[ImGuiCol_SeparatorHovered] = orange;
        colors[ImGuiCol_SeparatorActive] = orangeHover;
        colors[ImGuiCol_ResizeGrip] = ImVec4(silver.x, silver.y, silver.z, 0.25F);
        colors[ImGuiCol_ResizeGripHovered] = orangeHover;
        colors[ImGuiCol_ResizeGripActive] = orangeActive;
        colors[ImGuiCol_Tab] = charcoalPanel;
        colors[ImGuiCol_TabHovered] = ImVec4(orange.x, orange.y, orange.z, 0.55F);
        colors[ImGuiCol_TabActive] = ImVec4(0.20F, 0.11F, 0.02F, 1.0F);
        colors[ImGuiCol_TabUnfocused] = charcoalPanel;
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.13F, 0.08F, 0.02F, 1.0F);
        colors[ImGuiCol_DockingPreview] = ImVec4(orange.x, orange.y, orange.z, 0.55F);
        colors[ImGuiCol_DockingEmptyBg] = charcoalBg;
        colors[ImGuiCol_PlotLines] = silver;
        colors[ImGuiCol_PlotLinesHovered] = orangeHover;
        colors[ImGuiCol_PlotHistogram] = orange;
        colors[ImGuiCol_PlotHistogramHovered] = orangeHover;
        colors[ImGuiCol_TextSelectedBg] = ImVec4(orange.x, orange.y, orange.z, 0.35F);
        colors[ImGuiCol_DragDropTarget] = orange;
        colors[ImGuiCol_NavHighlight] = orange;
    }

    void applyTheme(const ThemeChoice theme)
    {
        if (theme == ThemeChoice::GameForgerAI)
        {
            applyGameForgerTheme();
        }
        else
        {
            applyDefaultDarkTheme();
        }
    }

    void drawAppearanceSettingsContent(AppearanceState& appearance)
    {
        ImGui::TextUnformatted("Appearance");
        ImGui::Separator();
        constexpr std::array<const char*, 2> themeNames{"Default Dark", "GameForgerAI"};
        int themeIndex = appearance.theme == ThemeChoice::GameForgerAI ? 1 : 0;
        if (ImGui::Combo("Theme", &themeIndex, themeNames.data(), static_cast<int>(themeNames.size())))
        {
            appearance.theme = themeIndex == 1 ? ThemeChoice::GameForgerAI : ThemeChoice::DefaultDark;
            applyTheme(appearance.theme);
        }
        ImGui::TextDisabled("GameForgerAI uses the logo's orange / charcoal / silver palette.");
    }

    void drawLanguageSettingsContent(LanguageState& language)
    {
        ImGui::TextUnformatted("Language");
        ImGui::Separator();
        constexpr std::array<const char*, 3> languageNames{
            "English", "Francais (coming soon)", "Mandarin (coming soon)"};
        int languageIndex = language.selectedLanguage;
        if (ImGui::Combo("Language", &languageIndex, languageNames.data(), static_cast<int>(languageNames.size())))
        {
            if (languageIndex == 0)
            {
                language.selectedLanguage = languageIndex;
            }
            // French/Mandarin are placeholders only - no translated strings exist yet.
        }
        ImGui::TextDisabled("French and Mandarin language packs are planned; only English is implemented today.");
    }

    void drawSettingsWindow(
        SettingsState& settings,
        AISetupState& aiSetup,
        AppearanceState& appearance,
        LanguageState& language,
        const std::filesystem::path& projectRoot,
        const EditorScene& scene,
        const AIProviderClient& providerClient,
        ConsoleState& console)
    {
        if (!settings.open)
        {
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(640.0F, 420.0F), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Settings", &settings.open))
        {
            ImGui::End();
            return;
        }

        constexpr std::array<const char*, 4> categories{"AI Setup", "Project", "Appearance", "Language"};
        ImGui::BeginChild("SettingsCategoryList", ImVec2(160.0F, 0.0F), true);
        for (int index = 0; index < static_cast<int>(categories.size()); ++index)
        {
            if (ImGui::Selectable(
                    categories[static_cast<std::size_t>(index)], settings.selectedCategory == index))
            {
                settings.selectedCategory = index;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("SettingsContent", ImVec2(0.0F, 0.0F));
        switch (settings.selectedCategory)
        {
            case 0: drawAiSetupSettingsContent(aiSetup, providerClient, console); break;
            case 1: drawProjectSettingsContent(projectRoot, scene); break;
            case 2: drawAppearanceSettingsContent(appearance); break;
            case 3: drawLanguageSettingsContent(language); break;
            default: break;
        }
        ImGui::EndChild();

        ImGui::End();
    }

    void drawConsolePanel(ConsoleState& console)
    {
        ImGui::Begin("Console");

        if (ImGui::Button("Clear"))
        {
            console.entries.clear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Info", &console.showInfo);
        ImGui::SameLine();
        ImGui::Checkbox("Warnings", &console.showWarnings);
        ImGui::SameLine();
        ImGui::Checkbox("Errors", &console.showErrors);
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &console.autoScroll);
        ImGui::Separator();

        ImGui::BeginChild("ConsoleScroll", ImVec2(0.0F, 0.0F), false, ImGuiWindowFlags_HorizontalScrollbar);
        for (const LogEntry& entry : console.entries)
        {
            if (entry.level == LogLevel::Info && !console.showInfo)
            {
                continue;
            }
            if (entry.level == LogLevel::Warning && !console.showWarnings)
            {
                continue;
            }
            if (entry.level == LogLevel::Error && !console.showErrors)
            {
                continue;
            }
            const ImVec4 color = entry.level == LogLevel::Error
                ? ImVec4(0.95F, 0.35F, 0.35F, 1.0F)
                : entry.level == LogLevel::Warning
                    ? ImVec4(0.95F, 0.75F, 0.25F, 1.0F)
                    : ImVec4(0.75F, 0.75F, 0.75F, 1.0F);
            ImGui::TextColored(color, "[%s] %s", entry.timestamp.c_str(), entry.message.c_str());
        }
        if (console.autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5.0F)
        {
            ImGui::SetScrollHereY(1.0F);
        }
        ImGui::EndChild();

        ImGui::End();
    }

    // Real Game/ file listing (Unity calls this the "Project" window), rather
    // than the previous hardcoded bullet list.
    void drawProjectBrowser(
        ProjectBrowserState& browser, const std::filesystem::path& projectRoot, ScriptEditorState& scriptEditor)
    {
        const std::filesystem::path gameRoot = projectRoot / "Game";
        if (browser.currentDirectory.empty())
        {
            browser.currentDirectory = gameRoot;
        }

        ImGui::Begin("Project");

        std::error_code relativeError;
        const std::filesystem::path relativePath =
            std::filesystem::relative(browser.currentDirectory, projectRoot, relativeError);
        ImGui::TextDisabled("%s", relativeError ? "Game" : relativePath.generic_string().c_str());

        if (browser.currentDirectory != gameRoot)
        {
            if (ImGui::Button(".. (Up)"))
            {
                browser.currentDirectory = browser.currentDirectory.parent_path();
            }
        }
        ImGui::Separator();

        std::error_code existsError;
        if (!std::filesystem::exists(browser.currentDirectory, existsError) || existsError)
        {
            ImGui::TextDisabled("Folder not found.");
            ImGui::End();
            return;
        }

        std::vector<std::filesystem::directory_entry> directories;
        std::vector<std::filesystem::directory_entry> files;
        std::error_code iterateError;
        for (const std::filesystem::directory_entry& entry :
            std::filesystem::directory_iterator(browser.currentDirectory, iterateError))
        {
            if (entry.is_directory())
            {
                directories.push_back(entry);
            }
            else
            {
                files.push_back(entry);
            }
        }
        const auto byName = [](const std::filesystem::directory_entry& a, const std::filesystem::directory_entry& b)
        {
            return a.path().filename() < b.path().filename();
        };
        std::sort(directories.begin(), directories.end(), byName);
        std::sort(files.begin(), files.end(), byName);

        for (const std::filesystem::directory_entry& directory : directories)
        {
            const std::string name = directory.path().filename().string();
            if (ImGui::Selectable(("[Folder] " + name).c_str()))
            {
                browser.currentDirectory = directory.path();
            }
        }
        for (const std::filesystem::directory_entry& file : files)
        {
            const std::string name = file.path().filename().string();
            const bool isSelected = browser.selectedFile == file.path();
            if (ImGui::Selectable(name.c_str(), isSelected))
            {
                browser.selectedFile = file.path();
                if (file.path().extension() == ".lua")
                {
                    std::error_code toProjectRootError;
                    const std::filesystem::path relativeToRoot =
                        std::filesystem::relative(file.path(), projectRoot, toProjectRootError);
                    std::ifstream scriptFile(file.path(), std::ios::binary);
                    const std::string content(
                        (std::istreambuf_iterator<char>(scriptFile)), std::istreambuf_iterator<char>());
                    scriptEditor.buffer.fill('\0');
                    const std::size_t copyLength = std::min(content.size(), scriptEditor.buffer.size() - 1);
                    std::memcpy(scriptEditor.buffer.data(), content.data(), copyLength);
                    scriptEditor.scriptPath = toProjectRootError ? name : relativeToRoot.generic_string();
                    scriptEditor.status.clear();
                    scriptEditor.requestOpen = true;
                }
            }
        }
        if (directories.empty() && files.empty())
        {
            ImGui::TextDisabled("(empty)");
        }

        ImGui::End();
    }

    glm::vec3 yawForward(const float yawDegrees)
    {
        const float yawRadians = glm::radians(yawDegrees);
        return glm::vec3(std::sin(yawRadians), 0.0F, std::cos(yawRadians));
    }

    // yawPitchForward/cameraLookingAt/scriptedPlayCamera moved to Engine
    // (GameForger/Runtime/GameCamera.hpp) so GameForgerRuntime can frame
    // the identical scripted Game/Play camera standalone - see the `using
    // gameforger::editor::...` declarations above.

    // Full pitch+yaw+roll forward direction for an entity's rotationEuler -
    // unlike yawForward/yawPitchForward (yaw-only, or yaw+pitch), the free-
    // roaming Cine Camera needs roll too. Reuses the same
    // ImGuizmo::RecomposeMatrixFromComponents decomposition Transform.cpp
    // already uses for entity transforms, extracting the local +Z column
    // (this project's forward axis - see the camera icon mesh comment in
    // ViewportRenderer.cpp).
    glm::vec3 entityForwardVector(const glm::vec3& rotationEuler)
    {
        const float translation[3] = {0.0F, 0.0F, 0.0F};
        const float rotation[3] = {rotationEuler.x, rotationEuler.y, rotationEuler.z};
        const float scale[3] = {1.0F, 1.0F, 1.0F};
        float matrix[16];
        ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale, matrix);
        const glm::mat4 rotationMatrix = glm::make_mat4(matrix);
        return glm::normalize(glm::vec3(rotationMatrix[2]));
    }

    // Nearest isPickupItem entity to `origin` (the player's own position,
    // not the camera's aim ray) within `horizontalRange` (X/Z distance,
    // ignoring height) and `heightTolerance` (Y difference) - or nullptr.
    // Deliberately proximity-based, not aim-based: an earlier version
    // required the camera's own eye/forward ray to pass within a tight
    // lateral distance of the item, which looked correct on paper but
    // failed in practice for a small, floor-level object (e.g. a potion
    // scaled to 0.11) sitting below a standing FPS camera's roughly-level
    // eye line - the player would have to deliberately look straight down
    // at it, which nobody does when just walking up and pressing E. A
    // plain "nearby, roughly your height" check matches what "walk up and
    // press E" actually implies. Used both to show a "[E] Pick up X" hint
    // every frame and to perform the actual pickup (only on E press)
    // against the exact same criteria, so the hint never lies about what
    // E would actually do.

    // Projects a world position to a pixel coordinate inside the Game
    // view's own on-screen rect (`imagePos`/`imageSize`, as established in
    // drawGameViewPanel - NOT the full app window) using that view's own
    // last-rendered camera matrices, for drawing 2D overlays (detection
    // icons, projectile markers) that track a moving 3D point without any
    // new mesh/billboard rendering machinery - same technique as the
    // existing crosshair/pickup-hint overlays, just driven by an arbitrary
    // world position instead of the screen center. Returns false (does
    // not write outScreenPosition) if the point is behind the camera,
    // where a naive projection would otherwise show it mirrored onscreen.
    bool worldToScreen(
        const glm::vec3& worldPosition,
        const glm::mat4& view,
        const glm::mat4& projection,
        const ImVec2& imagePos,
        const ImVec2& imageSize,
        ImVec2& outScreenPosition)
    {
        const glm::vec4 clip = projection * view * glm::vec4(worldPosition, 1.0F);
        if (clip.w <= 0.0001F)
        {
            return false;
        }
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        outScreenPosition = ImVec2(
            imagePos.x + (ndc.x * 0.5F + 0.5F) * imageSize.x,
            imagePos.y + (1.0F - (ndc.y * 0.5F + 0.5F)) * imageSize.y);
        return true;
    }

    // Red starburst "detected!" icon (see the reference image the user
    // attached - a jagged spiky burst with radiating lines) drawn above an
    // enemy_ai.lua/ranged_attacker.lua entity once it reports self.detected
    // = 1 - see drawGameViewPanel's detection-icon pass. Pure ImDrawList
    // primitives (no image asset), gently pulsing via `timeSeconds` so it
    // reads as an alert rather than a static decal.
    void drawDetectionIcon(ImDrawList* drawList, const ImVec2& center, const float timeSeconds)
    {
        const float pulse = 0.85F + 0.15F * std::sin(timeSeconds * 8.0F);
        const float outerRadius = 16.0F * pulse;
        const float innerRadius = 7.0F * pulse;
        constexpr int kSpikeCount = 8;
        constexpr ImU32 kRed = IM_COL32(235, 40, 40, 255);
        for (int index = 0; index < kSpikeCount; ++index)
        {
            const float angle = (static_cast<float>(index) / static_cast<float>(kSpikeCount)) * 2.0F * 3.14159265F;
            const float spikeLength = (index % 2 == 0) ? outerRadius : outerRadius * 0.6F;
            const ImVec2 tip(center.x + std::cos(angle) * spikeLength, center.y + std::sin(angle) * spikeLength);
            const ImVec2 baseA(
                center.x + std::cos(angle - 0.18F) * innerRadius, center.y + std::sin(angle - 0.18F) * innerRadius);
            const ImVec2 baseB(
                center.x + std::cos(angle + 0.18F) * innerRadius, center.y + std::sin(angle + 0.18F) * innerRadius);
            drawList->AddTriangleFilled(baseA, tip, baseB, kRed);
        }
        drawList->AddCircleFilled(center, innerRadius, kRed);
        drawList->AddCircle(center, innerRadius, IM_COL32(255, 140, 140, 255), 0, 2.0F);
    }

    const SceneEntity* findPickupCandidate(
        const EditorScene& scene,
        const glm::vec3& origin,
        const float horizontalRange,
        const float heightTolerance,
        const int excludeId)
    {
        const SceneEntity* best = nullptr;
        float bestDistanceSquared = horizontalRange * horizontalRange;
        for (const SceneEntity& other : scene.entities())
        {
            if (other.id == excludeId || !other.isPickupItem)
            {
                continue;
            }
            if (std::abs(other.position.y - origin.y) > heightTolerance)
            {
                continue;
            }
            const float deltaX = other.position.x - origin.x;
            const float deltaZ = other.position.z - origin.z;
            const float distanceSquared = deltaX * deltaX + deltaZ * deltaZ;
            if (distanceSquared > bestDistanceSquared)
            {
                continue;
            }
            best = &other;
            bestDistanceSquared = distanceSquared;
        }
        return best;
    }

    // The enemy catapult's arm/yaw mount are normally children (parented
    // under the base for visual following, see the Inspector's Catapult
    // section) - applyParentConstraints (GameplayLoop.cpp) recomputes a
    // parented entity's WORLD rotationEuler from its own localRotationEuler
    // + the parent's world transform unconditionally, every frame, so these
    // read the LOCAL rotation instead whenever the entity actually has a
    // parent (falling back to plain world rotation for an unparented one).
    // The player catapult's equivalent WRITE side of this same logic now
    // lives in ScriptRuntime.cpp as self.world:setEntityRotation, called
    // from catapult_controller.lua instead of engine-side C++.
    float entityOwnYawDegrees(const SceneEntity& entity)
    {
        return entity.parentName.empty() ? entity.rotationEuler.y : entity.localRotationEuler.y;
    }

    float entityOwnPitchAxisDegrees(const SceneEntity& entity)
    {
        return entity.parentName.empty() ? entity.rotationEuler.x : entity.localRotationEuler.x;
    }

    // Samples a gravity-arced trajectory starting at `origin` with initial
    // `velocity`, using the exact same semi-implicit Euler step (gravity
    // applied before position) and gravity constant (kProjectileGravity,
    // GameplayLoop.hpp) as the real fired projectile in tickProjectiles -
    // so the preview line drawn while aiming always matches where a fired
    // boulder will actually go. Stops early at world y<=0 (ground plane) -
    // a known simplification since there's no real terrain height sampling
    // yet, see the plan's "coordination note".
    std::vector<glm::vec3> simulateProjectileArc(
        const glm::vec3& origin, const glm::vec3& velocity, const float stepSeconds, const int maxSteps)
    {
        std::vector<glm::vec3> points;
        points.reserve(static_cast<std::size_t>(maxSteps) + 1);
        glm::vec3 position = origin;
        glm::vec3 currentVelocity = velocity;
        points.push_back(position);
        for (int step = 0; step < maxSteps; ++step)
        {
            currentVelocity.y += gameforger::editor::kProjectileGravity * stepSeconds;
            position += currentVelocity * stepSeconds;
            points.push_back(position);
            if (position.y <= 0.0F)
            {
                break;
            }
        }
        return points;
    }

    // "As the player would see it": a separate, non-navigable render of the
    // current scene, distinct from the editable Viewport (Unity's Scene view).
    // It renders live whether or not Play is active, so animated/Play-mode
    // motion shows up here in real time too - it just isn't editable. During
    // Play, if a script has claimed the camera (self.camera:setMode(...)),
    // this follows that entity instead of using the fixed default framing,
    // and holding right mouse over the image looks around (see PlayModeState).
    void drawGameViewPanel(
        gameforger::editor::ViewportRenderer& renderer,
        const EditorCameraState& gameCamera,
        EditorScene& scene,
        AICommandBus& commandBus,
        PlayModeState& playMode,
        const ScriptRuntime& scriptRuntime,
        const std::filesystem::path& projectRoot,
        GLFWwindow* window)
    {
        ImGui::Begin("Game");

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const bool ready = available.x > 0.0F && available.y > 0.0F &&
            renderer.resize(static_cast<int>(available.x), static_cast<int>(available.y));
        if (ready)
        {
            const SceneEntity* followedEntity = (playMode.isPlaying && scriptRuntime.hasActiveCamera())
                ? scene.findEntity(scriptRuntime.activeCameraEntityId())
                : nullptr;
            // Hoisted out of the if/else below (not just computed inline)
            // because the pickup-interaction check further down also needs
            // the followed camera's actual eye/forward - keeping one
            // computation avoids drift between what you SEE and what you
            // can interact with.
            GameCameraState scripted{};
            if (followedEntity != nullptr)
            {
                scripted = scriptedPlayCamera(
                    *followedEntity,
                    scriptRuntime.activeCameraMode(),
                    playMode.gameCameraLookYawDegrees,
                    playMode.gameCameraLookPitchDegrees);
                renderer.setCamera(scripted.yaw, scripted.pitch, scripted.distance, scripted.target);
            }
            else
            {
                renderer.setCamera(gameCamera.yaw, gameCamera.pitch, gameCamera.distance, gameCamera.target);
            }
            // Standard FPS convention: don't render the player's own body
            // mesh from inside its own head. Automatic, tied to whichever
            // mode the script itself reported via self.camera:setMode(...)
            // - both fps_controller.lua and third_person_controller.lua
            // already call that, so neither needs any change for this to
            // work; third-person keeps showing the model since you're
            // viewing it from outside there.
            const int excludeEntityId = (followedEntity != nullptr && scriptRuntime.activeCameraMode() == "fps")
                ? followedEntity->id
                : -1;
            renderer.render(scene.entities(), {}, projectRoot, excludeEntityId);
            const ImVec2 imagePos = ImGui::GetCursorScreenPos();
            ImGui::Image(
                static_cast<ImTextureID>(renderer.texture()), available, ImVec2(0.0F, 1.0F), ImVec2(1.0F, 0.0F));

            // Source of truth is the followed entity's OWN authored
            // preference (Inspector's Camera Rig > Lock Cursor), not a
            // transient UI toggle - so it's correctly released the instant
            // Play stops, the scripted camera goes away, or Esc suppresses
            // it for this session, not just when this panel happens to be
            // hovered. Also released (regardless of Esc/lockCursor) while
            // the inventory grid is open, so the mouse is free to drag
            // icons - and re-applies automatically the moment it closes,
            // since this is recomputed fresh every frame rather than a
            // one-shot toggle.
            const bool wantsCursorLock = playMode.isPlaying && followedEntity != nullptr &&
                followedEntity->cameraRig.lockCursor && !playMode.cursorLockSuppressed &&
                !playMode.inventoryWindowOpen;
            if (wantsCursorLock && !playMode.gameplay.cursorCurrentlyLocked)
            {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                playMode.gameplay.cursorCurrentlyLocked = true;
            }
            else if (!wantsCursorLock && playMode.gameplay.cursorCurrentlyLocked)
            {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                playMode.gameplay.cursorCurrentlyLocked = false;
            }
            // Esc releases the lock without stopping Play (the universal
            // "let me out" key) and without touching the entity's own
            // saved preference - just suppresses it for the rest of this
            // Play session (reset when Play next starts).
            if (playMode.gameplay.cursorCurrentlyLocked && ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                playMode.cursorLockSuppressed = true;
            }

            if (followedEntity != nullptr)
            {
                if (playMode.gameplay.cursorCurrentlyLocked)
                {
                    // Continuous look every frame while locked - no button
                    // needs to be held, unlike the gesture-based drag below.
                    playMode.gameCameraDragging = false;
                }
                else
                {
                    const bool hovered = ImGui::IsItemHovered();
                    if (!playMode.gameCameraDragging && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                    {
                        playMode.gameCameraDragging = true;
                    }
                    if (playMode.gameCameraDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
                    {
                        playMode.gameCameraDragging = false;
                    }
                }

                // While operating a catapult, catapult_controller.lua reads
                // mouse delta for its own aim - skip the normal camera-look
                // update entirely so the two don't fight over the same
                // MouseDelta this frame.
                if ((playMode.gameplay.cursorCurrentlyLocked || playMode.gameCameraDragging) &&
                    !playMode.gameplay.playerOperatingCatapult)
                {
                    constexpr float mouseLookSensitivity = 0.15F;
                    const ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
                    playMode.gameCameraLookPitchDegrees = glm::clamp(
                        playMode.gameCameraLookPitchDegrees - mouseDelta.y * mouseLookSensitivity, -80.0F, 80.0F);
                    if (scriptRuntime.activeCameraMode() == "third_person")
                    {
                        playMode.gameCameraLookYawDegrees -= mouseDelta.x * mouseLookSensitivity;
                    }
                    else
                    {
                        const glm::vec3 newRotation(
                            followedEntity->rotationEuler.x,
                            followedEntity->rotationEuler.y - mouseDelta.x * mouseLookSensitivity,
                            followedEntity->rotationEuler.z);
                        commandBus.execute(
                            SetPropertyCommand{followedEntity->name, "Transform", "rotation", newRotation});
                    }
                }
            }

            // Catapult aiming/firing is now entirely catapult_controller.lua
            // (self.input reads F/E/T/R + mouse directly, self.world:
            // setEntityRotation/fireGravityProjectile/setOperatingCatapult
            // do the rest) - no engine-side E/mouse/R handling left here.
            // The trajectory preview line the script can't draw itself is
            // rendered separately below, sourced from the script's own
            // exposed self.aiming/aim_yaw/aim_pitch/... fields.

            // Weapon hold (F) - engine-side, same proximity check as the
            // pickup block below but reparents into the player's hand
            // (main.cpp's applyParentConstraints keeps it following) rather
            // than deleting the entity into the inventory grid. Not gated
            // behind inventory_system.lua - a separate mechanic, a scene
            // can have holdable weapons with no inventory system at all.
            if (followedEntity != nullptr && playMode.gameplay.gameOverMessage.empty())
            {
                constexpr float kHoldInteractRange = 4.0F;
                constexpr float kHoldHeightTolerance = 2.5F;
                if (playMode.gameplay.heldItemEntityName.empty())
                {
                    const SceneEntity* holdCandidate = findPickupCandidate(
                        scene, followedEntity->position, kHoldInteractRange, kHoldHeightTolerance,
                        followedEntity->id);
                    if (holdCandidate != nullptr)
                    {
                        const std::string hint = "[F] Hold " + holdCandidate->pickupItem.itemName;
                        const ImVec2 hintSize = ImGui::CalcTextSize(hint.c_str());
                        ImGui::GetWindowDrawList()->AddText(
                            ImVec2(
                                imagePos.x + available.x * 0.5F - hintSize.x * 0.5F,
                                imagePos.y + available.y * 0.5F + 40.0F),
                            IM_COL32(255, 255, 255, 235),
                            hint.c_str());
                        if (ImGui::IsKeyPressed(ImGuiKey_F))
                        {
                            const std::string itemName = holdCandidate->name;
                            commandBus.execute(
                                SetPropertyCommand{itemName, "Parent", "parentName", followedEntity->name});
                            commandBus.execute(SetPropertyCommand{
                                itemName, "Parent", "localPosition", glm::vec3(0.35F, -0.2F, 0.55F)});
                            commandBus.execute(SetPropertyCommand{
                                itemName, "Parent", "localRotation", glm::vec3(0.0F, 0.0F, -80.0F)});
                            commandBus.execute(SetPropertyCommand{itemName, "Collider", "enabled", false});
                            playMode.gameplay.heldItemEntityName = itemName;
                        }
                    }
                }
                else
                {
                    const char* hint = "[F] Drop weapon";
                    const ImVec2 hintSize = ImGui::CalcTextSize(hint);
                    ImGui::GetWindowDrawList()->AddText(
                        ImVec2(
                            imagePos.x + available.x * 0.5F - hintSize.x * 0.5F,
                            imagePos.y + available.y * 0.5F + 40.0F),
                        IM_COL32(255, 255, 255, 235),
                        hint);
                    if (ImGui::IsKeyPressed(ImGuiKey_F))
                    {
                        const std::string itemName = playMode.gameplay.heldItemEntityName;
                        const glm::vec3 dropForward = yawPitchForward(followedEntity->rotationEuler.y, 0.0F);
                        commandBus.execute(SetPropertyCommand{itemName, "Parent", "parentName", std::string()});
                        commandBus.execute(SetPropertyCommand{
                            itemName, "Transform", "position",
                            followedEntity->position + dropForward * 1.5F});
                        commandBus.execute(SetPropertyCommand{
                            itemName, "Transform", "rotation",
                            glm::vec3(0.0F, followedEntity->rotationEuler.y, 0.0F)});
                        commandBus.execute(SetPropertyCommand{itemName, "Collider", "enabled", true});
                        playMode.gameplay.heldItemEntityName.clear();
                    }
                }
            }

            // Pickup + inventory - active while the followed entity has an
            // inventory-providing script (inventory_system.lua, or any script
            // that sets self.inventory_enabled = true, e.g. fps_player.lua).
            // E picks up the nearest "Is Pickup Item" or items.lua object in
            // range (see findPickupItemCandidate, GameplayLoop.cpp - shared
            // with GameForgerRuntime); I toggles the inventory grid.
            // pickup_range/pickup_height_tolerance are read live off
            // whichever script sets them.
            const bool providesInventory =
                followedEntity != nullptr && entityProvidesInventory(*followedEntity, scriptRuntime);
            std::string interactionHint;
            if (providesInventory && playMode.gameplay.gameOverMessage.empty())
            {
                float pickupRange = 4.0F;
                float pickupHeightTolerance = 2.5F;
                for (const std::string& scriptPath : followedEntity->scripts)
                {
                    const float range =
                        scriptRuntime.getScriptNumberField(followedEntity->id, scriptPath, "pickup_range", -1.0F);
                    if (range >= 0.0F)
                    {
                        pickupRange = range;
                        pickupHeightTolerance = scriptRuntime.getScriptNumberField(
                            followedEntity->id, scriptPath, "pickup_height_tolerance", pickupHeightTolerance);
                        break;
                    }
                }
                const SceneEntity* candidate = findPickupItemCandidate(
                    scene, followedEntity->position, pickupRange, pickupHeightTolerance, followedEntity->id);
                if (candidate != nullptr && !playMode.inventoryWindowOpen)
                {
                    const std::string itemLabel = pickupDisplayName(*candidate, scriptRuntime);
                    interactionHint = "[E] Pick up " + itemLabel;
                    if (ImGui::IsKeyPressed(ImGuiKey_E, false))
                    {
                        const int followedId = followedEntity->id;
                        const bool picked = pickUpItem(scene, commandBus, playMode.gameplay, scriptRuntime, *candidate);
                        playMode.gameplay.messageText = picked ? "Picked up " + itemLabel : "Inventory full";
                        playMode.gameplay.messageSecondsRemaining = 1.6F;
                        // A legacy pickup is deleted, which can move other
                        // entities in the scene's storage - re-find ours.
                        followedEntity = scene.findEntity(followedId);
                    }
                }

                // I toggles the inventory grid - works with or without the
                // cursor lock feature on.
                if (ImGui::IsKeyPressed(ImGuiKey_I, false))
                {
                    playMode.inventoryWindowOpen = !playMode.inventoryWindowOpen;
                }
            }

            // Aim reticle at the viewport center while the cursor is
            // captured - in FPS this is where the character looks; in
            // third-person it's where the orbiting camera's angle points -
            // also what the pickup check above aims from. Only drawn while
            // locked (the real OS cursor is already the pointer otherwise,
            // a crosshair would just be redundant/confusing).
            if (playMode.gameplay.cursorCurrentlyLocked)
            {
                const ImVec2 center(imagePos.x + available.x * 0.5F, imagePos.y + available.y * 0.5F);
                constexpr float crosshairHalfSize = 8.0F;
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddLine(
                    ImVec2(center.x - crosshairHalfSize, center.y),
                    ImVec2(center.x + crosshairHalfSize, center.y),
                    IM_COL32(255, 255, 255, 220),
                    2.0F);
                drawList->AddLine(
                    ImVec2(center.x, center.y - crosshairHalfSize),
                    ImVec2(center.x, center.y + crosshairHalfSize),
                    IM_COL32(255, 255, 255, 220),
                    2.0F);
            }

            // Detection icon - red starburst above any enemy_ai.lua/
            // ranged_attacker.lua entity currently reporting self.detected
            // = 1 (see those scripts + ScriptRuntime::getScriptNumberField).
            // Checked for every entity in the scene, not just the followed
            // one - an enemy elsewhere in the world shows its own alert
            // regardless of which entity currently has the camera.
            if (playMode.isPlaying)
            {
                ImDrawList* overlayDrawList = ImGui::GetWindowDrawList();
                const float timeSeconds = static_cast<float>(ImGui::GetTime());
                constexpr std::array<const char*, 3> kDetectionScriptPaths{
                    "Game/Scripts/enemy_ai.lua", "Game/Scripts/ranged_attacker.lua", gameforger::editor::fpsdemo::kEnemy};
                for (const SceneEntity& other : scene.entities())
                {
                    if (other.scripts.empty())
                    {
                        continue;
                    }
                    bool detected = false;
                    for (const char* scriptPath : kDetectionScriptPaths)
                    {
                        if (std::find(other.scripts.begin(), other.scripts.end(), scriptPath) ==
                            other.scripts.end())
                        {
                            continue;
                        }
                        if (scriptRuntime.getScriptNumberField(other.id, scriptPath, "detected", 0.0F) >= 0.5F)
                        {
                            detected = true;
                            break;
                        }
                    }
                    if (!detected)
                    {
                        continue;
                    }
                    const glm::vec3 iconWorldPosition =
                        other.position + glm::vec3(0.0F, other.scale.y * 1.2F + 0.6F, 0.0F);
                    ImVec2 screenPosition{};
                    if (worldToScreen(
                            iconWorldPosition, renderer.view(), renderer.projection(), imagePos, available,
                            screenPosition))
                    {
                        drawDetectionIcon(overlayDrawList, screenPosition, timeSeconds);
                    }
                }

                // Engine-managed projectiles (GameplayState::Projectile /
                // tickProjectiles) - a small bright dot with a short
                // trailing streak along its travel direction, screen-space
                // just like the detection icon above (no new 3D mesh/
                // billboard rendering machinery needed for this).
                for (const GameplayState::Projectile& projectile : playMode.gameplay.projectiles)
                {
                    ImVec2 headScreen{};
                    if (!worldToScreen(
                            projectile.position, renderer.view(), renderer.projection(), imagePos, available,
                            headScreen))
                    {
                        continue;
                    }
                    ImVec2 tailScreen = headScreen;
                    const glm::vec3 tailWorld = projectile.position - glm::normalize(projectile.velocity) * 0.4F;
                    worldToScreen(
                        tailWorld, renderer.view(), renderer.projection(), imagePos, available, tailScreen);
                    overlayDrawList->AddLine(tailScreen, headScreen, IM_COL32(255, 200, 60, 255), 3.0F);
                    overlayDrawList->AddCircleFilled(headScreen, 4.0F, IM_COL32(255, 230, 120, 255));
                }

                // Castle HP bars - same screen-space overlay technique as
                // the detection icon above (worldToScreen + ImDrawList),
                // anchored above every isCastle entity regardless of tag.
                for (const SceneEntity& other : scene.entities())
                {
                    if (!other.isCastle)
                    {
                        continue;
                    }
                    const glm::vec3 barWorldPosition =
                        other.position + glm::vec3(0.0F, other.scale.y + 1.0F, 0.0F);
                    ImVec2 barScreen{};
                    if (!worldToScreen(
                            barWorldPosition, renderer.view(), renderer.projection(), imagePos, available,
                            barScreen))
                    {
                        continue;
                    }
                    constexpr float kBarWidth = 90.0F;
                    constexpr float kBarHeight = 10.0F;
                    const ImVec2 barMin(barScreen.x - kBarWidth * 0.5F, barScreen.y - kBarHeight * 0.5F);
                    const ImVec2 barMax(barScreen.x + kBarWidth * 0.5F, barScreen.y + kBarHeight * 0.5F);
                    overlayDrawList->AddRectFilled(barMin, barMax, IM_COL32(40, 40, 40, 200));
                    const float hpFraction = other.castle.maxHp > 0.0F
                        ? glm::clamp(other.castle.hp / other.castle.maxHp, 0.0F, 1.0F)
                        : 0.0F;
                    overlayDrawList->AddRectFilled(
                        barMin, ImVec2(barMin.x + kBarWidth * hpFraction, barMax.y), IM_COL32(220, 50, 50, 230));
                    overlayDrawList->AddRect(barMin, barMax, IM_COL32(0, 0, 0, 200));
                    const std::string barLabel = other.name;
                    const ImVec2 labelSize = ImGui::CalcTextSize(barLabel.c_str());
                    overlayDrawList->AddText(
                        ImVec2(barScreen.x - labelSize.x * 0.5F, barMin.y - labelSize.y - 2.0F),
                        IM_COL32(255, 255, 255, 230), barLabel.c_str());
                }

                // Catapult trajectory preview - catapult_controller.lua
                // owns all the actual aim/fire input handling and state
                // now, but a script has no way to draw anything, so this
                // reads its exposed self.aiming/aim_yaw/aim_pitch/
                // launch_speed/arm_pos_x,y,z fields (same getScriptNumberField
                // mechanism the detection-icon pass above already uses for
                // enemy_ai.lua's self.detected) and draws the arc for it,
                // same technique/math as the engine-side version this
                // replaced.
                constexpr const char* kCatapultControllerScriptPath = "Game/Scripts/catapult_controller.lua";
                for (const SceneEntity& other : scene.entities())
                {
                    if (std::find(
                            other.scripts.begin(), other.scripts.end(), kCatapultControllerScriptPath) ==
                        other.scripts.end())
                    {
                        continue;
                    }
                    const float aiming = scriptRuntime.getScriptNumberField(
                        other.id, kCatapultControllerScriptPath, "aiming", 0.0F);
                    if (aiming < 0.5F)
                    {
                        continue;
                    }
                    const float aimYaw = scriptRuntime.getScriptNumberField(
                        other.id, kCatapultControllerScriptPath, "aim_yaw", 0.0F);
                    const float aimPitch = scriptRuntime.getScriptNumberField(
                        other.id, kCatapultControllerScriptPath, "aim_pitch", 20.0F);
                    const float launchSpeed = scriptRuntime.getScriptNumberField(
                        other.id, kCatapultControllerScriptPath, "launch_speed", 20.0F);
                    const glm::vec3 launchOrigin(
                        scriptRuntime.getScriptNumberField(
                            other.id, kCatapultControllerScriptPath, "arm_pos_x", other.position.x),
                        scriptRuntime.getScriptNumberField(
                            other.id, kCatapultControllerScriptPath, "arm_pos_y", other.position.y),
                        scriptRuntime.getScriptNumberField(
                            other.id, kCatapultControllerScriptPath, "arm_pos_z", other.position.z));

                    const glm::vec3 launchDirection = yawPitchForward(aimYaw, aimPitch);
                    const glm::vec3 launchVelocity = launchDirection * launchSpeed;
                    const std::vector<glm::vec3> arcPoints =
                        simulateProjectileArc(launchOrigin, launchVelocity, 0.05F, 60);
                    ImVec2 previousScreen{};
                    bool havePrevious = false;
                    for (const glm::vec3& point : arcPoints)
                    {
                        ImVec2 screen{};
                        if (!worldToScreen(
                                point, renderer.view(), renderer.projection(), imagePos, available, screen))
                        {
                            havePrevious = false;
                            continue;
                        }
                        if (havePrevious)
                        {
                            overlayDrawList->AddLine(previousScreen, screen, IM_COL32(255, 220, 100, 230), 2.0F);
                        }
                        previousScreen = screen;
                        havePrevious = true;
                    }

                    const char* aimHint = "Mouse: aim   Hold [T]: power   [R] Fire   [E] Cancel";
                    const ImVec2 aimHintSize = ImGui::CalcTextSize(aimHint);
                    overlayDrawList->AddText(
                        ImVec2(
                            imagePos.x + available.x * 0.5F - aimHintSize.x * 0.5F,
                            imagePos.y + available.y * 0.5F + 20.0F),
                        IM_COL32(255, 255, 255, 235), aimHint);
                }
            }

            // ---- Gameplay HUD: effects, damage numbers, health/XP bars, hotbar,
            // weapon line, messages - the same layout GameForgerRuntime draws
            // (drawGameplayHud, GameplayHud.cpp).
            if (playMode.isPlaying)
            {
                ImGuiHudCanvas canvas(ImGui::GetWindowDrawList(), projectRoot);
                HudFrame frame;
                frame.origin = glm::vec2(imagePos.x, imagePos.y);
                frame.size = glm::vec2(available.x, available.y);
                frame.view = renderer.view();
                frame.projection = renderer.projection();
                frame.timeSeconds = static_cast<float>(ImGui::GetTime());
                frame.player = followedEntity;
                frame.showHotbar = providesInventory && followedEntity != nullptr;
                frame.interactionHint = interactionHint;
                frame.drawCrosshair = false; // the Game view draws its own while the cursor is locked
                drawGameplayHud(canvas, frame, scene, scriptRuntime, playMode.gameplay);
            }

            // Win/lose banner - set once by tickProjectiles (a castle's hp
            // reached 0) or the enemy auto-fire tick (main loop). Drawn on
            // top of everything else in the panel; further E/R/F input and
            // the enemy timer are gated on this being empty, so nothing
            // else needs to check isPlaying separately once it's set.
            if (!playMode.gameplay.gameOverMessage.empty())
            {
                ImDrawList* bannerDrawList = ImGui::GetWindowDrawList();
                const ImVec2 bannerSize = ImGui::CalcTextSize(playMode.gameplay.gameOverMessage.c_str());
                const ImVec2 bannerPos(
                    imagePos.x + available.x * 0.5F - bannerSize.x * 0.5F,
                    imagePos.y + available.y * 0.5F - bannerSize.y * 0.5F - 60.0F);
                bannerDrawList->AddRectFilled(
                    ImVec2(bannerPos.x - 16.0F, bannerPos.y - 10.0F),
                    ImVec2(bannerPos.x + bannerSize.x + 16.0F, bannerPos.y + bannerSize.y + 10.0F),
                    IM_COL32(0, 0, 0, 180));
                bannerDrawList->AddText(
                    bannerPos, IM_COL32(255, 235, 120, 255), playMode.gameplay.gameOverMessage.c_str());
            }

            if (!playMode.isPlaying)
            {
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(imagePos.x + 8.0F, imagePos.y + 8.0F),
                    IM_COL32(255, 200, 60, 255),
                    "Preview - press Play to playtest with animations running");
            }
            else if (followedEntity != nullptr)
            {
                const std::string lookHint =
                    playMode.gameplay.cursorCurrentlyLocked ? "cursor locked, Esc to release" : "hold Right Mouse to look around";
                const std::string label = followedEntity->name + " camera (" + scriptRuntime.activeCameraMode() +
                    ") - press C to switch, " + lookHint;
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(imagePos.x + 8.0F, imagePos.y + 8.0F), IM_COL32(120, 210, 255, 255), label.c_str());
            }
        }
        else
        {
            ImGui::TextUnformatted("Game view unavailable.");
        }

        ImGui::End();
    }

    // I-key inventory grid (see the E/I key handling in drawGameViewPanel,
    // which toggles inventoryWindowOpen). kInventorySlotCount fixed slots,
    // the top row being the hotbar (keys 1-8). Click a slot to equip/select
    // it; drag a slot onto another to swap (empty slots too); drag a slot
    // out of the window to drop one back into the world in front of the
    // player; right-click for Equip / Drop one / Drop all. Pickup/drop logic
    // itself is shared with GameForgerRuntime (GameplayLoop.cpp).
    void drawInventoryWindow(
        PlayModeState& playMode,
        EditorScene& scene,
        AICommandBus& commandBus,
        const SceneEntity* followedEntity,
        const std::filesystem::path& projectRoot)
    {
        if (!playMode.inventoryWindowOpen)
        {
            return;
        }
        auto& slots = playMode.gameplay.inventoryItems;
        if (static_cast<int>(slots.size()) != kInventorySlotCount)
        {
            ensureInventorySlots(playMode.gameplay);
        }

        constexpr int kColumns = kHotbarSlotCount;
        constexpr float kSlotSize = 56.0F;
        constexpr float kSlotSpacing = 6.0F;
        const float gridWidth = kColumns * kSlotSize + (kColumns - 1) * kSlotSpacing;
        ImGui::SetNextWindowSize(ImVec2(gridWidth + 32.0F, 0.0F), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.93F);
        bool stillOpen = true;
        ImGui::Begin("Inventory (I to close)", &stillOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
        if (!stillOpen)
        {
            playMode.inventoryWindowOpen = false;
        }

        int dropSlot = -1;
        int dropCount = 0;
        const auto dropFromSlot = [&](const int slotIndex, const int count)
        {
            dropSlot = slotIndex;
            dropCount = count;
        };

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const int slotCount = static_cast<int>(slots.size());
        for (int index = 0; index < slotCount; ++index)
        {
            if (index == 0)
            {
                ImGui::TextDisabled("Hotbar (1-8)");
            }
            else if (index == kHotbarSlotCount)
            {
                ImGui::Dummy(ImVec2(0.0F, 4.0F));
                ImGui::TextDisabled("Backpack");
            }
            if (index % kColumns != 0)
            {
                ImGui::SameLine(0.0F, kSlotSpacing);
            }

            GameplayState::InventoryItem& item = slots[static_cast<std::size_t>(index)];
            const bool selected = playMode.gameplay.selectedSlot == index;
            ImGui::PushID(index);
            const ImVec2 slotMin = ImGui::GetCursorScreenPos();
            const ImVec2 slotMax(slotMin.x + kSlotSize, slotMin.y + kSlotSize);
            if (ImGui::InvisibleButton("##slot", ImVec2(kSlotSize, kSlotSize)))
            {
                playMode.gameplay.selectedSlot = index;
            }
            const bool hovered = ImGui::IsItemHovered();

            drawList->AddRectFilled(
                slotMin, slotMax,
                index < kHotbarSlotCount ? IM_COL32(30, 26, 20, 255) : IM_COL32(22, 22, 26, 255), 5.0F);
            if (!item.empty())
            {
                const GLuint icon = item.iconPath.empty() ? 0 : ensureIconTextureGpu(item.iconPath, projectRoot);
                if (icon != 0)
                {
                    drawList->AddImage(
                        static_cast<ImTextureID>(icon), ImVec2(slotMin.x + 5.0F, slotMin.y + 5.0F),
                        ImVec2(slotMax.x - 5.0F, slotMax.y - 5.0F));
                }
                else
                {
                    const std::string initials = item.itemName.substr(0, 3);
                    drawList->AddText(
                        ImVec2(slotMin.x + 8.0F, slotMin.y + kSlotSize * 0.5F - 7.0F), IM_COL32(230, 230, 230, 255),
                        initials.c_str());
                }
                if (item.count > 1)
                {
                    const std::string countLabel = "x" + std::to_string(item.count);
                    const ImVec2 countSize = ImGui::CalcTextSize(countLabel.c_str());
                    drawList->AddText(
                        ImVec2(slotMax.x - countSize.x - 4.0F, slotMax.y - countSize.y - 2.0F),
                        IM_COL32(255, 255, 255, 255), countLabel.c_str());
                }
            }
            if (index < kHotbarSlotCount)
            {
                drawList->AddText(
                    ImVec2(slotMin.x + 4.0F, slotMin.y + 2.0F), IM_COL32(200, 190, 170, 170),
                    std::to_string(index + 1).c_str());
            }
            drawList->AddRect(
                slotMin, slotMax,
                selected ? IM_COL32(249, 135, 3, 255)
                         : (hovered ? IM_COL32(220, 220, 230, 200) : IM_COL32(120, 120, 130, 140)),
                5.0F, 0, selected ? 3.0F : 1.0F);

            if (!item.empty())
            {
                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload("INVENTORY_SLOT", &index, sizeof(int));
                    const GLuint icon = item.iconPath.empty() ? 0 : ensureIconTextureGpu(item.iconPath, projectRoot);
                    if (icon != 0)
                    {
                        ImGui::Image(static_cast<ImTextureID>(icon), ImVec2(32.0F, 32.0F));
                        ImGui::SameLine();
                    }
                    ImGui::TextUnformatted(item.itemName.c_str());
                    ImGui::EndDragDropSource();
                }
                if (hovered && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(item.itemName.c_str());
                    if (!item.itemType.empty())
                    {
                        ImGui::TextDisabled("%s%s%s", item.itemType.c_str(), item.weapon.empty() ? "" : " - ",
                            item.weapon.c_str());
                    }
                    if (item.count > 1)
                    {
                        ImGui::TextDisabled("x%d", item.count);
                    }
                    ImGui::TextDisabled("Click: equip   Drag out: drop   Right-click: more");
                    ImGui::EndTooltip();
                }
                if (ImGui::BeginPopupContextItem("SlotMenu"))
                {
                    ImGui::TextUnformatted(item.itemName.c_str());
                    ImGui::Separator();
                    if (ImGui::MenuItem("Equip / Select"))
                    {
                        playMode.gameplay.selectedSlot = index;
                    }
                    if (ImGui::MenuItem("Drop one"))
                    {
                        dropFromSlot(index, 1);
                    }
                    if (item.count > 1 && ImGui::MenuItem("Drop all"))
                    {
                        dropFromSlot(index, item.count);
                    }
                    ImGui::EndPopup();
                }
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("INVENTORY_SLOT"))
                {
                    const int sourceIndex = *static_cast<const int*>(payload->Data);
                    if (sourceIndex != index && sourceIndex >= 0 && sourceIndex < slotCount)
                    {
                        std::swap(slots[static_cast<std::size_t>(sourceIndex)], slots[static_cast<std::size_t>(index)]);
                        // Keep the equipped item equipped when it moves.
                        if (playMode.gameplay.selectedSlot == sourceIndex)
                        {
                            playMode.gameplay.selectedSlot = index;
                        }
                        else if (playMode.gameplay.selectedSlot == index)
                        {
                            playMode.gameplay.selectedSlot = sourceIndex;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();
        }

        // Details of the selected slot.
        ImGui::Separator();
        const int selectedSlot = playMode.gameplay.selectedSlot;
        if (selectedSlot >= 0 && selectedSlot < slotCount && !slots[static_cast<std::size_t>(selectedSlot)].empty())
        {
            const GameplayState::InventoryItem& item = slots[static_cast<std::size_t>(selectedSlot)];
            ImGui::Text("Equipped: %s", item.itemName.c_str());
            if (!item.weapon.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", item.weapon.c_str());
            }
        }
        else
        {
            ImGui::TextDisabled("Equipped: nothing (fists)");
        }
        ImGui::TextDisabled("E picks up items near you.");

        // ImGui has no direct "dropped over nothing" event, so this infers
        // it: our payload is still active, the mouse just released, and the
        // release point is outside this window (a release inside was
        // already handled as a slot swap above).
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            const ImGuiPayload* payload = ImGui::GetDragDropPayload();
            if (payload != nullptr && payload->IsDataType("INVENTORY_SLOT"))
            {
                const ImVec2 windowMin = ImGui::GetWindowPos();
                const ImVec2 windowMax(windowMin.x + ImGui::GetWindowSize().x, windowMin.y + ImGui::GetWindowSize().y);
                const ImVec2 mousePos = ImGui::GetIO().MousePos;
                const bool insideWindow = mousePos.x >= windowMin.x && mousePos.x <= windowMax.x &&
                    mousePos.y >= windowMin.y && mousePos.y <= windowMax.y;
                if (!insideWindow)
                {
                    dropFromSlot(*static_cast<const int*>(payload->Data), 1);
                }
            }
        }

        ImGui::End();

        if (dropSlot >= 0 && followedEntity != nullptr)
        {
            // Copy what we need first - dropping can create entities, which
            // may move the followed entity in the scene's storage.
            const glm::vec3 forward = yawPitchForward(followedEntity->rotationEuler.y, 0.0F);
            const glm::vec3 basePosition = followedEntity->position;
            const float yaw = followedEntity->rotationEuler.y;
            for (int dropped = 0; dropped < dropCount; ++dropped)
            {
                // Spread several dropped items out a little.
                const glm::vec3 side(forward.z, 0.0F, -forward.x);
                const glm::vec3 dropPosition = basePosition + forward * 2.0F +
                    side * (static_cast<float>(dropped % 5) - 2.0F) * 0.45F + glm::vec3(0.0F, 0.6F, 0.0F);
                if (!dropInventoryItem(scene, commandBus, playMode.gameplay, dropSlot, dropPosition, yaw))
                {
                    break;
                }
            }
        }
    }

    // Defined below, alongside applyPose/tickPlayModeAnimations - forward
    // declared here since drawCineCameraPreviewPanel (which calls it) is
    // defined earlier in the file for proximity to drawGameViewPanel.
    void tickCineAnimatedEntities(
        EditorScene& scene, AICommandBus& commandBus, float elapsedTime, int excludeEntityId);

    // A separate camera from the player/Game view camera, for cutscenes only.
    // Only appears once StoryboardState::windowOpen is set (from the Toolbox
    // or the Storyboard panel) and closes via its own window X. Drives the
    // render off whichever path is "active": the currently-selected Cine
    // Camera entity's own EntityAnimation when previewing it live, or the
    // current movie shot's captured cameraPath when playingMovie - both are
    // just an EntityAnimation, ticked the same way via sampleAnimation.
    void drawCineCameraPreviewPanel(
        gameforger::editor::ViewportRenderer& renderer,
        EditorScene& scene,
        AICommandBus& commandBus,
        SelectionState& selection,
        StoryboardState& storyboard,
        const std::filesystem::path& projectRoot,
        const float deltaTime)
    {
        if (!storyboard.windowOpen)
        {
            return;
        }

        bool stillOpen = true;
        ImGui::Begin("Cine Camera Preview", &stillOpen);
        if (!stillOpen)
        {
            storyboard.windowOpen = false;
            storyboard.isPlaying = false;
            storyboard.playingMovie = false;
            storyboard.movieShotIndex = -1;
            ImGui::End();
            return;
        }

        const SceneEntity* liveCamera = selection.selectedEntityId.has_value()
            ? scene.findEntity(*selection.selectedEntityId)
            : nullptr;
        const bool liveCameraValid = liveCamera != nullptr && liveCamera->isCineCamera;

        const bool soloShotValid =
            storyboard.previewShotIndex >= 0 && storyboard.previewShotIndex < static_cast<int>(storyboard.shots.size());

        const EntityAnimation* activePath = nullptr;
        int excludeEntityId = -1;
        std::string label;
        if (storyboard.playingMovie && storyboard.movieShotIndex >= 0 &&
            storyboard.movieShotIndex < static_cast<int>(storyboard.shots.size()))
        {
            const CineShot& shot = storyboard.shots[static_cast<std::size_t>(storyboard.movieShotIndex)];
            activePath = &shot.cameraPath;
            label = "Shot " + std::to_string(storyboard.movieShotIndex + 1) + "/" +
                std::to_string(storyboard.shots.size()) + ": " + shot.name;
        }
        else if (soloShotValid)
        {
            const CineShot& shot = storyboard.shots[static_cast<std::size_t>(storyboard.previewShotIndex)];
            activePath = &shot.cameraPath;
            label = "Shot " + std::to_string(storyboard.previewShotIndex + 1) + ": " + shot.name;
        }
        else if (liveCameraValid)
        {
            activePath = &liveCamera->animation;
            excludeEntityId = liveCamera->id;
            label = liveCamera->name;
        }

        if (storyboard.playingMovie)
        {
            if (ImGui::Button("Stop Movie"))
            {
                storyboard.playingMovie = false;
                storyboard.isPlaying = false;
                storyboard.movieShotIndex = -1;
            }
        }
        else if (!soloShotValid && !liveCameraValid)
        {
            ImGui::TextDisabled(
                "Select a Cine Camera entity in the Hierarchy, or click Preview on a shot in the Storyboard panel.");
        }
        else if (!storyboard.isPlaying)
        {
            if (ImGui::Button("Play"))
            {
                storyboard.isPlaying = true;
                storyboard.playTime = 0.0F;
            }
        }
        else if (ImGui::Button("Stop"))
        {
            storyboard.isPlaying = false;
            storyboard.previewShotIndex = -1;
        }
        if (!label.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s (%.1fs)", label.c_str(), storyboard.playTime);
        }

        const bool playing = storyboard.isPlaying || storyboard.playingMovie;
        if (playing && activePath != nullptr && !activePath->keyframes.empty())
        {
            storyboard.playTime += deltaTime;
            const float duration = activePath->keyframes.back().time;
            // Only non-looping paths are treated as having a finish line - a
            // shot deliberately left looping just plays forever in preview
            // and never auto-advances the movie past it.
            if (!activePath->looping && storyboard.playTime > duration)
            {
                if (storyboard.playingMovie)
                {
                    const int nextShotIndex = storyboard.movieShotIndex + 1;
                    if (nextShotIndex < static_cast<int>(storyboard.shots.size()))
                    {
                        storyboard.movieShotIndex = nextShotIndex;
                    }
                    else
                    {
                        storyboard.playingMovie = false;
                        storyboard.movieShotIndex = -1;
                    }
                }
                else
                {
                    storyboard.isPlaying = false;
                }
                storyboard.playTime = 0.0F;
            }
        }

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const bool ready = available.x > 0.0F && available.y > 0.0F &&
            renderer.resize(static_cast<int>(available.x), static_cast<int>(available.y));
        if (ready)
        {
            if (activePath != nullptr && !activePath->keyframes.empty())
            {
                const AnimatedPose pose = sampleAnimation(*activePath, storyboard.playTime);
                const glm::vec3 forward = entityForwardVector(pose.rotationEuler);
                const GameCameraState shotCamera = cameraLookingAt(pose.position, pose.position + forward * 10.0F);
                renderer.setCamera(shotCamera.yaw, shotCamera.pitch, shotCamera.distance, shotCamera.target);
                if (playing)
                {
                    tickCineAnimatedEntities(scene, commandBus, storyboard.playTime, excludeEntityId);
                }
            }
            else if (liveCameraValid)
            {
                const glm::vec3 forward = entityForwardVector(liveCamera->rotationEuler);
                const GameCameraState shotCamera =
                    cameraLookingAt(liveCamera->position, liveCamera->position + forward * 10.0F);
                renderer.setCamera(shotCamera.yaw, shotCamera.pitch, shotCamera.distance, shotCamera.target);
            }
            renderer.render(scene.entities(), {}, projectRoot);
            ImGui::Image(
                static_cast<ImTextureID>(renderer.texture()), available, ImVec2(0.0F, 1.0F), ImVec2(1.0F, 0.0F));
        }
        else
        {
            ImGui::TextUnformatted("Cine Camera preview unavailable.");
        }

        ImGui::End();
    }

    // Numbered, reorderable list of captured CineShots and their assembly
    // into "the movie" - a shot is just a snapshot of a Cine Camera entity's
    // own EntityAnimation at the moment it's captured (in-memory only this
    // pass, not persisted with the scene). Playback happens in the Cine
    // Camera Preview panel, which this opens as needed.
    void drawStoryboardPanel(EditorScene& scene, SelectionState& selection, StoryboardState& storyboard)
    {
        ImGui::Begin("Storyboard");

        const SceneEntity* liveCamera = selection.selectedEntityId.has_value()
            ? scene.findEntity(*selection.selectedEntityId)
            : nullptr;
        const bool liveCameraValid = liveCamera != nullptr && liveCamera->isCineCamera;

        if (!liveCameraValid)
        {
            ImGui::TextDisabled("Select a Cine Camera entity (with a recorded Animation path) to capture a shot.");
        }
        else if (liveCamera->animation.keyframes.empty())
        {
            ImGui::TextDisabled(
                "'%s' has no recorded path yet - record keyframes on the Animation tab first.", liveCamera->name.c_str());
        }
        else if (ImGui::Button("Save Current Path as New Shot"))
        {
            CineShot shot;
            shot.name = "Shot " + std::to_string(storyboard.shots.size() + 1);
            shot.cameraPath = liveCamera->animation;
            storyboard.shots.push_back(std::move(shot));
        }

        ImGui::Separator();

        if (storyboard.shots.empty())
        {
            ImGui::TextDisabled("(no shots captured yet)");
        }

        int shotIndexToRemove = -1;
        for (int index = 0; index < static_cast<int>(storyboard.shots.size()); ++index)
        {
            CineShot& shot = storyboard.shots[static_cast<std::size_t>(index)];
            ImGui::PushID(index);
            ImGui::Text("%d.", index + 1);
            ImGui::SameLine();

            std::array<char, 128> nameBuffer{};
            std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", shot.name.c_str());
            ImGui::SetNextItemWidth(150.0F);
            if (ImGui::InputText("##ShotName", nameBuffer.data(), nameBuffer.size()))
            {
                shot.name = nameBuffer.data();
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("Up") && index > 0)
            {
                std::swap(storyboard.shots[static_cast<std::size_t>(index)],
                    storyboard.shots[static_cast<std::size_t>(index - 1)]);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Down") && index + 1 < static_cast<int>(storyboard.shots.size()))
            {
                std::swap(storyboard.shots[static_cast<std::size_t>(index)],
                    storyboard.shots[static_cast<std::size_t>(index + 1)]);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Preview"))
            {
                storyboard.windowOpen = true;
                storyboard.playingMovie = false;
                storyboard.movieShotIndex = -1;
                storyboard.previewShotIndex = index;
                storyboard.isPlaying = true;
                storyboard.playTime = 0.0F;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove"))
            {
                shotIndexToRemove = index;
            }
            ImGui::PopID();
        }
        if (shotIndexToRemove >= 0)
        {
            storyboard.shots.erase(storyboard.shots.begin() + shotIndexToRemove);
            if (storyboard.previewShotIndex == shotIndexToRemove)
            {
                storyboard.previewShotIndex = -1;
            }
        }

        ImGui::Separator();
        const bool canPlayMovie = !storyboard.shots.empty();
        if (!canPlayMovie)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Play Movie", ImVec2(-1.0F, 0.0F)))
        {
            storyboard.windowOpen = true;
            storyboard.previewShotIndex = -1;
            storyboard.playingMovie = true;
            storyboard.movieShotIndex = 0;
            storyboard.isPlaying = false;
            storyboard.playTime = 0.0F;
        }
        if (!canPlayMovie)
        {
            ImGui::EndDisabled();
        }
        ImGui::TextDisabled("Plays every shot above in order in the Cine Camera Preview window.");

        ImGui::End();
    }

    std::optional<AIEditorCommand> parseEditorPrompt(const std::string& prompt)
    {
        std::istringstream input(prompt);
        std::string operation;
        input >> operation;
        if (operation == "create_entity")
        {
            std::string name;
            input >> name;
            if (!name.empty())
            {
                return CreateEntityCommand{name};
            }
        }
        else if (operation == "set_position")
        {
            SetPositionCommand command;
            input >> command.entityName >> command.position.x >> command.position.y >> command.position.z;
            if (!command.entityName.empty() && input)
            {
                return command;
            }
        }
        else if (operation == "create_script")
        {
            CreateScriptCommand command;
            input >> command.path;
            std::getline(input >> std::ws, command.content);
            if (!command.path.empty() && !command.content.empty())
            {
                return command;
            }
        }
        return std::nullopt;
    }

    // A dockable list of creation tools - unlike the GameObject menu (which
    // spawns primitives instantly), tools here open a small creation dialog
    // since they need extra input (a font file, in Text Mesh's case). First
    // (and so far only) tool: Text Mesh, real extruded 3D glyph geometry.
    void drawToolboxPanel(
        const EditorCameraState& toolboxCamera,
        TextMeshToolState& textMeshTool,
        StoryboardState& storyboard,
        TerrainSculptState& terrainSculpt,
        EditorScene& scene,
        AICommandBus& commandBus,
        SelectionState& selection,
        ConsoleState& console,
        const std::filesystem::path& projectRoot,
        const HWND nativeWindowHandle)
    {
        ImGui::Begin("Toolbox");
        if (ImGui::Button("Text Mesh", ImVec2(-1.0F, 0.0F)))
        {
            textMeshTool.requestOpen = true;
            textMeshTool.status.clear();
        }
        ImGui::TextDisabled("3D extruded text - import a font, type a string, place it in the scene.");

        if (ImGui::Button("Cine Camera", ImVec2(-1.0F, 0.0F)))
        {
            CreateEntityCommand command;
            command.primitive = PrimitiveType::Cube; // ignored for rendering - draws as a wireframe icon.
            command.name = makeMenuEntityName(scene, "CineCamera");
            const AICommandResult result = commandBus.execute(command);
            logMessage(console, result.success ? LogLevel::Info : LogLevel::Error, result.message);
            if (result.success)
            {
                commandBus.execute(SetPropertyCommand{command.name, "CineCamera", "enabled", true});
                if (SceneEntity* created = scene.findEntityMutable(scene.entities().back().id))
                {
                    created->animation.enabled = true;
                    selectOnly(selection, created->id);
                }
                storyboard.windowOpen = true;
            }
        }
        ImGui::TextDisabled(
            "Cutscene camera - record its path on the Animation tab, then capture shots in the Storyboard "
            "panel and sequence them into a movie.");

        if (ImGui::Button("Terrain", ImVec2(-1.0F, 0.0F)))
        {
            CreateTerrainCommand command;
            command.name = makeMenuEntityName(scene, "Terrain");
            const AICommandResult result = commandBus.execute(command);
            logMessage(console, result.success ? LogLevel::Info : LogLevel::Error, result.message);
            if (result.success && !scene.entities().empty())
            {
                selectOnly(selection, scene.entities().back().id);
                terrainSculpt.active = true;
            }
        }
        ImGui::TextDisabled(
            "Real heightmap terrain - starts flat. Select it and use the Inspector's Terrain section to "
            "sculpt, import a heightmap, or generate one with Perlin noise.");

        if (ImGui::Button("FPS Player (Hands + Weapons)", ImVec2(-1.0F, 0.0F)))
        {
            createFpsPlayer(scene, commandBus, selection, console, toolboxCamera, false);
        }
        if (ImGui::Button("FPS Demo Arena", ImVec2(-1.0F, 0.0F)))
        {
            createFpsPlayer(scene, commandBus, selection, console, toolboxCamera, true);
        }
        ImGui::TextDisabled(
            "First-person player with animated hands and 8 weapons (fps_player.lua). The arena adds "
            "ground, weapon pickups (items.lua) and targets (health.lua).");
        ImGui::End();

        if (textMeshTool.requestOpen)
        {
            ImGui::OpenPopup("Create Text Mesh");
            textMeshTool.requestOpen = false;
        }
        if (ImGui::BeginPopupModal("Create Text Mesh", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text(
                "Font: %s",
                textMeshTool.fontRelativePath.empty() ? "(none selected)" : textMeshTool.fontRelativePath.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Browse..."))
            {
                const std::filesystem::path fontsDirectory = projectRoot / "Game" / "Fonts";
                if (const std::optional<std::filesystem::path> picked =
                        showOpenFontDialog(nativeWindowHandle, fontsDirectory))
                {
                    if (const std::optional<std::string> imported = importFontIntoProject(*picked, projectRoot))
                    {
                        textMeshTool.fontRelativePath = *imported;
                        textMeshTool.status.clear();
                    }
                    else
                    {
                        textMeshTool.status = "Could not import that font file.";
                        textMeshTool.statusSuccess = false;
                    }
                }
            }

            ImGui::InputTextMultiline(
                "Text", textMeshTool.content.data(), textMeshTool.content.size(), ImVec2(400.0F, 80.0F));
            ImGui::DragFloat("Size", &textMeshTool.fontSize, 0.02F, 0.05F, 20.0F);
            ImGui::DragFloat("Depth", &textMeshTool.depth, 0.01F, 0.01F, 5.0F);

            const bool canAdd = !textMeshTool.fontRelativePath.empty() && textMeshTool.content[0] != '\0';
            if (!canAdd)
            {
                ImGui::BeginDisabled();
            }
            // Deliberately doesn't auto-close on success, so several text
            // objects can be added in a row without reopening the dialog.
            if (ImGui::Button("Add"))
            {
                CreateTextMeshCommand command;
                command.content = textMeshTool.content.data();
                command.fontPath = textMeshTool.fontRelativePath;
                command.fontSize = textMeshTool.fontSize;
                command.depth = textMeshTool.depth;
                const AICommandResult result = commandBus.execute(command);
                textMeshTool.status = result.message;
                textMeshTool.statusSuccess = result.success;
                logMessage(console, result.success ? LogLevel::Info : LogLevel::Error, result.message);
                if (result.success && !scene.entities().empty())
                {
                    // CreateTextMeshCommand always appends the new entity at the end.
                    selectOnly(selection, scene.entities().back().id);
                }
            }
            if (!canAdd)
            {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (ImGui::Button("Close"))
            {
                ImGui::CloseCurrentPopup();
            }
            if (!textMeshTool.status.empty() && !textMeshTool.statusSuccess)
            {
                ImGui::TextColored(ImVec4(0.95F, 0.35F, 0.35F, 1.0F), "%s", textMeshTool.status.c_str());
            }
            ImGui::EndPopup();
        }
    }

    // Hierarchy panel drag-and-drop: dropping `draggedName`'s row onto
    // `newParentName`'s row parents it there. Solves for new local*
    // fields so the dragged entity's WORLD position/rotation/scale stay
    // exactly where they visually were the instant before the drop (the
    // standard Unity reparent-by-drag behavior) - without this, an
    // object with any local offset already set would visibly jump the
    // moment it's reparented, since local* is relative to whichever
    // parent it's currently under. Refuses (no-op) if newParentName is
    // `draggedName` itself or one of ITS OWN descendants (walks up
    // newParentName's own ancestor chain looking for draggedName) -
    // either would create a cycle; applyParentConstraints would degrade
    // gracefully either way, but rejecting at drop time gives a clearer
    // "nothing happened" instead of a silently-ignored cycle.
    void reparentEntityKeepingWorldTransform(
        EditorScene& scene, AICommandBus& commandBus, const std::string& draggedName, const std::string& newParentName)
    {
        const SceneEntity* dragged = scene.findEntity(draggedName);
        const SceneEntity* newParent = scene.findEntity(newParentName);
        if (dragged == nullptr || newParent == nullptr || dragged->id == newParent->id)
        {
            return;
        }
        for (const SceneEntity* ancestor = newParent; ancestor != nullptr && !ancestor->parentName.empty();
             ancestor = scene.findEntity(ancestor->parentName))
        {
            if (ancestor->parentName == draggedName)
            {
                return;
            }
        }

        const glm::mat4 parentWorld = composeEntityPivotFrame(*newParent);
        const glm::mat4 childWorldCurrent = composeEntityPivotFrame(*dragged);
        const glm::mat4 localMatrix = glm::inverse(parentWorld) * childWorldCurrent;

        float translation[3];
        float rotation[3];
        float scale[3];
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(localMatrix), translation, rotation, scale);

        commandBus.execute(SetPropertyCommand{draggedName, "Parent", "parentName", newParentName});
        commandBus.execute(SetPropertyCommand{
            draggedName, "Parent", "localPosition", glm::vec3(translation[0], translation[1], translation[2])});
        commandBus.execute(SetPropertyCommand{
            draggedName, "Parent", "localRotation", glm::vec3(rotation[0], rotation[1], rotation[2])});
        commandBus.execute(
            SetPropertyCommand{draggedName, "Parent", "localScale", glm::vec3(scale[0], scale[1], scale[2])});
    }

    // Named "Hierarchy" to match the usual editor convention (Unity, etc.): the
    // text tree of every entity and its parent-child relationships (real
    // ones now - SceneEntity::parentName - not just a flat list under
    // "World"). The 3D navigable viewport is the separate "Viewport" panel
    // (Unity calls that one "Scene view").
    void drawHierarchyPanel(
        EditorScene& scene,
        AICommandBus& commandBus,
        SelectionState& selection,
        RenameState& renameState,
        ConsoleState& console,
        const std::filesystem::path& projectRoot,
        const HWND nativeWindowHandle)
    {
        ImGui::Begin("Hierarchy");

        static std::array<char, 128> searchFilterBuffer{};
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputTextWithHint("##HierarchySearch", "Search entities by name/tag...", searchFilterBuffer.data(), searchFilterBuffer.size());
        ImGui::Separator();

        const bool hasFilter = searchFilterBuffer[0] != '\0';
        std::string filterLower = searchFilterBuffer.data();
        std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        auto entityMatchesFilter = [&](const SceneEntity& ent) -> bool
        {
            if (!hasFilter) return true;
            std::string nameLower = ent.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (nameLower.find(filterLower) != std::string::npos) return true;
            for (const auto& tag : ent.tags)
            {
                std::string tagLower = tag;
                std::transform(tagLower.begin(), tagLower.end(), tagLower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (tagLower.find(filterLower) != std::string::npos) return true;
            }
            return false;
        };

        // Any action that mutates scene.entities() (Duplicate/Delete/Add
        // Child) must NOT run synchronously from inside drawNode below -
        // entities_.push_back/erase can reallocate/shift the vector's
        // backing storage, which would leave the outer tree-traversal
        // loop's own iterators (and the `entity` reference drawNode is
        // currently running with) dangling for the rest of this frame -
        // a real crash, not a theoretical one. Only one action can fire
        // per frame (ImGui menu items only register one click/frame), so
        // a single deferred slot - not a queue - is enough; it's captured
        // by drawNode's existing [&] capture with no signature changes.
        std::function<void()> pendingHierarchyAction;

        std::vector<int> orderedIds;
        orderedIds.reserve(scene.entities().size());
        for (const SceneEntity& entity : scene.entities())
        {
            orderedIds.push_back(entity.id);
        }

        // Rebuilt fresh every frame (scenes here are small - dozens, not
        // thousands, of entities - so this is cheap) rather than cached,
        // so a rename/reparent/delete this same frame can never leave it
        // stale.
        std::unordered_map<std::string, std::vector<int>> childIdsByParentName;
        for (const SceneEntity& entity : scene.entities())
        {
            if (!entity.parentName.empty())
            {
                childIdsByParentName[entity.parentName].push_back(entity.id);
            }
        }

        const std::function<bool(const SceneEntity&)> subtreeMatchesFilter = [&](const SceneEntity& ent) -> bool
        {
            if (entityMatchesFilter(ent)) return true;
            const auto it = childIdsByParentName.find(ent.name);
            if (it != childIdsByParentName.end())
            {
                for (const int childId : it->second)
                {
                    if (const SceneEntity* child = scene.findEntity(childId))
                    {
                        if (subtreeMatchesFilter(*child)) return true;
                    }
                }
            }
            return false;
        };

        // Recursive - captures itself by reference so it can call itself
        // for each child, arbitrary depth. Deleting a parent does NOT
        // cascade-delete its children (a safer default than silently
        // destroying more than the user explicitly asked for) - an
        // orphaned child (parentName pointing at a now-missing entity)
        // still renders here, just promoted back to a top-level row, so
        // it's never silently invisible - see the top-level loop below.
        const std::function<void(const SceneEntity&)> drawNode = [&](const SceneEntity& entity)
        {
            if (hasFilter && !subtreeMatchesFilter(entity))
            {
                return;
            }

            ImGui::PushID(entity.id);
            const auto childIds = childIdsByParentName.find(entity.name);
            const bool hasChildren = childIds != childIdsByParentName.end() && !childIds->second.empty();

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (selection.contains(entity.id))
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            if (!hasChildren)
            {
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            }
            if (hasFilter)
            {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            }

            bool active = entity.active;
            if (ImGui::Checkbox("##Active", &active))
            {
                pendingHierarchyAction = [&scene, entityId = entity.id, active]()
                {
                    if (SceneEntity* mut = scene.findEntityMutable(entityId))
                    {
                        mut->active = active;
                    }
                };
            }
            ImGui::SameLine();
            if (!entity.active)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55F, 0.55F, 0.55F, 0.70F));
            }
            const bool open = ImGui::TreeNodeEx(entity.name.c_str(), flags);
            if (!entity.active)
            {
                ImGui::PopStyleColor();
            }
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            {
                const ImGuiIO& io = ImGui::GetIO();
                applySelectionClick(selection, orderedIds, entity.id, io.KeyCtrl, io.KeyShift);
            }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                ImGui::SetDragDropPayload("HIERARCHY_ENTITY_ID", &entity.id, sizeof(int));
                ImGui::TextUnformatted(entity.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY_ID"))
                {
                    const int draggedId = *static_cast<const int*>(payload->Data);
                    if (const SceneEntity* draggedEntity = scene.findEntity(draggedId))
                    {
                        reparentEntityKeepingWorldTransform(scene, commandBus, draggedEntity->name, entity.name);
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::BeginPopupContextItem("EntityContextMenu"))
            {
                if (!selection.contains(entity.id))
                {
                    selectOnly(selection, entity.id);
                }
                if (ImGui::MenuItem("Rename"))
                {
                    renameState.requestOpen = true;
                    renameState.entityId = entity.id;
                    std::snprintf(renameState.buffer.data(), renameState.buffer.size(), "%s", entity.name.c_str());
                }
                if (ImGui::MenuItem("Duplicate"))
                {
                    pendingHierarchyAction = [&commandBus, name = entity.name]()
                    { commandBus.execute(DuplicateEntityCommand{name}); };
                }
                if (ImGui::BeginMenu("Add Child"))
                {
                    if (ImGui::MenuItem("Cube"))
                    {
                        pendingHierarchyAction = [&scene, &commandBus, &selection, parentName = entity.name]()
                        { spawnChildPrimitive(scene, commandBus, selection, PrimitiveType::Cube, "Cube", parentName); };
                    }
                    if (ImGui::MenuItem("Sphere"))
                    {
                        pendingHierarchyAction = [&scene, &commandBus, &selection, parentName = entity.name]()
                        {
                            spawnChildPrimitive(
                                scene, commandBus, selection, PrimitiveType::Sphere, "Sphere", parentName);
                        };
                    }
                    if (ImGui::MenuItem("Cylinder"))
                    {
                        pendingHierarchyAction = [&scene, &commandBus, &selection, parentName = entity.name]()
                        {
                            spawnChildPrimitive(
                                scene, commandBus, selection, PrimitiveType::Cylinder, "Cylinder", parentName);
                        };
                    }
                    if (ImGui::MenuItem("Cone"))
                    {
                        pendingHierarchyAction = [&scene, &commandBus, &selection, parentName = entity.name]()
                        { spawnChildPrimitive(scene, commandBus, selection, PrimitiveType::Cone, "Cone", parentName); };
                    }
                    if (ImGui::MenuItem("Plane"))
                    {
                        pendingHierarchyAction = [&scene, &commandBus, &selection, parentName = entity.name]()
                        {
                            spawnChildPrimitive(scene, commandBus, selection, PrimitiveType::Plane, "Plane", parentName);
                        };
                    }
                    if (ImGui::MenuItem("Capsule"))
                    {
                        pendingHierarchyAction = [&scene, &commandBus, &selection, parentName = entity.name]()
                        {
                            spawnChildPrimitive(
                                scene, commandBus, selection, PrimitiveType::Capsule, "Capsule", parentName);
                        };
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Upload Model..."))
                    {
                        pendingHierarchyAction = [&scene, &commandBus, &selection, &console, &projectRoot,
                                                   nativeWindowHandle, parentName = entity.name]()
                        {
                            spawnChildModel(
                                scene, commandBus, selection, console, projectRoot, nativeWindowHandle, parentName);
                        };
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Delete"))
                {
                    pendingHierarchyAction = [&commandBus, &selection, name = entity.name, id = entity.id]()
                    {
                        commandBus.execute(DeleteEntityCommand{name});
                        const auto it =
                            std::find(selection.multiSelectedIds.begin(), selection.multiSelectedIds.end(), id);
                        if (it != selection.multiSelectedIds.end())
                        {
                            selection.multiSelectedIds.erase(it);
                            syncPrimarySelection(selection);
                        }
                    };
                }
                ImGui::EndPopup();
            }

            if (hasChildren && open)
            {
                for (const int childId : childIds->second)
                {
                    if (const SceneEntity* child = scene.findEntity(childId))
                    {
                        drawNode(*child);
                    }
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        };

        if (ImGui::TreeNodeEx("World", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // Dropping onto "World" itself unparents - no local-to-world
            // conversion needed here (unlike reparentEntityKeepingWorldTransform),
            // a top-level entity's position/rotation/scale already ARE
            // its world transform.
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY_ID"))
                {
                    const int draggedId = *static_cast<const int*>(payload->Data);
                    if (const SceneEntity* draggedEntity = scene.findEntity(draggedId))
                    {
                        commandBus.execute(
                            SetPropertyCommand{draggedEntity->name, "Parent", "parentName", std::string()});
                    }
                }
                ImGui::EndDragDropTarget();
            }
            for (const SceneEntity& entity : scene.entities())
            {
                const bool isOrphaned =
                    !entity.parentName.empty() && scene.findEntity(entity.parentName) == nullptr;
                if (entity.parentName.empty() || isOrphaned)
                {
                    drawNode(entity);
                }
            }
            ImGui::TreePop();
        }
        ImGui::End();

        // Now that the whole tree traversal is done for this frame (no more
        // `entity` references into scene.entities() are in play), it's safe
        // to actually run whatever Duplicate/Delete/Add Child action was
        // clicked above - see pendingHierarchyAction's own comment.
        if (pendingHierarchyAction)
        {
            pendingHierarchyAction();
            pendingHierarchyAction = nullptr;
        }

        if (renameState.requestOpen)
        {
            ImGui::OpenPopup("Rename Entity");
            renameState.requestOpen = false;
        }
        if (ImGui::BeginPopupModal("Rename Entity", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const bool confirmed = ImGui::InputText(
                "Name",
                renameState.buffer.data(),
                renameState.buffer.size(),
                ImGuiInputTextFlags_EnterReturnsTrue);
            const bool okPressed = ImGui::Button("OK");
            ImGui::SameLine();
            const bool cancelPressed = ImGui::Button("Cancel");
            if (confirmed || okPressed)
            {
                if (const SceneEntity* entity = scene.findEntity(renameState.entityId))
                {
                    commandBus.execute(RenameEntityCommand{entity->name, renameState.buffer.data()});
                }
                ImGui::CloseCurrentPopup();
            }
            else if (cancelPressed)
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void drawInspector(
        EditorScene& scene,
        AICommandBus& commandBus,
        SelectionState& selection,
        ScriptCreatorState& scriptCreator,
        ScriptEditorState& scriptEditor,
        const AIProviderClient& aiProviderClient,
        const std::string& activeProviderId,
        const std::filesystem::path& projectRoot,
        ConsoleState& console,
        EditHistoryState& history,
        ScriptRuntime& scriptRuntime,
        TerrainSculptState& terrainSculpt,
        const HWND nativeWindowHandle)
    {
        ImGui::Begin("Inspector");

        const SceneEntity* selected = selection.selectedEntityId.has_value()
            ? scene.findEntity(*selection.selectedEntityId)
            : nullptr;

        if (selected == nullptr)
        {
            ImGui::TextDisabled("Select an object in the Hierarchy or click one in the Viewport.");
            ImGui::End();
            return;
        }

        const SceneEntity entity = *selected;

        static int lastEntityId = -1;
        static std::array<char, 128> nameBuffer{};
        if (lastEntityId != entity.id)
        {
            std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", entity.name.c_str());
            lastEntityId = entity.id;
        }

        if (selection.multiSelectedIds.size() > 1)
        {
            ImGui::TextColored(
                ImVec4(1.0F, 0.8F, 0.2F, 1.0F),
                "Editing '%s' (+%d more selected)",
                entity.name.c_str(),
                static_cast<int>(selection.multiSelectedIds.size()) - 1);
        }
        if (entity.isTerrain)
        {
            ImGui::TextUnformatted("Shape: Terrain");
        }
        else if (entity.isImportedMesh)
        {
            ImGui::TextUnformatted("Shape: Imported Model");
        }
        else if (entity.isTextMesh)
        {
            ImGui::TextUnformatted("Shape: Text Mesh");
        }
        else if (entity.isCineCamera)
        {
            ImGui::TextUnformatted("Shape: Cine Camera");
        }
        else
        {
            ImGui::Text("Shape: %s", primitiveTypeName(entity.primitive));
        }
        if (hasScript(entity, gameforger::editor::fpsdemo::kGameManager))
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98F, 0.62F, 0.1F, 1.0F));
            ImGui::TextWrapped(
                "GAME MANAGER - the built game's settings. Edit title, splash logo (Change Image...) and intro "
                "under Scripts > game_manager.lua below; File > Build Game makes this scene the one "
                "GameForgerRuntime loads.");
            ImGui::PopStyleColor();
        }

        bool active = entity.active;
        if (ImGui::Checkbox("Active", &active))
        {
            if (SceneEntity* mutableEntity = scene.findEntityMutable(entity.id))
            {
                mutableEntity->active = active;
            }
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputText("##EntityName", nameBuffer.data(), nameBuffer.size());
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            commandBus.execute(RenameEntityCommand{entity.name, nameBuffer.data()});
        }

        ImGui::TextUnformatted("Tags");
        ImGui::TextDisabled(
            "Priority order, top = primary - e.g. what a \"Ground\" auto-collider check looks at first.");
        {
            int tagIndexToRemove = -1;
            for (int index = 0; index < static_cast<int>(entity.tags.size()); ++index)
            {
                ImGui::PushID(index);
                ImGui::Text("%d.", index + 1);
                ImGui::SameLine();
                ImGui::TextUnformatted(entity.tags[static_cast<std::size_t>(index)].c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Up") && index > 0)
                {
                    if (SceneEntity* mutableEntity = scene.findEntityMutable(entity.id))
                    {
                        std::swap(
                            mutableEntity->tags[static_cast<std::size_t>(index)],
                            mutableEntity->tags[static_cast<std::size_t>(index - 1)]);
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Down") && index + 1 < static_cast<int>(entity.tags.size()))
                {
                    if (SceneEntity* mutableEntity = scene.findEntityMutable(entity.id))
                    {
                        std::swap(
                            mutableEntity->tags[static_cast<std::size_t>(index)],
                            mutableEntity->tags[static_cast<std::size_t>(index + 1)]);
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove"))
                {
                    tagIndexToRemove = index;
                }
                ImGui::PopID();
            }
            if (tagIndexToRemove >= 0)
            {
                commandBus.execute(RemoveTagCommand{
                    entity.name, entity.tags[static_cast<std::size_t>(tagIndexToRemove)]});
            }
            if (entity.tags.empty())
            {
                ImGui::TextDisabled("Untagged");
            }
        }

        constexpr std::array<const char*, 10> tagPresets{
            "Player", "Enemy", "Prop", "Environment", "Trigger", "Ground",
            "PlayerCastle", "EnemyCastle", "PlayerCatapult", "EnemyCatapult"};
        if (ImGui::BeginCombo("##TagPresetsCombo", "Add preset tag..."))
        {
            for (const char* preset : tagPresets)
            {
                if (ImGui::Selectable(preset))
                {
                    commandBus.execute(AddTagCommand{entity.name, std::string(preset)});
                }
            }
            ImGui::EndCombo();
        }
        static std::array<char, 64> customTagBuffer{};
        ImGui::SetNextItemWidth(160.0F);
        const bool customTagSubmitted = ImGui::InputText(
            "##CustomTag", customTagBuffer.data(), customTagBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button("Add Tag") || customTagSubmitted) && customTagBuffer[0] != '\0')
        {
            commandBus.execute(AddTagCommand{entity.name, std::string(customTagBuffer.data())});
            customTagBuffer.fill('\0');
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Transform");

        std::array<float, 3> position{entity.position.x, entity.position.y, entity.position.z};
        if (ImGui::DragFloat3("Position", position.data(), 0.05F))
        {
            commandBus.execute(SetPropertyCommand{
                entity.name, "Transform", "position", glm::vec3(position[0], position[1], position[2])});
        }

        std::array<float, 3> rotation{entity.rotationEuler.x, entity.rotationEuler.y, entity.rotationEuler.z};
        if (ImGui::DragFloat3("Rotation", rotation.data(), 1.0F))
        {
            commandBus.execute(SetPropertyCommand{
                entity.name, "Transform", "rotation", glm::vec3(rotation[0], rotation[1], rotation[2])});
        }

        std::array<float, 3> scale{entity.scale.x, entity.scale.y, entity.scale.z};
        if (ImGui::DragFloat3("Scale", scale.data(), 0.05F, 0.01F, 100.0F))
        {
            commandBus.execute(SetPropertyCommand{
                entity.name, "Transform", "scale", glm::vec3(scale[0], scale[1], scale[2])});
        }

        if (entity.isTextMesh)
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Text Mesh");

            static int lastTextEntityId = -1;
            static std::array<char, 512> textContentBuffer{};
            if (lastTextEntityId != entity.id)
            {
                std::snprintf(
                    textContentBuffer.data(), textContentBuffer.size(), "%s", entity.textMesh.content.c_str());
                lastTextEntityId = entity.id;
            }
            ImGui::InputTextMultiline(
                "Content", textContentBuffer.data(), textContentBuffer.size(), ImVec2(0.0F, 60.0F));
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                commandBus.execute(
                    SetPropertyCommand{entity.name, "TextMesh", "content", std::string(textContentBuffer.data())});
            }

            ImGui::Text("Font: %s", entity.textMesh.fontPath.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Change Font..."))
            {
                const std::filesystem::path fontsDirectory = projectRoot / "Game" / "Fonts";
                if (const std::optional<std::filesystem::path> picked =
                        showOpenFontDialog(nativeWindowHandle, fontsDirectory))
                {
                    if (const std::optional<std::string> imported = importFontIntoProject(*picked, projectRoot))
                    {
                        commandBus.execute(SetPropertyCommand{entity.name, "TextMesh", "fontPath", *imported});
                    }
                }
            }

            float textFontSize = entity.textMesh.fontSize;
            if (ImGui::DragFloat("Font Size", &textFontSize, 0.02F, 0.05F, 20.0F))
            {
                commandBus.execute(SetPropertyCommand{entity.name, "TextMesh", "fontSize", textFontSize});
            }

            float textDepth = entity.textMesh.depth;
            if (ImGui::DragFloat("Extrusion Depth", &textDepth, 0.01F, 0.01F, 5.0F))
            {
                commandBus.execute(SetPropertyCommand{entity.name, "TextMesh", "depth", textDepth});
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Pivot");
        ImGui::TextDisabled("What Move/Rotate/Scale (and animation) pivot around - e.g. a door's hinge edge.");

        std::array<float, 3> pivot{entity.pivotOffset.x, entity.pivotOffset.y, entity.pivotOffset.z};
        if (ImGui::DragFloat3("Pivot Offset", pivot.data(), 0.05F, -1.0F, 1.0F))
        {
            commandBus.execute(SetPropertyCommand{
                entity.name, "Transform", "pivot", glm::vec3(pivot[0], pivot[1], pivot[2])});
        }

        {
            const std::array<std::pair<const char*, glm::vec3>, 9> pivotPresets{{
                {"TL", glm::vec3(-1.0F, 1.0F, 0.0F)}, {"T", glm::vec3(0.0F, 1.0F, 0.0F)}, {"TR", glm::vec3(1.0F, 1.0F, 0.0F)},
                {"L", glm::vec3(-1.0F, 0.0F, 0.0F)}, {"C", glm::vec3(0.0F, 0.0F, 0.0F)}, {"R", glm::vec3(1.0F, 0.0F, 0.0F)},
                {"BL", glm::vec3(-1.0F, -1.0F, 0.0F)}, {"B", glm::vec3(0.0F, -1.0F, 0.0F)}, {"BR", glm::vec3(1.0F, -1.0F, 0.0F)},
            }};
            for (int index = 0; index < static_cast<int>(pivotPresets.size()); ++index)
            {
                if (index % 3 != 0)
                {
                    ImGui::SameLine();
                }
                ImGui::PushID(index);
                if (ImGui::Button(pivotPresets[static_cast<std::size_t>(index)].first, ImVec2(32.0F, 22.0F)))
                {
                    commandBus.execute(SetPropertyCommand{
                        entity.name, "Transform", "pivot", pivotPresets[static_cast<std::size_t>(index)].second});
                }
                ImGui::PopID();
            }
            ImGui::TextDisabled("Or describe it in AI Forge, e.g. \"pivot the door on its left edge\".");
        }

        // Built-in (browse Game/Textures/) vs Upload-from-PC texture picker,
        // like Unity's per-slot TerrainLayer texture assignment - shared by
        // the Appearance section below (SceneEntity::materialLayers, any
        // entity) and the Terrain section further down
        // (TerrainData::layers, isTerrain entities only), since both need
        // the exact same picker UI. Leaving Normal/Height empty
        // auto-generates them from the Diffuse texture's own brightness
        // (see TerrainTexture.hpp).
        const auto drawTexturePicker = [&](const char* label,
                                            const std::string& currentPath,
                                            const std::function<void(const std::string&)>& apply)
        {
            ImGui::PushID(label);
            ImGui::Text("%s: %s", label, currentPath.empty() ? "(none - auto-generated)" : currentPath.c_str());
            ImGui::SetNextItemWidth(-1.0F);
            if (ImGui::BeginCombo("##BuiltIn", "Browse project textures..."))
            {
                std::error_code walkError;
                const std::filesystem::path texturesRoot = projectRoot / "Game" / "Textures";
                if (std::filesystem::exists(texturesRoot, walkError))
                {
                    for (const std::filesystem::directory_entry& fileEntry :
                        std::filesystem::recursive_directory_iterator(texturesRoot, walkError))
                    {
                        if (!fileEntry.is_regular_file())
                        {
                            continue;
                        }
                        std::string extension = fileEntry.path().extension().string();
                        std::transform(
                            extension.begin(), extension.end(), extension.begin(),
                            [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        const bool isImage = extension == ".png" || extension == ".jpg" ||
                            extension == ".jpeg" || extension == ".bmp" || extension == ".tga";
                        if (!isImage)
                        {
                            continue;
                        }
                        std::error_code relativeError;
                        const std::string relative =
                            std::filesystem::relative(fileEntry.path(), projectRoot, relativeError)
                                .generic_string();
                        if (relativeError)
                        {
                            continue;
                        }
                        if (ImGui::Selectable(relative.c_str(), relative == currentPath))
                        {
                            apply(relative);
                        }
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Import Texture from PC...", ImVec2(-1.0F, 0.0F)))
            {
                if (const std::optional<std::filesystem::path> picked =
                        showOpenImageDialog(nativeWindowHandle, projectRoot / "Game" / "Textures"))
                {
                    if (const std::optional<std::string> imported = importTextureIntoProject(*picked, projectRoot))
                    {
                        apply(*imported);
                    }
                    else
                    {
                        logMessage(console, LogLevel::Error, "Could not import that texture into the project.");
                    }
                }
            }
            if (!currentPath.empty())
            {
                if (ImGui::SmallButton("Clear##Texture"))
                {
                    apply(std::string());
                }
            }
            ImGui::PopID();
        };

        // Same Built-in/Upload idea as drawTexturePicker above, but scoped
        // to icon-appropriate folders instead of Game/Textures - the
        // built-in Ravenmore icon pack (Game/Models/iconpack1, 48 icons:
        // weapons/armor/potions/gems/hearts/etc, including Backpack.png
        // used as the inventory window's own icon and Map.png reserved for
        // a future map feature) plus Game/Icons/ for uploaded custom icons.
        // Kept separate from drawTexturePicker rather than generalizing it
        // with a root-directory parameter, since that would mean touching
        // all ~15 existing terrain/material picker call sites for a lambda
        // only used once here.
        const auto drawIconPicker = [&](const std::string& currentPath, const std::function<void(const std::string&)>& apply)
        {
            ImGui::PushID("IconPicker");
            if (!currentPath.empty())
            {
                const GLuint iconTexture = ensureIconTextureGpu(currentPath, projectRoot);
                if (iconTexture != 0)
                {
                    ImGui::Image(static_cast<ImTextureID>(iconTexture), ImVec2(32.0F, 32.0F));
                    ImGui::SameLine();
                }
            }
            ImGui::Text("Icon: %s", currentPath.empty() ? "(none)" : currentPath.c_str());
            ImGui::SetNextItemWidth(-1.0F);
            if (ImGui::BeginCombo("##BuiltInIcons", "Browse icons..."))
            {
                const std::array<std::filesystem::path, 2> iconRoots{
                    projectRoot / "Game" / "Models" / "iconpack1", projectRoot / "Game" / "Icons"};
                for (const std::filesystem::path& iconRoot : iconRoots)
                {
                    std::error_code walkError;
                    if (!std::filesystem::exists(iconRoot, walkError))
                    {
                        continue;
                    }
                    for (const std::filesystem::directory_entry& fileEntry :
                        std::filesystem::recursive_directory_iterator(iconRoot, walkError))
                    {
                        if (!fileEntry.is_regular_file())
                        {
                            continue;
                        }
                        std::string extension = fileEntry.path().extension().string();
                        std::transform(
                            extension.begin(), extension.end(), extension.begin(),
                            [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        const bool isImage = extension == ".png" || extension == ".jpg" ||
                            extension == ".jpeg" || extension == ".bmp" || extension == ".tga";
                        if (!isImage)
                        {
                            continue;
                        }
                        std::error_code relativeError;
                        const std::string relative =
                            std::filesystem::relative(fileEntry.path(), projectRoot, relativeError)
                                .generic_string();
                        if (relativeError)
                        {
                            continue;
                        }
                        const GLuint thumbnail = ensureIconTextureGpu(relative, projectRoot);
                        if (thumbnail != 0)
                        {
                            ImGui::Image(static_cast<ImTextureID>(thumbnail), ImVec2(20.0F, 20.0F));
                            ImGui::SameLine();
                        }
                        if (ImGui::Selectable(relative.c_str(), relative == currentPath))
                        {
                            apply(relative);
                        }
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Import Icon from PC...", ImVec2(-1.0F, 0.0F)))
            {
                if (const std::optional<std::filesystem::path> picked =
                        showOpenImageDialog(nativeWindowHandle, projectRoot / "Game" / "Icons"))
                {
                    if (const std::optional<std::string> imported = importIconIntoProject(*picked, projectRoot))
                    {
                        apply(*imported);
                    }
                    else
                    {
                        logMessage(console, LogLevel::Error, "Could not import that icon into the project.");
                    }
                }
            }
            if (!currentPath.empty())
            {
                if (ImGui::SmallButton("Clear##Icon"))
                {
                    apply(std::string());
                }
            }
            ImGui::PopID();
        };

        ImGui::Separator();
        ImGui::TextUnformatted("Appearance");
        std::array<float, 3> color{entity.color.r, entity.color.g, entity.color.b};
        if (ImGui::ColorEdit3("Color", color.data()))
        {
            commandBus.execute(SetPropertyCommand{
                entity.name, "Renderer", "color", glm::vec3(color[0], color[1], color[2])});
        }
        ImGui::TextDisabled("The flat color above is used wherever the mask weight below is 0 for all 3 slots.");

        if (!entity.isTerrain)
        {
            ImGui::TextUnformatted("Texture (optional, up to 3 layers)");
            ImGui::TextDisabled(
                "Import up to 3 textures and mix them with the RGB mask - 0 on all three uses the flat "
                "color above instead. No real UVs on primitives, so textures wrap the whole object via "
                "triplanar projection (from world-position, not per-face unwrap).");

            std::array<float, 3> materialWeight{
                entity.materialBlendWeight.r, entity.materialBlendWeight.g, entity.materialBlendWeight.b};
            if (ImGui::SliderFloat3("Mask RGB", materialWeight.data(), 0.0F, 1.0F))
            {
                if (SceneEntity* mutableEntity = scene.findEntityMutable(entity.id))
                {
                    mutableEntity->materialBlendWeight =
                        glm::vec3(materialWeight[0], materialWeight[1], materialWeight[2]);
                }
            }

            std::array<float, 2> materialUvScale{entity.materialUvScale.x, entity.materialUvScale.y};
            if (ImGui::DragFloat2("UV Scale", materialUvScale.data(), 0.25F, 0.1F, 512.0F, "%.2f"))
            {
                if (SceneEntity* mutableEntity = scene.findEntityMutable(entity.id))
                {
                    mutableEntity->materialUvScale =
                        glm::vec2(std::max(0.01F, materialUvScale[0]), std::max(0.01F, materialUvScale[1]));
                }
            }
            ImGui::TextDisabled(
                "World units per texture repeat (X, Z), triplanar - try 32x32 for tighter tiling on a "
                "big wall/floor, or an uneven pair like 20x128 for a stretched look.");

            for (int layerIndex = 0; layerIndex < 3; ++layerIndex)
            {
                ImGui::PushID(layerIndex + 200);
                ImGui::Text("Layer %d (%s)", layerIndex + 1, layerIndex == 0 ? "R" : layerIndex == 1 ? "G" : "B");
                const auto applyMaterialField =
                    [&, layerIndex](std::string TerrainLayerData::* field, const std::string& value)
                {
                    if (SceneEntity* mutableEntity = scene.findEntityMutable(entity.id))
                    {
                        mutableEntity->materialLayers[static_cast<std::size_t>(layerIndex)].*field = value;
                    }
                };
                const TerrainLayerData& materialLayerData =
                    entity.materialLayers[static_cast<std::size_t>(layerIndex)];
                drawTexturePicker(
                    "Diffuse", materialLayerData.diffusePath,
                    [&](const std::string& v)
                    {
                        applyMaterialField(&TerrainLayerData::diffusePath, v);
                        // Fixes a real bug: assigning a diffuse texture here had
                        // no visible effect if Mask RGB was still all-zero (the
                        // slider above defaults to 0,0,0, and hasMaterial in
                        // ViewportRenderer requires a nonzero weight sum to even
                        // switch to the textured shader) - so a freshly-picked
                        // texture silently kept rendering as the flat color,
                        // reading as "texture doesn't apply". Mirrors terrain's
                        // own CreateTerrainCommand default (new layer starts at
                        // full weight) - only bumps THIS channel, and only if it
                        // was exactly 0, so it never overrides a blend already
                        // set up on purpose.
                        if (SceneEntity* mutableEntity = scene.findEntityMutable(entity.id);
                            mutableEntity != nullptr && !v.empty() &&
                            mutableEntity->materialBlendWeight[layerIndex] <= 0.0F)
                        {
                            mutableEntity->materialBlendWeight[layerIndex] = 1.0F;
                        }
                    });
                drawTexturePicker(
                    "Height Map (optional)", materialLayerData.heightPath,
                    [&](const std::string& v) { applyMaterialField(&TerrainLayerData::heightPath, v); });
                ImGui::PopID();
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Collider");
        bool hasCollider = entity.hasCollider;
        if (ImGui::Checkbox("Solid (blocks scripted physics)", &hasCollider))
        {
            commandBus.execute(SetPropertyCommand{entity.name, "Collider", "enabled", hasCollider});
        }
        ImGui::TextDisabled(
            "Ground/walls/platforms a self.physics:resolve() script (FPS/Third-Person/Rigidbody presets) "
            "collides with. Tagging an object \"Ground\" turns this on automatically.");

        ImGui::Separator();
        ImGui::TextUnformatted("Parent");
        ImGui::TextDisabled(
            "Usually set via the Hierarchy panel's \"Add Child\" (right-click a parent object) - shown "
            "here mainly to re-parent or unparent. While parented, this object's position/rotation/scale "
            "become WORLD values computed from Local Position/Rotation/Scale below plus the parent's own "
            "transform every frame, recursively (a child of a child follows too) - true Unity-style "
            "parenting, moving/rotating/scaling the parent carries every descendant with it.");
        {
            const std::string parentPreview = entity.parentName.empty() ? "(None)" : entity.parentName;
            if (ImGui::BeginCombo("Parent Object", parentPreview.c_str()))
            {
                if (ImGui::Selectable("(None)", entity.parentName.empty()))
                {
                    commandBus.execute(SetPropertyCommand{entity.name, "Parent", "parentName", std::string()});
                }
                for (const SceneEntity& other : scene.entities())
                {
                    if (other.id == entity.id)
                    {
                        continue;
                    }
                    ImGui::PushID(other.id);
                    const bool isSelected = other.name == entity.parentName;
                    if (ImGui::Selectable(other.name.c_str(), isSelected))
                    {
                        commandBus.execute(SetPropertyCommand{entity.name, "Parent", "parentName", other.name});
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            if (!entity.parentName.empty())
            {
                std::array<float, 3> localPosition{
                    entity.localPosition.x, entity.localPosition.y, entity.localPosition.z};
                if (ImGui::DragFloat3("Local Position", localPosition.data(), 0.05F))
                {
                    commandBus.execute(SetPropertyCommand{
                        entity.name, "Parent", "localPosition",
                        glm::vec3(localPosition[0], localPosition[1], localPosition[2])});
                }
                std::array<float, 3> localRotation{
                    entity.localRotationEuler.x, entity.localRotationEuler.y, entity.localRotationEuler.z};
                if (ImGui::DragFloat3("Local Rotation", localRotation.data(), 0.5F))
                {
                    commandBus.execute(SetPropertyCommand{
                        entity.name, "Parent", "localRotation",
                        glm::vec3(localRotation[0], localRotation[1], localRotation[2])});
                }
                std::array<float, 3> localScale{entity.localScale.x, entity.localScale.y, entity.localScale.z};
                if (ImGui::DragFloat3("Local Scale", localScale.data(), 0.05F, 0.01F, 100.0F))
                {
                    commandBus.execute(SetPropertyCommand{
                        entity.name, "Parent", "localScale",
                        glm::vec3(localScale[0], localScale[1], localScale[2])});
                }
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Pickup Item");
        bool isPickupItem = entity.isPickupItem;
        if (ImGui::Checkbox("Is Pickup Item", &isPickupItem))
        {
            commandBus.execute(SetPropertyCommand{entity.name, "PickupItem", "enabled", isPickupItem});
        }
        if (entity.isPickupItem)
        {
            ImGui::TextDisabled(
                "While Play is running, a scripted FPS/Third-Person camera facing this object shows a "
                "\"[E] Pick up\" prompt in range - E removes it and adds it to the inventory (press I to "
                "open). No script needed on this object or the player.");

            std::array<char, 64> itemNameBuffer{};
            std::snprintf(itemNameBuffer.data(), itemNameBuffer.size(), "%s", entity.pickupItem.itemName.c_str());
            if (ImGui::InputText("Item Name", itemNameBuffer.data(), itemNameBuffer.size()))
            {
                commandBus.execute(
                    SetPropertyCommand{entity.name, "PickupItem", "itemName", std::string(itemNameBuffer.data())});
            }

            drawIconPicker(
                entity.pickupItem.iconPath,
                [&](const std::string& v)
                { commandBus.execute(SetPropertyCommand{entity.name, "PickupItem", "iconPath", v}); });
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Castle");
        bool isCastle = entity.isCastle;
        if (ImGui::Checkbox("Is Castle", &isCastle))
        {
            commandBus.execute(SetPropertyCommand{entity.name, "Castle", "enabled", isCastle});
        }
        if (entity.isCastle)
        {
            ImGui::TextDisabled(
                "Takes damage from gravity-fired catapult boulders whose hitTag matches one of this "
                "object's Tags (\"PlayerCastle\"/\"EnemyCastle\") - also needs \"Is Collider\" on above, "
                "since the hit test is this entity's collider AABB, not its visual mesh.");
            if (!entity.hasCollider)
            {
                ImGui::TextColored(
                    ImVec4(1.0F, 0.6F, 0.2F, 1.0F), "Warning: no Collider - boulders will pass through.");
            }
            float castleHp = entity.castle.hp;
            if (ImGui::DragFloat("HP", &castleHp, 1.0F, 0.0F, entity.castle.maxHp))
            {
                commandBus.execute(SetPropertyCommand{entity.name, "Castle", "hp", castleHp});
            }
            float castleMaxHp = entity.castle.maxHp;
            if (ImGui::DragFloat("Max HP", &castleMaxHp, 1.0F, 1.0F, 10000.0F))
            {
                commandBus.execute(SetPropertyCommand{entity.name, "Castle", "maxHp", castleMaxHp});
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Catapult");
        bool isCatapult = entity.isCatapult;
        if (ImGui::Checkbox("Is Catapult", &isCatapult))
        {
            commandBus.execute(SetPropertyCommand{entity.name, "Catapult", "enabled", isCatapult});
        }
        if (entity.isCatapult)
        {
            ImGui::TextDisabled(
                "Tag this entity \"PlayerCatapult\" (walk up, E to aim/mouse/R to fire) or "
                "\"EnemyCatapult\" (auto-fires on a timer along its authored facing). Yaw Entity/Arm "
                "Entity should be children of this one (Hierarchy > Add Child) that the aiming system "
                "rotates directly.");
            const auto entityCombo = [&](const char* label, std::string& currentName, const char* component,
                                          const char* property)
            {
                const std::string preview = currentName.empty() ? "(None)" : currentName;
                if (ImGui::BeginCombo(label, preview.c_str()))
                {
                    if (ImGui::Selectable("(None)", currentName.empty()))
                    {
                        commandBus.execute(SetPropertyCommand{entity.name, component, property, std::string()});
                    }
                    for (const SceneEntity& other : scene.entities())
                    {
                        if (other.id == entity.id)
                        {
                            continue;
                        }
                        ImGui::PushID(other.id);
                        if (ImGui::Selectable(other.name.c_str(), other.name == currentName))
                        {
                            commandBus.execute(
                                SetPropertyCommand{entity.name, component, property, other.name});
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
            };
            std::string yawEntityName = entity.catapult.yawEntityName;
            entityCombo("Yaw Entity", yawEntityName, "Catapult", "yawEntityName");
            std::string armEntityName = entity.catapult.armEntityName;
            entityCombo("Arm Entity", armEntityName, "Catapult", "armEntityName");

            float minPitch = entity.catapult.minPitchDegrees;
            if (ImGui::DragFloat("Min Pitch", &minPitch, 0.5F, 0.0F, 89.0F))
            {
                commandBus.execute(SetPropertyCommand{entity.name, "Catapult", "minPitchDegrees", minPitch});
            }
            float maxPitch = entity.catapult.maxPitchDegrees;
            if (ImGui::DragFloat("Max Pitch", &maxPitch, 0.5F, 0.0F, 89.0F))
            {
                commandBus.execute(SetPropertyCommand{entity.name, "Catapult", "maxPitchDegrees", maxPitch});
            }
            float launchSpeed = entity.catapult.launchSpeed;
            if (ImGui::DragFloat("Launch Speed", &launchSpeed, 0.5F, 1.0F, 200.0F))
            {
                commandBus.execute(SetPropertyCommand{entity.name, "Catapult", "launchSpeed", launchSpeed});
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Cine Camera");
        bool isCineCamera = entity.isCineCamera;
        if (ImGui::Checkbox("Is Cine Camera", &isCineCamera))
        {
            commandBus.execute(SetPropertyCommand{entity.name, "CineCamera", "enabled", isCineCamera});
        }
        if (entity.isCineCamera)
        {
            ImGui::TextDisabled(
                "Cutscene camera, separate from the player/Game view camera - renders as a wireframe icon. "
                "Move it and record keyframes on the Animation tab to author its path (enable \"Animate "
                "Object\" there first), then capture the path as a numbered shot in the Storyboard panel.");
        }

        if (entity.isTerrain)
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Terrain");
            ImGui::Text(
                "%d x %d vertices, height scale %.1f",
                entity.terrain.resolution,
                entity.terrain.resolution,
                static_cast<double>(entity.terrain.heightScale));
            {
                // This is also the terrain's actual COLLISION boundary
                // (resolveBoxCollision, ScriptRuntime.cpp, uses this exact
                // field - walking past worldSize/2 from the terrain's
                // center has no ground at all, however big the object
                // LOOKS via Transform Scale, which the collider ignores).
                // Was previously fixed at 50 with no way to change it, at
                // creation or after - the real cause of a "huge" map still
                // only being walkable in a small area in the middle.
                float worldSize = entity.terrain.worldSize;
                if (ImGui::DragFloat("World Size", &worldSize, 5.0F, 10.0F, 4000.0F, "%.0f"))
                {
                    if (SceneEntity* mutableEntity = scene.findEntityMutable(entity.id))
                    {
                        mutableEntity->terrain.worldSize = std::max(10.0F, worldSize);
                    }
                }
                ImGui::TextDisabled(
                    "Total walkable width/depth in world units - also the real collision boundary. "
                    "Safe to grow freely (your sculpted heights stay exactly where they are, just "
                    "spread across more space); Resolution stays fixed, so a much bigger World Size "
                    "makes each existing bump/valley span more ground - re-sculpt after growing it if "
                    "you want finer detail across the new area.");
            }

            ImGui::Checkbox("Sculpt Mode", &terrainSculpt.active);
            if (ImGui::RadioButton("Raise", !terrainSculpt.paintMode && !terrainSculpt.lowerMode))
            {
                terrainSculpt.paintMode = false;
                terrainSculpt.lowerMode = false;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Lower", !terrainSculpt.paintMode && terrainSculpt.lowerMode))
            {
                terrainSculpt.paintMode = false;
                terrainSculpt.lowerMode = true;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Paint", terrainSculpt.paintMode))
            {
                terrainSculpt.paintMode = true;
            }
            ImGui::TextDisabled(
                "While on: left-click-drag in the Viewport sculpts height (Raise/Lower) or paints the "
                "selected layer's texture (Paint). Hold Shift while sculpting to temporarily use the "
                "other height mode.");
            ImGui::DragFloat("Brush Radius", &terrainSculpt.brushRadius, 0.1F, 0.5F, 100.0F);
            ImGui::DragFloat("Brush Strength", &terrainSculpt.brushStrength, 0.05F, 0.1F, 20.0F);

            if (terrainSculpt.paintMode)
            {
                ImGui::Separator();
                ImGui::TextUnformatted("Paint Layer");
                ImGui::TextDisabled("Which of the 3 layers below the brush paints.");
                for (int layerIndex = 0; layerIndex < 3; ++layerIndex)
                {
                    ImGui::PushID(layerIndex);
                    std::array<char, 16> label{};
                    std::snprintf(label.data(), label.size(), "Layer %d", layerIndex + 1);
                    if (ImGui::RadioButton(label.data(), terrainSculpt.paintLayerIndex == layerIndex))
                    {
                        terrainSculpt.paintLayerIndex = layerIndex;
                    }
                    ImGui::PopID();
                    if (layerIndex < 2)
                    {
                        ImGui::SameLine();
                    }
                }
            }

            // Deliberately NOT gated behind Sculpt Mode/Paint - assigning
            // which texture goes in each layer is independent of whether
            // you're actively brush-painting right now, unlike the "Paint
            // Layer" selector above (which only matters while painting).
            // Built-in (browse Game/Textures/) vs Upload-from-PC picker,
            // like Unity's per-slot TerrainLayer texture assignment.
            // Leaving Normal/Height empty auto-generates them from the
            // Diffuse texture's own brightness (see TerrainTexture.hpp) -
            // real authored maps can replace either at any time, the
            // renderer picks them up the next time this changes.
            ImGui::Separator();
            ImGui::TextUnformatted("Ground Textures (3 layers)");
            ImGui::TextDisabled(
                "Assign a texture to each layer here, then turn on Sculpt Mode + Paint above to brush "
                "where each one shows.");

            {
                std::array<float, 2> uvScale{entity.terrain.uvScale.x, entity.terrain.uvScale.y};
                if (ImGui::DragFloat2("UV Scale", uvScale.data(), 0.5F, 0.25F, 512.0F, "%.2f"))
                {
                    if (SceneEntity* mutableEntity = scene.findEntityMutable(entity.id))
                    {
                        mutableEntity->terrain.uvScale = glm::vec2(std::max(0.01F, uvScale[0]), std::max(0.01F, uvScale[1]));
                    }
                }
                ImGui::TextDisabled(
                    "World units per texture repeat (X, Z) - shared by all 3 layers. Try 32x32 for a "
                    "tighter grass repeat, or an uneven pair like 20x128 for a stretched look.");
            }
            for (int layerIndex = 0; layerIndex < 3; ++layerIndex)
            {
                ImGui::PushID(layerIndex + 100);
                ImGui::Text("Layer %d Textures", layerIndex + 1);
                const auto applyField = [&, layerIndex](std::string TerrainLayerData::* field, const std::string& value)
                {
                    if (SceneEntity* mutableEntity = scene.findEntityMutable(entity.id))
                    {
                        mutableEntity->terrain.layers[static_cast<std::size_t>(layerIndex)].*field = value;
                    }
                };
                const TerrainLayerData& layerData =
                    entity.terrain.layers[static_cast<std::size_t>(layerIndex)];
                drawTexturePicker(
                    "Diffuse", layerData.diffusePath,
                    [&](const std::string& v) { applyField(&TerrainLayerData::diffusePath, v); });
                drawTexturePicker(
                    "Normal Map (optional)", layerData.normalPath,
                    [&](const std::string& v) { applyField(&TerrainLayerData::normalPath, v); });
                drawTexturePicker(
                    "Height Map (optional)", layerData.heightPath,
                    [&](const std::string& v) { applyField(&TerrainLayerData::heightPath, v); });
                ImGui::PopID();
                ImGui::Separator();
            }

            if (ImGui::Button("Import Heightmap..."))
            {
                if (const std::optional<std::filesystem::path> picked =
                        showOpenImageDialog(nativeWindowHandle, projectRoot / "Game" / "Textures"))
                {
                    const std::vector<float> heights = loadHeightmapImage(*picked, entity.terrain.resolution);
                    if (heights.empty())
                    {
                        logMessage(console, LogLevel::Error, "Could not load that image as a heightmap.");
                    }
                    else if (SceneEntity* mutableEntity = scene.findEntityMutable(entity.id))
                    {
                        mutableEntity->terrain.heights = heights;
                        logMessage(console, LogLevel::Info, "Heightmap imported onto '" + entity.name + "'.");
                    }
                }
            }
            ImGui::TextDisabled("A grayscale image works best - brightness becomes height.");

            static float noiseScale = 4.0F;
            static int noiseOctaves = 4;
            static int noiseSeed = 1;
            ImGui::DragFloat("Noise Scale", &noiseScale, 0.05F, 0.1F, 32.0F);
            ImGui::SliderInt("Noise Octaves", &noiseOctaves, 1, 8);
            ImGui::InputInt("Noise Seed", &noiseSeed);
            if (ImGui::Button("Generate (Perlin Noise)"))
            {
                if (SceneEntity* mutableEntity = scene.findEntityMutable(entity.id))
                {
                    const int resolution = mutableEntity->terrain.resolution;
                    const PerlinPermutation perm(static_cast<unsigned int>(noiseSeed));
                    std::vector<float>& heights = mutableEntity->terrain.heights;
                    heights.resize(static_cast<std::size_t>(resolution) * static_cast<std::size_t>(resolution));
                    for (int row = 0; row < resolution; ++row)
                    {
                        for (int col = 0; col < resolution; ++col)
                        {
                            const float sampleX = static_cast<float>(col) / static_cast<float>(resolution) * noiseScale;
                            const float sampleY = static_cast<float>(row) / static_cast<float>(resolution) * noiseScale;
                            const float noise = fractalPerlinNoise2D(perm, sampleX, sampleY, noiseOctaves, 0.5F);
                            heights[static_cast<std::size_t>(row) * static_cast<std::size_t>(resolution) +
                                static_cast<std::size_t>(col)] = std::clamp(noise * 0.5F + 0.5F, 0.0F, 1.0F);
                        }
                    }
                    logMessage(console, LogLevel::Info, "Generated Perlin noise terrain for '" + entity.name + "'.");
                }
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Scripts");
        ImGui::TextDisabled("Click a script to focus it - Ctrl+C/Ctrl+V/Delete then act on it.");
        {
            // Each of these three drives its own independent gravity/ground-
            // collision every frame and writes the entity's transform - having
            // more than one attached makes them fight over it every frame
            // (frozen/jittery movement, broken jump, a controller's camera
            // that looks like it isn't following). The Presets picker now
            // prevents attaching a new combination like this, but this warns
            // about scenes/entities that already have one from before.
            constexpr std::array<const char*, 3> physicsDrivingScripts{
                "Game/Scripts/fps_controller.lua",
                "Game/Scripts/third_person_controller.lua",
                "Game/Scripts/rigidbody.lua"};
            const int physicsDrivingCount = static_cast<int>(std::count_if(
                entity.scripts.begin(),
                entity.scripts.end(),
                [&physicsDrivingScripts](const std::string& scriptPath)
                {
                    return std::find(
                               physicsDrivingScripts.begin(), physicsDrivingScripts.end(), scriptPath) !=
                        physicsDrivingScripts.end();
                }));
            if (physicsDrivingCount > 1)
            {
                ImGui::TextColored(
                    ImVec4(0.95F, 0.65F, 0.25F, 1.0F),
                    "Warning: more than one movement/physics script attached (FPS/Third-Person "
                    "Controller/Rigidbody) - remove all but one, they fight over this object's position "
                    "every frame.");
            }
        }
        // Plain scripts first, then each preset's scripts under its own line.
        std::vector<std::pair<std::string, std::string>> orderedScripts; // path, preset name ("" = none)
        for (const std::string& scriptPath : entity.scripts)
        {
            const ScriptRuntime::ScriptPresetTag tag = ScriptRuntime::cachedScriptPreset(projectRoot / scriptPath);
            if (tag.name.empty())
            {
                orderedScripts.emplace_back(scriptPath, std::string());
            }
        }
        for (const std::string& scriptPath : entity.scripts)
        {
            const ScriptRuntime::ScriptPresetTag tag = ScriptRuntime::cachedScriptPreset(projectRoot / scriptPath);
            if (!tag.name.empty())
            {
                orderedScripts.emplace_back(scriptPath, tag.name);
            }
        }
        std::string previousPreset;
        for (const auto& [scriptPath, presetName] : orderedScripts)
        {
            if (!presetName.empty() && presetName != previousPreset)
            {
                ImGui::SeparatorText((presetName + " preset").c_str());
            }
            previousPreset = presetName;
            ImGui::PushID(scriptPath.c_str());
            const bool isFocused =
                history.focusedScriptEntityId.has_value() && *history.focusedScriptEntityId == entity.id &&
                history.focusedScriptPath == scriptPath;

            // Selectable() defaults to filling the entire remaining row width,
            // which - since it's submitted before the Edit/Remove buttons -
            // silently extends its clickable hit-region underneath them,
            // swallowing clicks meant for those buttons (and making it
            // ambiguous whether a click on the row focuses the script or hits
            // a button). Reserve exactly the buttons' own width so the
            // Selectable's hit-region stops before they begin.
            const ImGuiStyle& style = ImGui::GetStyle();
            const float editWidth = ImGui::CalcTextSize("Edit").x + style.FramePadding.x * 2.0F;
            const float removeWidth = ImGui::CalcTextSize("Remove").x + style.FramePadding.x * 2.0F;
            const float reservedWidth = editWidth + removeWidth + style.ItemSpacing.x * 2.0F;
            const float selectableWidth =
                std::max(ImGui::GetContentRegionAvail().x - reservedWidth, 40.0F);
            if (ImGui::Selectable(scriptPath.c_str(), isFocused, 0, ImVec2(selectableWidth, 0.0F)))
            {
                history.focusedScriptEntityId = entity.id;
                history.focusedScriptPath = scriptPath;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", scriptPath.c_str());
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Edit"))
            {
                std::ifstream scriptFile(projectRoot / scriptPath, std::ios::binary);
                std::string content(
                    (std::istreambuf_iterator<char>(scriptFile)), std::istreambuf_iterator<char>());
                scriptEditor.buffer.fill('\0');
                const std::size_t copyLength = std::min(content.size(), scriptEditor.buffer.size() - 1);
                std::memcpy(scriptEditor.buffer.data(), content.data(), copyLength);
                scriptEditor.scriptPath = scriptPath;
                scriptEditor.status.clear();
                scriptEditor.requestOpen = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove"))
            {
                commandBus.execute(DetachScriptCommand{entity.name, scriptPath});
                if (scriptRuntime.isRunning())
                {
                    // Detaching only edits SceneEntity::scripts; the already-
                    // running instance keeps ticking during Play otherwise.
                    scriptRuntime.stopScript(entity.id, scriptPath);
                }
                if (isFocused)
                {
                    history.focusedScriptEntityId.reset();
                    history.focusedScriptPath.clear();
                }
            }

            // The script's `-- @property` fields (ScriptRuntime::
            // ExposedScriptProperty). Outside Play an edit is THIS object's
            // own value - undoable and saved with the scene
            // (SceneEntity::scriptProperties) - so two objects sharing a
            // script (e.g. two items.lua pickups) keep different names/icons.
            // During Play an edit changes the running script live instead
            // (and, like everything in Play, is discarded on Stop).
            const auto& exposedProps = ScriptRuntime::cachedScriptProperties(projectRoot / scriptPath);
            if (!exposedProps.empty())
            {
                using PropertyType = ScriptRuntime::ExposedScriptProperty::Type;
                ImGui::Indent(15.0F);
                const bool live = scriptRuntime.isRunning();
                for (const auto& prop : exposedProps)
                {
                    ImGui::PushID(prop.name.c_str());
                    const gameforger::editor::ScriptPropertyOverride* stored =
                        gameforger::editor::findScriptProperty(entity, scriptPath, prop.name);
                    const std::string storedText = stored != nullptr ? stored->value : prop.defaultAsText();
                    const std::string propertyKey = scriptPath + "#" + prop.name;
                    // Text-typed values (string/icon/enum) share one path.
                    const auto commitText = [&](const std::string& text)
                    {
                        if (live)
                        {
                            scriptRuntime.setScriptStringField(entity.id, scriptPath, prop.name, text);
                        }
                        else
                        {
                            (void)commandBus.execute(SetPropertyCommand{entity.name, "ScriptProperty", propertyKey, text});
                        }
                    };
                    const std::string currentText = live
                        ? scriptRuntime.getScriptStringField(entity.id, scriptPath, prop.name, storedText)
                        : storedText;

                    if (prop.type == PropertyType::Number)
                    {
                        char* parseEnd = nullptr;
                        const float storedValue = std::strtof(storedText.c_str(), &parseEnd);
                        float value = live
                            ? scriptRuntime.getScriptNumberField(entity.id, scriptPath, prop.name, storedValue)
                            : storedValue;
                        if (ImGui::DragFloat(prop.name.c_str(), &value, 0.05F))
                        {
                            if (live)
                            {
                                scriptRuntime.setScriptNumberField(entity.id, scriptPath, prop.name, value);
                            }
                            else
                            {
                                std::array<char, 32> numberText{};
                                std::snprintf(numberText.data(), numberText.size(), "%g", static_cast<double>(value));
                                (void)commandBus.execute(
                                    SetPropertyCommand{entity.name, "ScriptProperty", propertyKey, std::string(numberText.data())});
                            }
                        }
                    }
                    else if (prop.type == PropertyType::Bool)
                    {
                        const bool storedValue = storedText == "true" || storedText == "1";
                        bool value = live
                            ? scriptRuntime.getScriptBoolField(entity.id, scriptPath, prop.name, storedValue)
                            : storedValue;
                        if (ImGui::Checkbox(prop.name.c_str(), &value))
                        {
                            if (live)
                            {
                                scriptRuntime.setScriptBoolField(entity.id, scriptPath, prop.name, value);
                            }
                            else
                            {
                                (void)commandBus.execute(SetPropertyCommand{
                                    entity.name, "ScriptProperty", propertyKey, std::string(value ? "true" : "false")});
                            }
                        }
                    }
                    else if (prop.type == PropertyType::Vec3)
                    {
                        std::array<float, 3> value{prop.defaultVec3.x, prop.defaultVec3.y, prop.defaultVec3.z};
                        {
                            std::string spaced = storedText;
                            std::replace(spaced.begin(), spaced.end(), ',', ' ');
                            std::istringstream stream(spaced);
                            stream >> value[0] >> value[1] >> value[2];
                        }
                        if (ImGui::DragFloat3(prop.name.c_str(), value.data(), 0.05F) && !live)
                        {
                            std::array<char, 96> vectorText{};
                            std::snprintf(vectorText.data(), vectorText.size(), "%g %g %g",
                                static_cast<double>(value[0]), static_cast<double>(value[1]), static_cast<double>(value[2]));
                            (void)commandBus.execute(
                                SetPropertyCommand{entity.name, "ScriptProperty", propertyKey, std::string(vectorText.data())});
                        }
                    }
                    else if (prop.type == PropertyType::Enum)
                    {
                        if (ImGui::BeginCombo(prop.name.c_str(), currentText.c_str()))
                        {
                            for (const std::string& option : prop.options)
                            {
                                if (ImGui::Selectable(option.c_str(), option == currentText))
                                {
                                    commitText(option);
                                }
                            }
                            ImGui::EndCombo();
                        }
                    }
                    else if (prop.type == PropertyType::Image)
                    {
                        // Big preview + "Change Image..." (copied into the
                        // default's own folder, e.g. Game/Branding/).
                        const GLuint imageTexture = currentText.empty() ? 0 : ensureIconTextureGpu(currentText, projectRoot);
                        ImGui::TextUnformatted(prop.name.c_str());
                        const float previewWidth = std::min(ImGui::GetContentRegionAvail().x, 220.0F);
                        if (imageTexture != 0)
                        {
                            ImGui::Image(static_cast<ImTextureID>(imageTexture), ImVec2(previewWidth, previewWidth * 0.5625F));
                        }
                        else
                        {
                            ImGui::TextDisabled("(no image)");
                        }
                        ImGui::TextDisabled("%s", currentText.c_str());
                        if (ImGui::Button("Change Image..."))
                        {
                            std::string folder = std::filesystem::path(prop.defaultString).parent_path().generic_string();
                            if (folder.rfind("Game/", 0) != 0)
                            {
                                folder = "Game/Images";
                            }
                            if (const std::optional<std::filesystem::path> picked =
                                    showOpenImageDialog(nativeWindowHandle, projectRoot / folder))
                            {
                                if (const std::optional<std::string> imported = importFileIntoFolder(*picked, projectRoot, folder))
                                {
                                    commitText(*imported);
                                }
                                else
                                {
                                    logMessage(console, LogLevel::Error, "Could not copy that image into " + folder + ".");
                                }
                            }
                        }
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("Pick an image from your PC - it's copied into the game's own folder so it "
                                "ships with the game.");
                        }
                        if (!live && stored != nullptr)
                        {
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Reset"))
                            {
                                (void)commandBus.execute(
                                    SetPropertyCommand{entity.name, "ScriptPropertyReset", propertyKey, std::string()});
                            }
                        }
                    }
                    else if (prop.type == PropertyType::Icon)
                    {
                        // [thumbnail]  name
                        //              path
                        //              [Change Icon...] [Built-in v] [Reset]
                        const GLuint iconTexture = currentText.empty() ? 0 : ensureIconTextureGpu(currentText, projectRoot);
                        const ImVec2 thumbMin = ImGui::GetCursorScreenPos();
                        constexpr float kThumb = 56.0F;
                        ImGui::GetWindowDrawList()->AddRectFilled(
                            thumbMin, ImVec2(thumbMin.x + kThumb, thumbMin.y + kThumb), IM_COL32(25, 25, 30, 255), 4.0F);
                        if (iconTexture != 0)
                        {
                            ImGui::Image(static_cast<ImTextureID>(iconTexture), ImVec2(kThumb, kThumb));
                        }
                        else
                        {
                            ImGui::Dummy(ImVec2(kThumb, kThumb));
                        }
                        ImGui::GetWindowDrawList()->AddRect(
                            thumbMin, ImVec2(thumbMin.x + kThumb, thumbMin.y + kThumb), IM_COL32(120, 120, 130, 180), 4.0F);
                        ImGui::SameLine();
                        ImGui::BeginGroup();
                        ImGui::TextUnformatted(prop.name.c_str());
                        ImGui::PushTextWrapPos(0.0F);
                        ImGui::TextDisabled("%s", currentText.empty() ? "(no icon)" : currentText.c_str());
                        ImGui::PopTextWrapPos();
                        if (ImGui::Button("Change Icon..."))
                        {
                            if (const std::optional<std::filesystem::path> picked =
                                    showOpenImageDialog(nativeWindowHandle, projectRoot / "Game" / "Icons"))
                            {
                                if (const std::optional<std::string> imported = importIconIntoProject(*picked, projectRoot))
                                {
                                    commitText(*imported);
                                }
                                else
                                {
                                    logMessage(console, LogLevel::Error, "Could not import that icon into the project.");
                                }
                            }
                        }
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("Pick an image from your PC - it's copied into Game/Icons/ and used for this object.");
                        }
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(110.0F);
                        if (ImGui::BeginCombo("##BuiltInIcon", "Built-in...", ImGuiComboFlags_HeightLarge))
                        {
                            for (const std::string& iconPath : projectIconPaths(projectRoot))
                            {
                                const GLuint thumbnail = ensureIconTextureGpu(iconPath, projectRoot);
                                if (thumbnail != 0)
                                {
                                    ImGui::Image(static_cast<ImTextureID>(thumbnail), ImVec2(22.0F, 22.0F));
                                    ImGui::SameLine();
                                }
                                const std::string label = std::filesystem::path(iconPath).stem().string() + "##" + iconPath;
                                if (ImGui::Selectable(label.c_str(), iconPath == currentText))
                                {
                                    commitText(iconPath);
                                }
                                if (ImGui::IsItemHovered())
                                {
                                    ImGui::SetTooltip("%s", iconPath.c_str());
                                }
                            }
                            ImGui::EndCombo();
                        }
                        if (!live && stored != nullptr)
                        {
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Reset"))
                            {
                                (void)commandBus.execute(
                                    SetPropertyCommand{entity.name, "ScriptPropertyReset", propertyKey, std::string()});
                            }
                        }
                        ImGui::EndGroup();
                    }
                    else
                    {
                        std::array<char, 256> textBuffer{};
                        std::snprintf(textBuffer.data(), textBuffer.size(), "%s", currentText.c_str());
                        ImGui::InputText(prop.name.c_str(), textBuffer.data(), textBuffer.size());
                        if (ImGui::IsItemDeactivatedAfterEdit())
                        {
                            commitText(textBuffer.data());
                        }
                    }

                    // Right-click any (non-icon) field to put it back to the
                    // script's own default.
                    if (!live && stored != nullptr && prop.type != PropertyType::Icon && prop.type != PropertyType::Image &&
                        ImGui::BeginPopupContextItem("PropertyMenu"))
                    {
                        if (ImGui::MenuItem("Reset to script default"))
                        {
                            (void)commandBus.execute(
                                SetPropertyCommand{entity.name, "ScriptPropertyReset", propertyKey, std::string()});
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                ImGui::Unindent(15.0F);
            }

            // Last row of a preset script: link/unlink the rest of its set.
            if (!presetName.empty())
            {
                const ScriptRuntime::ScriptPresetTag tag = ScriptRuntime::cachedScriptPreset(projectRoot / scriptPath);
                const std::vector<std::string> linkable = presetLinkableScripts(projectRoot, presetName);
                if ((tag.role == "player" || tag.role == "damageable") && linkable.size() > 1)
                {
                    const int attached = static_cast<int>(std::count_if(linkable.begin(), linkable.end(),
                        [&entity](const std::string& path) { return hasScript(entity, path); }));
                    bool linked = attached == static_cast<int>(linkable.size());
                    ImGui::Indent(15.0F);
                    const std::string label = "Link all " + presetName + " scripts (" + std::to_string(attached) + "/" +
                        std::to_string(linkable.size()) + ")";
                    if (ImGui::Checkbox(label.c_str(), &linked))
                    {
                        for (const std::string& other : linkable)
                        {
                            if (linked && !hasScript(entity, other))
                            {
                                (void)commandBus.execute(AttachScriptCommand{entity.name, other});
                            }
                            else if (!linked && other != scriptPath && hasScript(entity, other))
                            {
                                (void)commandBus.execute(DetachScriptCommand{entity.name, other});
                                if (scriptRuntime.isRunning())
                                {
                                    scriptRuntime.stopScript(entity.id, other);
                                }
                            }
                        }
                        if (linked && hasScript(entity, gameforger::editor::fpsdemo::kPlayer))
                        {
                            (void)commandBus.execute(SetPropertyCommand{entity.name, "ScriptProperty",
                                std::string(std::string(gameforger::editor::fpsdemo::kHealth) + "#is_player"), std::string("true")});
                        }
                        logMessage(console, LogLevel::Info,
                            linked ? presetName + ": linked every compatible script to '" + entity.name + "'."
                                   : presetName + ": unlinked the other preset scripts from '" + entity.name + "'.");
                    }
                    if (ImGui::IsItemHovered())
                    {
                        std::string tooltip = "All of these must be on this object for the preset to work at its best:";
                        for (const std::string& path : linkable)
                        {
                            tooltip += "\n  " + std::string(hasScript(entity, path) ? "[x] " : "[ ] ") + path;
                        }
                        tooltip += "\nUntick to remove the others (keeps this one).";
                        ImGui::SetTooltip("%s", tooltip.c_str());
                    }
                    ImGui::Unindent(15.0F);
                }
            }

            ImGui::PopID();
        }
        if (ImGui::Button("Add Script..."))
        {
            scriptCreator.requestOpen = true;
            scriptCreator.targetEntityName = entity.name;
            scriptCreator.existingPath.fill('\0');
            scriptCreator.scriptName.fill('\0');
            scriptCreator.description.fill('\0');
            scriptCreator.attachStatus.clear();
            scriptCreator.previewSynced = false;
            scriptCreator.resultLogged = false;
            {
                const std::lock_guard<std::mutex> lock(scriptCreator.resultMutex);
                scriptCreator.hasResult = false;
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Camera Rig");
        ImGui::TextDisabled(
            "Used by scripts that call self.camera:setMode(\"fps\"|\"third_person\") on this object (e.g. the "
            "FPS/Third-Person presets) to position the Game view camera during Play.");

        float fpsEyeHeight = entity.cameraRig.fpsEyeHeight;
        if (ImGui::DragFloat("FPS Eye Height", &fpsEyeHeight, 0.02F, 0.0F, 5.0F))
        {
            commandBus.execute(SetPropertyCommand{entity.name, "Camera", "fpsEyeHeight", fpsEyeHeight});
        }

        float thirdPersonDistance = entity.cameraRig.thirdPersonDistance;
        if (ImGui::DragFloat("3rd-Person Distance", &thirdPersonDistance, 0.05F, 0.5F, 20.0F))
        {
            commandBus.execute(
                SetPropertyCommand{entity.name, "Camera", "thirdPersonDistance", thirdPersonDistance});
        }

        float thirdPersonHeight = entity.cameraRig.thirdPersonHeight;
        if (ImGui::DragFloat("3rd-Person Height", &thirdPersonHeight, 0.05F, -5.0F, 10.0F))
        {
            commandBus.execute(SetPropertyCommand{entity.name, "Camera", "thirdPersonHeight", thirdPersonHeight});
        }

        float thirdPersonAimHeight = entity.cameraRig.thirdPersonAimHeight;
        if (ImGui::DragFloat("3rd-Person Aim Height", &thirdPersonAimHeight, 0.05F, -5.0F, 10.0F))
        {
            commandBus.execute(
                SetPropertyCommand{entity.name, "Camera", "thirdPersonAimHeight", thirdPersonAimHeight});
        }

        float thirdPersonYawOffset = entity.cameraRig.thirdPersonYawOffsetDegrees;
        if (ImGui::DragFloat("3rd-Person Angle", &thirdPersonYawOffset, 1.0F, -180.0F, 180.0F, "%.0f deg"))
        {
            commandBus.execute(
                SetPropertyCommand{entity.name, "Camera", "thirdPersonYawOffsetDegrees", thirdPersonYawOffset});
        }

        bool lockCursor = entity.cameraRig.lockCursor;
        if (ImGui::Checkbox("Lock Cursor", &lockCursor))
        {
            commandBus.execute(SetPropertyCommand{entity.name, "Camera", "lockCursor", lockCursor});
        }
        ImGui::TextDisabled(
            "While Play is running and this object has claimed the Game view camera (either FPS or "
            "Third-Person): hides and captures the cursor for continuous mouse-look instead of needing "
            "Right Mouse held, and shows a center crosshair. Esc releases it temporarily without "
            "stopping Play.");

        ImGui::Separator();
        ImGui::TextUnformatted("Animation");
        bool animateObject = entity.animation.enabled;
        if (ImGui::Checkbox("Animate Object", &animateObject))
        {
            if (SceneEntity* mutableEntity = scene.findEntityMutable(entity.id))
            {
                mutableEntity->animation.enabled = animateObject;
            }
        }
        if (entity.animation.enabled)
        {
            ImGui::TextDisabled(
                "%d keyframe(s) for '%s' - switch to the Animation tab, click Start Recording, then pose it here "
                "or with the gizmo.",
                static_cast<int>(entity.animation.keyframes.size()),
                entity.name.c_str());
        }

        ImGui::Separator();
        if (ImGui::Button("Delete Entity"))
        {
            commandBus.execute(DeleteEntityCommand{entity.name});
            clearSelection(selection);
        }

        ImGui::End();

        if (scriptCreator.requestOpen)
        {
            ImGui::OpenPopup("Add Script");
            scriptCreator.requestOpen = false;
        }
        if (ImGui::BeginPopupModal("Add Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Entity: %s", scriptCreator.targetEntityName.c_str());
            ImGui::Separator();

            ImGui::TextUnformatted("Attach an existing script");
            ImGui::InputText("Script path", scriptCreator.existingPath.data(), scriptCreator.existingPath.size());
            ImGui::SameLine();
            if (ImGui::Button("Attach##existing"))
            {
                const AICommandResult result = commandBus.execute(
                    AttachScriptCommand{scriptCreator.targetEntityName, scriptCreator.existingPath.data()});
                scriptCreator.attachStatus = result.message;
                scriptCreator.attachStatusSuccess = result.success;
                logMessage(console, result.success ? LogLevel::Info : LogLevel::Error, result.message);
                if (result.success)
                {
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!scriptCreator.attachStatus.empty() && !scriptCreator.attachStatusSuccess)
            {
                ImGui::TextColored(
                    ImVec4(0.95F, 0.35F, 0.35F, 1.0F), "%s", scriptCreator.attachStatus.c_str());
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Presets: controllers & collider");
            ImGui::TextDisabled(
                "Pick one or more from the dropdown - \"Add Selected Presets\" down by Close applies them, "
                "no need to touch the AI section below. Controllers also mark this object as a Collider.");
            enum class PresetKind
            {
                MovementController,
                Rigidbody,
                ColliderOnly,
                // A real script, unlike ColliderOnly, but not a physics/
                // movement driver either - doesn't call self.entity:
                // setPosition() or self.physics:resolve(), so it never
                // fights a MovementController/Rigidbody for the entity's
                // transform and stays free to combine with either (or
                // neither) exactly like ColliderOnly does below.
                Utility,
                // enemy_ai.lua DOES call self.entity:setPosition() every
                // frame (wander/chase movement), same as a
                // MovementController - so it's exclusive with those too,
                // just its own kind rather than reusing MovementController
                // (which is worded/described as player-input-driven).
                EnemyAI,
                // A plain marker script (items.lua, health.lua) - attaches
                // without turning the Collider on.
                Marker,
                // Several scripts at once (the whole FPS Demo player set).
                Bundle
            };
            struct ScriptPreset
            {
                const char* label;
                const char* path; // unused for ColliderOnly
                const char* description;
                PresetKind kind;
            };
            constexpr std::array<ScriptPreset, 16> presets{{
                    {"FPS Controller",
                     "Game/Scripts/fps_controller.lua",
                     "WASD move, Space jump, Shift sprint, mouse-look. First-person camera by default - "
                     "press C in Play to swap to third-person. Also marks this object as a Collider.",
                     PresetKind::MovementController},
                    {"Third-Person Controller",
                     "Game/Scripts/third_person_controller.lua",
                     "Same movement/sprint/jump/mouse-look, plus the capsule turns to face where it's "
                     "moving. Third-person camera by default - press C to swap to first-person. Also marks "
                     "this object as a Collider.",
                     PresetKind::MovementController},
                    {"Rigidbody",
                     "Game/Scripts/rigidbody.lua",
                     "Falls under gravity and lands on/collides with Collider-enabled objects - for props "
                     "like a cube you want to fall and stay put. Also marks this object as a Collider. Not "
                     "for a player character - use FPS/Third-Person Controller instead, which already "
                     "include their own gravity and ground collision.",
                     PresetKind::Rigidbody},
                    {"Collider Only",
                     nullptr,
                     "No script - just marks this object solid, so an FPS/Third-Person/Rigidbody script's "
                     "self.physics:resolve() collides with it. Use for ground, walls, and platforms.",
                     PresetKind::ColliderOnly},
                    {"Inventory & Pickup",
                     "Game/Scripts/inventory_system.lua",
                     "E picks up any nearby \"Is Pickup Item\" object (Inspector), I opens the inventory "
                     "grid. Attach to the player alongside FPS/Third-Person Controller - has no effect on "
                     "its own without one of those claiming the camera. pickup_range/pickup_height_tolerance "
                     "are editable in the script itself.",
                     PresetKind::Utility},
                    {"Enemy AI",
                     "Game/Scripts/enemy_ai.lua",
                     "Wanders near its spawn point until an object tagged target_tag (default \"Player\") "
                     "comes within search_radius, then chases it until it escapes escape_radius. Shows the "
                     "red detection icon while chasing. All radii/speeds/target_tag are editable in the "
                     "script itself. Also marks this object as a Collider.",
                     PresetKind::EnemyAI},
                    {"Ranged Attacker",
                     "Game/Scripts/ranged_attacker.lua",
                     "Fires projectiles at the nearest object tagged target_tag once it's within fire_range "
                     "- attach to an enemy (target_tag=\"Player\"), the player (target_tag=\"Enemy\"), or a "
                     "future tower (either). Optional muzzle_tag names a separate tagged entity to fire "
                     "from (e.g. \"player_gun_muzzle\") instead of this object's own position - just a "
                     "regular Inspector tag, no extra setup needed. Needs a target_tag that actually exists "
                     "in the scene to do anything. Also marks this object as a Collider - fine for an enemy "
                     "or tower, uncheck it after applying if used on something that shouldn't block movement.",
                     PresetKind::Utility},
                    {"Catapult Controller",
                     "Game/Scripts/catapult_controller.lua",
                     "Attach to a catapult's base (needs Is Catapult + a \"PlayerCatapult\" tag, Inspector). "
                     "F grabs/tows it toward you, F again drops it; walk up and press E to load/aim, mouse to "
                     "aim, hold T to power up the throw, R to fire. Finds its own arm by proximity to an "
                     "entity tagged \"CatapultArm\" - tag your imported arm object with that. All ranges/"
                     "speeds/power-charge-time are editable in the script itself.",
                     PresetKind::Utility},
                    // ---- FPS Demo preset (drawn under its own separator) ----
                    {"FPS Demo - full player set",
                     nullptr,
                     "Everything the player needs, in one go: fps_player.lua (movement, camera, inventory, 11 "
                     "weapons + hands), projectiles.lua (spell projectiles), effects.lua (particles), "
                     "xp_system.lua (XP, weapon levels, caster unlocks) and health.lua (health bar, healing, "
                     "respawn). The hands rig comes from GameObject > FPS Player (Hands + Weapons).",
                     PresetKind::Bundle},
                    {"FPS Player (fps_player.lua)",
                     gameforger::editor::fpsdemo::kPlayer,
                     "Movement, camera, inventory (E / I / 1-8) and the weapons with first-person hands. Works "
                     "alone, but spells, effects, XP and healing need the rest of the FPS Demo set.",
                     PresetKind::MovementController},
                    {"Projectiles (projectiles.lua)",
                     gameforger::editor::fpsdemo::kProjectiles,
                     "Spell projectiles for the Storm/Fire/Frost Casters - chain lightning, explosions + burning, "
                     "freezing. Goes on the player.",
                     PresetKind::Marker},
                    {"Effects (effects.lua)",
                     gameforger::editor::fpsdemo::kEffects,
                     "Particles: spell casts, trails, elemental impacts, heals, level-ups, crits. Goes on the player.",
                     PresetKind::Marker},
                    {"XP System (xp_system.lua)",
                     gameforger::editor::fpsdemo::kXpSystem,
                     "XP from every hit; weapon levels boost damage, fire rate, accuracy, ammo and crits; player "
                     "levels unlock the Fire, Frost and Life Casters. Goes on the player.",
                     PresetKind::Marker},
                    {"Health (health.lua)",
                     gameforger::editor::fpsdemo::kHealth,
                     "Lets weapons damage this object; health bar, damage numbers, burning/frozen states. On the "
                     "player, tick is_player for the HUD bar, healing and respawn.",
                     PresetKind::Marker},
                    {"Inventory Item (items.lua)",
                     gameforger::editor::fpsdemo::kItems,
                     "Makes this object pick-up-able with E into the inventory. Set its name, icon (Change "
                     "Icon... picks one from your PC), type and weapon in the Scripts section afterwards.",
                     PresetKind::Marker},
                    {"Game Manager (game_manager.lua)",
                     gameforger::editor::fpsdemo::kGameManager,
                     "The built game's title, splash logo (Change Image...) and intro message - put it on one "
                     "object per scene (or use GameObject > Game Manager).",
                     PresetKind::Marker},
                }};

            static std::array<bool, presets.size()> presetSelected{};

            std::string presetsPreview;
            for (std::size_t index = 0; index < presets.size(); ++index)
            {
                if (presetSelected[index])
                {
                    presetsPreview += (presetsPreview.empty() ? "" : ", ") + std::string(presets[index].label);
                }
            }
            if (presetsPreview.empty())
            {
                presetsPreview = "Select presets...";
            }

            if (ImGui::BeginCombo("##PresetsCombo", presetsPreview.c_str()))
            {
                for (std::size_t index = 0; index < presets.size(); ++index)
                {
                    ImGui::PushID(static_cast<int>(index));
                    const bool wasSelected = presetSelected[index];
                    const auto isExclusive = [](const PresetKind kind)
                    {
                        return kind == PresetKind::MovementController || kind == PresetKind::Rigidbody ||
                            kind == PresetKind::EnemyAI || kind == PresetKind::Bundle;
                    };
                    if (presets[index].kind == PresetKind::Bundle)
                    {
                        ImGui::SeparatorText("FPS Demo preset");
                    }
                    if (ImGui::Checkbox(presets[index].label, &presetSelected[index]) && !wasSelected &&
                        isExclusive(presets[index].kind))
                    {
                        // At most one physics-driving preset (a movement controller
                        // OR Rigidbody) at a time - each runs its own independent
                        // gravity/ground-collision every frame and calls
                        // self.entity:setPosition(), so attaching more than one to
                        // the same object makes them fight over its transform every
                        // frame - symptoms: frozen/jittery movement, broken jump,
                        // and a third-person camera that looks like it isn't
                        // following. Collider Only and Utility (e.g. Inventory &
                        // Pickup) never touch the transform, so they stay free to
                        // combine with any of these.
                        for (std::size_t other = 0; other < presets.size(); ++other)
                        {
                            if (other != index && isExclusive(presets[other].kind))
                            {
                                presetSelected[other] = false;
                            }
                        }
                    }
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 380.0F);
                    ImGui::TextDisabled("%s", presets[index].description);
                    ImGui::PopTextWrapPos();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Generate a new script with AI");
            ImGui::InputText("Script name", scriptCreator.scriptName.data(), scriptCreator.scriptName.size());
            ImGui::InputTextMultiline(
                "Describe the behavior",
                scriptCreator.description.data(),
                scriptCreator.description.size(),
                ImVec2(400.0F, 100.0F));

            const bool isGenerating = scriptCreator.generating.load();
            if (isGenerating)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Generate"))
            {
                if (scriptCreator.worker.joinable())
                {
                    scriptCreator.worker.join();
                }
                {
                    const std::lock_guard<std::mutex> lock(scriptCreator.resultMutex);
                    scriptCreator.hasResult = false;
                }
                scriptCreator.previewSynced = false;
                scriptCreator.resultLogged = false;
                scriptCreator.generating = true;

                const std::string entityName = scriptCreator.targetEntityName;
                const std::string description = scriptCreator.description.data();
                const std::string providerId = activeProviderId;
                scriptCreator.worker = std::thread(
                    [&scriptCreator, &aiProviderClient, entityName, description, providerId]()
                    {
                        const ScriptGenerationResult result =
                            generateEntityScript(aiProviderClient, providerId, entityName, description);
                        const std::lock_guard<std::mutex> lock(scriptCreator.resultMutex);
                        scriptCreator.resultSuccess = result.success;
                        scriptCreator.resultContent = result.content;
                        scriptCreator.hasResult = true;
                        scriptCreator.generating = false;
                    });
            }
            if (isGenerating)
            {
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("Generating...");
            }

            bool hasResultNow = false;
            bool resultSuccessNow = false;
            std::string resultContentNow;
            {
                const std::lock_guard<std::mutex> lock(scriptCreator.resultMutex);
                hasResultNow = scriptCreator.hasResult;
                resultSuccessNow = scriptCreator.resultSuccess;
                resultContentNow = scriptCreator.resultContent;
            }

            if (hasResultNow && !scriptCreator.resultLogged)
            {
                scriptCreator.resultLogged = true;
                logMessage(
                    console,
                    resultSuccessNow ? LogLevel::Info : LogLevel::Error,
                    resultSuccessNow ? "AI script generated for '" + scriptCreator.targetEntityName + "'."
                                      : "AI script generation failed: " + resultContentNow);
            }

            if (hasResultNow)
            {
                ImGui::Separator();
                if (resultSuccessNow)
                {
                    if (!scriptCreator.previewSynced)
                    {
                        std::snprintf(
                            scriptCreator.previewBuffer.data(),
                            scriptCreator.previewBuffer.size(),
                            "%s",
                            resultContentNow.c_str());
                        scriptCreator.previewSynced = true;
                    }
                    ImGui::InputTextMultiline(
                        "##GeneratedScript",
                        scriptCreator.previewBuffer.data(),
                        scriptCreator.previewBuffer.size(),
                        ImVec2(480.0F, 220.0F));

                    if (ImGui::Button("Save & Attach"))
                    {
                        const std::string path = makeUniqueScriptPath(projectRoot, scriptCreator.scriptName.data());
                        const AICommandResult createResult = commandBus.execute(
                            CreateScriptCommand{path, "lua", std::string(scriptCreator.previewBuffer.data())});
                        if (createResult.success)
                        {
                            (void)commandBus.execute(AttachScriptCommand{scriptCreator.targetEntityName, path});
                        }
                        {
                            const std::lock_guard<std::mutex> lock(scriptCreator.resultMutex);
                            scriptCreator.hasResult = false;
                        }
                        scriptCreator.previewSynced = false;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Discard"))
                    {
                        const std::lock_guard<std::mutex> lock(scriptCreator.resultMutex);
                        scriptCreator.hasResult = false;
                        scriptCreator.previewSynced = false;
                    }
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.95F, 0.35F, 0.35F, 1.0F), "%s", resultContentNow.c_str());
                }
            }

            ImGui::Separator();
            {
                const bool anyPresetSelected =
                    std::any_of(presetSelected.begin(), presetSelected.end(), [](const bool s) { return s; });
                if (!anyPresetSelected)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Add Selected Presets"))
                {
                    bool anySuccess = false;
                    for (std::size_t index = 0; index < presets.size(); ++index)
                    {
                        if (!presetSelected[index])
                        {
                            continue;
                        }
                        const ScriptPreset& preset = presets[index];
                        AICommandResult result{true, false, ""};
                        if (preset.kind == PresetKind::Bundle)
                        {
                            int attachedCount = 0;
                            for (const std::string& script : fpsDemoPlayerScripts())
                            {
                                if (commandBus.execute(AttachScriptCommand{scriptCreator.targetEntityName, script}).success)
                                {
                                    ++attachedCount;
                                }
                            }
                            (void)commandBus.execute(SetPropertyCommand{scriptCreator.targetEntityName, "ScriptProperty",
                                std::string(std::string(gameforger::editor::fpsdemo::kHealth) + "#is_player"), std::string("true")});
                            (void)commandBus.execute(SetPropertyCommand{scriptCreator.targetEntityName, "ScriptProperty",
                                std::string(std::string(gameforger::editor::fpsdemo::kHealth) + "#destroy_on_death"), std::string("false")});
                            result = {attachedCount > 0, false,
                                "Attached " + std::to_string(attachedCount) + " FPS Demo script(s)."};
                        }
                        else if (preset.kind != PresetKind::ColliderOnly)
                        {
                            result = commandBus.execute(
                                AttachScriptCommand{scriptCreator.targetEntityName, preset.path});
                            if (result.success && preset.kind != PresetKind::Marker)
                            {
                                commandBus.execute(SetPropertyCommand{
                                    scriptCreator.targetEntityName, "Collider", "enabled", true});
                            }
                        }
                        else
                        {
                            result = commandBus.execute(SetPropertyCommand{
                                scriptCreator.targetEntityName, "Collider", "enabled", true});
                        }
                        logMessage(
                            console,
                            result.success ? LogLevel::Info : LogLevel::Error,
                            std::string(preset.label) + ": " + result.message);
                        anySuccess = anySuccess || result.success;
                        presetSelected[index] = false;
                    }
                    scriptCreator.attachStatus =
                        anySuccess ? "Preset(s) applied." : "No presets applied - see Console for details.";
                    scriptCreator.attachStatusSuccess = anySuccess;
                    if (anySuccess)
                    {
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!anyPresetSelected)
                {
                    ImGui::EndDisabled();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Close"))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (scriptEditor.requestOpen)
        {
            ImGui::OpenPopup("Edit Script");
            scriptEditor.requestOpen = false;
        }
        if (ImGui::BeginPopupModal("Edit Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("File: %s", scriptEditor.scriptPath.c_str());
            ImGui::Separator();
            ImGui::InputTextMultiline(
                "##EditScriptContent",
                scriptEditor.buffer.data(),
                scriptEditor.buffer.size(),
                ImVec2(560.0F, 320.0F));

            if (ImGui::Button("Save"))
            {
                const AICommandResult saveResult = commandBus.execute(
                    CreateScriptCommand{scriptEditor.scriptPath, "lua", std::string(scriptEditor.buffer.data())});
                scriptEditor.status = saveResult.message;
                scriptEditor.statusSuccess = saveResult.success;
                if (saveResult.success)
                {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Close"))
            {
                ImGui::CloseCurrentPopup();
            }
            if (!scriptEditor.status.empty())
            {
                ImGui::TextColored(
                    scriptEditor.statusSuccess ? ImVec4(0.35F, 0.85F, 0.45F, 1.0F) : ImVec4(0.95F, 0.35F, 0.35F, 1.0F),
                    "%s",
                    scriptEditor.status.c_str());
            }
            ImGui::EndPopup();
        }
    }

    Ray computeMouseRay(
        const ImVec2& mouseScreenPos,
        const ImVec2& viewportOrigin,
        const ImVec2& viewportSize,
        const glm::mat4& view,
        const glm::mat4& projection)
    {
        const float ndcX = ((mouseScreenPos.x - viewportOrigin.x) / viewportSize.x) * 2.0F - 1.0F;
        const float ndcY = 1.0F - ((mouseScreenPos.y - viewportOrigin.y) / viewportSize.y) * 2.0F;

        const glm::mat4 inverseViewProjection = glm::inverse(projection * view);
        glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, -1.0F, 1.0F);
        glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0F, 1.0F);
        nearPoint /= nearPoint.w;
        farPoint /= farPoint.w;

        Ray ray;
        ray.origin = glm::vec3(nearPoint);
        ray.direction = glm::normalize(glm::vec3(farPoint - nearPoint));
        return ray;
    }

    // Every primitive is authored inside a [-1,1] local box, so a single
    // uniform slab test works for all of them - EXCEPT text meshes, whose
    // local geometry is sized directly in world units from the font/string
    // (see TextMesh.hpp) and can be any shape. For those, actually build the
    // mesh and use its real bounds. Picking is click-driven (not per-frame),
    // so re-triangulating on click is cheap enough to not need its own cache.
    std::pair<glm::vec3, glm::vec3> entityLocalBounds(const SceneEntity& entity, const std::filesystem::path& projectRoot)
    {
        if (entity.isTerrain)
        {
            // Approximate box (ignores exact per-vertex heights, like every
            // other bounds check here) - real per-texel accuracy is only
            // needed for the sculpt brush's own raycast (terrainRaycastHit),
            // not for click-to-select.
            const float half = entity.terrain.worldSize * 0.5F;
            const float halfHeight = entity.terrain.heightScale * 0.5F;
            return {glm::vec3(-half, -halfHeight, -half), glm::vec3(half, halfHeight, half)};
        }
        const auto boundsFromVertices =
            [](const std::vector<float>& vertices, const std::size_t stride) -> std::pair<glm::vec3, glm::vec3>
        {
            if (vertices.empty())
            {
                return {glm::vec3(-0.01F), glm::vec3(0.01F)};
            }
            glm::vec3 minBounds(std::numeric_limits<float>::max());
            glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
            for (std::size_t i = 0; i < vertices.size(); i += stride)
            {
                const glm::vec3 point(vertices[i], vertices[i + 1], vertices[i + 2]);
                minBounds = glm::min(minBounds, point);
                maxBounds = glm::max(maxBounds, point);
            }
            return {minBounds, maxBounds};
        };

        if (entity.isImportedMesh)
        {
            const ModelImportResult built = loadModelMesh(projectRoot / entity.importedMesh.sourcePath);
            return boundsFromVertices(built.vertices, static_cast<std::size_t>(built.vertexStride));
        }
        if (!entity.isTextMesh)
        {
            return {glm::vec3(-1.0F), glm::vec3(1.0F)};
        }
        const TextMeshBuildResult built = buildTextMesh(
            projectRoot / entity.textMesh.fontPath, entity.textMesh.content, entity.textMesh.fontSize,
            entity.textMesh.depth);
        return boundsFromVertices(built.vertices, 6);
    }

    std::optional<int> pickEntity(
        const Ray& worldRay, const EditorScene& scene, const std::filesystem::path& projectRoot)
    {
        std::optional<int> bestId;
        float bestDistance = std::numeric_limits<float>::max();

        for (const SceneEntity& entity : scene.entities())
        {
            // What you can't see, you can't click (hidden objects, items
            // sitting in an inventory, anything under a hidden parent).
            if (!scene.isActiveInHierarchy(entity))
            {
                continue;
            }
            const glm::mat4 model = composeEntityTransform(entity);
            const glm::mat4 inverseModel = glm::inverse(model);
            const glm::vec3 localOrigin = glm::vec3(inverseModel * glm::vec4(worldRay.origin, 1.0F));
            const glm::vec3 localDirection = glm::vec3(inverseModel * glm::vec4(worldRay.direction, 0.0F));
            // An "Empty" group node isn't drawn - give it a small grab box
            // at its origin instead of the full [-1,1] cube.
            const auto [localMin, localMax] = hasTag(entity, "Empty")
                ? std::pair<glm::vec3, glm::vec3>{glm::vec3(-0.12F), glm::vec3(0.12F)}
                : entityLocalBounds(entity, projectRoot);

            float tMin = 0.0F;
            float tMax = std::numeric_limits<float>::max();
            bool hit = true;
            for (int axis = 0; axis < 3 && hit; ++axis)
            {
                const float origin = localOrigin[axis];
                const float direction = localDirection[axis];
                if (std::abs(direction) < 1e-6F)
                {
                    if (origin < localMin[axis] || origin > localMax[axis])
                    {
                        hit = false;
                    }
                    continue;
                }
                float t1 = (localMin[axis] - origin) / direction;
                float t2 = (localMax[axis] - origin) / direction;
                if (t1 > t2)
                {
                    std::swap(t1, t2);
                }
                tMin = std::max(tMin, t1);
                tMax = std::min(tMax, t2);
                if (tMin > tMax)
                {
                    hit = false;
                }
            }

            if (hit && tMin < bestDistance)
            {
                bestDistance = tMin;
                bestId = entity.id;
            }
        }

        return bestId;
    }

    // Thin wrapper around Terrain.hpp's shared sampleTerrainHeight (single
    // source of truth for the heights[]-to-world-Y mapping, also used by
    // ScriptRuntime.cpp for terrain-aware physics grounding) - just unpacks
    // TerrainData's fields, since every caller here already has one of
    // those instead of the loose parameters the shared function takes.
    float sampleTerrainHeight(const TerrainData& terrain, const float localX, const float localZ)
    {
        return gameforger::editor::sampleTerrainHeight(
            terrain.resolution, terrain.worldSize, terrain.heightScale, terrain.heights, localX, localZ);
    }

    // Raymarches `worldRay` against `entity`'s heightmap surface (not just
    // its bounding box, unlike pickEntity) and bisects to refine the hit -
    // the standard heightfield raycasting technique. Returns the world-space
    // hit point, or nullopt if the ray never crosses the surface within the
    // terrain's footprint.
    std::optional<glm::vec3> terrainRaycastHit(const Ray& worldRay, const SceneEntity& entity)
    {
        if (!entity.isTerrain || entity.terrain.heights.empty())
        {
            return std::nullopt;
        }
        const glm::mat4 model = composeEntityTransform(entity);
        const glm::mat4 inverseModel = glm::inverse(model);
        const glm::vec3 localOrigin = glm::vec3(inverseModel * glm::vec4(worldRay.origin, 1.0F));
        const glm::vec3 localDirection =
            glm::normalize(glm::vec3(inverseModel * glm::vec4(worldRay.direction, 0.0F)));

        const TerrainData& terrain = entity.terrain;
        const float half = terrain.worldSize * 0.5F;
        const float maxDistance = terrain.worldSize * 3.0F + terrain.heightScale * 4.0F + 50.0F;
        constexpr int steps = 200;
        const float stepSize = maxDistance / static_cast<float>(steps);

        float previousT = 0.0F;
        float previousDiff =
            localOrigin.y - sampleTerrainHeight(terrain, localOrigin.x, localOrigin.z);
        for (int step = 1; step <= steps; ++step)
        {
            const float t = static_cast<float>(step) * stepSize;
            const glm::vec3 point = localOrigin + localDirection * t;
            const float diff = point.y - sampleTerrainHeight(terrain, point.x, point.z);
            const bool withinFootprint = point.x >= -half && point.x <= half && point.z >= -half && point.z <= half;
            if (withinFootprint && diff <= 0.0F && previousDiff > 0.0F)
            {
                float lo = previousT;
                float hi = t;
                glm::vec3 hitPoint = point;
                for (int bisect = 0; bisect < 12; ++bisect)
                {
                    const float mid = (lo + hi) * 0.5F;
                    hitPoint = localOrigin + localDirection * mid;
                    const float midDiff = hitPoint.y - sampleTerrainHeight(terrain, hitPoint.x, hitPoint.z);
                    if (midDiff > 0.0F)
                    {
                        lo = mid;
                    }
                    else
                    {
                        hi = mid;
                    }
                }
                return glm::vec3(model * glm::vec4(hitPoint, 1.0F));
            }
            previousT = t;
            previousDiff = diff;
        }
        return std::nullopt;
    }

    // Raises (or, if `lower`, lowers) every heightmap sample within
    // `brushRadius` world units of `hitPointWorld`, with a linear falloff to
    // zero at the brush edge. Mutates the entity directly via
    // findEntityMutable rather than going through the command bus - same
    // "continuous per-frame editor bookkeeping" precedent as Animation
    // keyframe recording and Storyboard shot reordering, since routing every
    // single vertex edit of every paint frame through a validated/undoable
    // command would be both unnecessary and slow.
    void sculptTerrain(
        EditorScene& scene,
        const int entityId,
        const glm::vec3& hitPointWorld,
        const float brushRadius,
        const float strength,
        const bool lower,
        const float deltaTime)
    {
        SceneEntity* entity = scene.findEntityMutable(entityId);
        if (entity == nullptr || !entity->isTerrain)
        {
            return;
        }
        TerrainData& terrain = entity->terrain;
        const glm::mat4 inverseModel = glm::inverse(composeEntityTransform(*entity));
        const glm::vec3 localHit = glm::vec3(inverseModel * glm::vec4(hitPointWorld, 1.0F));
        const float half = terrain.worldSize * 0.5F;
        const float cellSpacing = terrain.worldSize / static_cast<float>(terrain.resolution - 1);
        const int radiusCells = static_cast<int>(std::ceil(brushRadius / cellSpacing)) + 1;

        const float u = std::clamp((localHit.x + half) / terrain.worldSize, 0.0F, 1.0F);
        const float v = std::clamp((half - localHit.z) / terrain.worldSize, 0.0F, 1.0F);
        const int centerCol = static_cast<int>(std::round(u * static_cast<float>(terrain.resolution - 1)));
        const int centerRow = static_cast<int>(std::round(v * static_cast<float>(terrain.resolution - 1)));

        const float delta = (lower ? -1.0F : 1.0F) * strength * deltaTime;
        const int rowStart = std::max(0, centerRow - radiusCells);
        const int rowEnd = std::min(terrain.resolution - 1, centerRow + radiusCells);
        const int colStart = std::max(0, centerCol - radiusCells);
        const int colEnd = std::min(terrain.resolution - 1, centerCol + radiusCells);
        for (int row = rowStart; row <= rowEnd; ++row)
        {
            for (int col = colStart; col <= colEnd; ++col)
            {
                const float worldX = static_cast<float>(col) * cellSpacing - half;
                const float worldZ = half - static_cast<float>(row) * cellSpacing;
                const float distance = glm::length(glm::vec2(worldX - localHit.x, worldZ - localHit.z));
                if (distance > brushRadius)
                {
                    continue;
                }
                const float falloff = 1.0F - (distance / brushRadius);
                const std::size_t index = static_cast<std::size_t>(row) * static_cast<std::size_t>(terrain.resolution) +
                    static_cast<std::size_t>(col);
                terrain.heights[index] = std::clamp(terrain.heights[index] + delta * falloff, 0.0F, 1.0F);
            }
        }
    }

    // Paints splat layer `layerIndex`'s weight up within `brushRadius` of
    // `hitPointWorld` (same raycast+falloff shape as sculptTerrain above),
    // renormalizing all 3 layer weights per texel so they keep summing to
    // 1 - the other two layers recede proportionally rather than the brush
    // just adding paint on top unbounded. Mutates splatWeights directly via
    // findEntityMutable, same "continuous per-frame editor bookkeeping"
    // precedent as sculptTerrain.
    void paintTerrainSplat(
        EditorScene& scene,
        const int entityId,
        const glm::vec3& hitPointWorld,
        const float brushRadius,
        const float strength,
        const int layerIndex,
        const float deltaTime)
    {
        SceneEntity* entity = scene.findEntityMutable(entityId);
        if (entity == nullptr || !entity->isTerrain || layerIndex < 0 || layerIndex > 2)
        {
            return;
        }
        TerrainData& terrain = entity->terrain;
        const std::size_t texelCount =
            static_cast<std::size_t>(terrain.resolution) * static_cast<std::size_t>(terrain.resolution);
        if (terrain.splatWeights.size() != texelCount * 3)
        {
            // Backward-compat: a terrain saved before splat painting
            // existed (or one whose weights array otherwise doesn't match
            // its own resolution) starts fully on layer 0, the same
            // default a freshly created terrain gets.
            terrain.splatWeights.assign(texelCount * 3, 0.0F);
            for (std::size_t texel = 0; texel < terrain.splatWeights.size(); texel += 3)
            {
                terrain.splatWeights[texel] = 1.0F;
            }
        }

        const glm::mat4 inverseModel = glm::inverse(composeEntityTransform(*entity));
        const glm::vec3 localHit = glm::vec3(inverseModel * glm::vec4(hitPointWorld, 1.0F));
        const float half = terrain.worldSize * 0.5F;
        const float cellSpacing = terrain.worldSize / static_cast<float>(terrain.resolution - 1);
        const int radiusCells = static_cast<int>(std::ceil(brushRadius / cellSpacing)) + 1;

        const float u = std::clamp((localHit.x + half) / terrain.worldSize, 0.0F, 1.0F);
        const float v = std::clamp((half - localHit.z) / terrain.worldSize, 0.0F, 1.0F);
        const int centerCol = static_cast<int>(std::round(u * static_cast<float>(terrain.resolution - 1)));
        const int centerRow = static_cast<int>(std::round(v * static_cast<float>(terrain.resolution - 1)));

        const int rowStart = std::max(0, centerRow - radiusCells);
        const int rowEnd = std::min(terrain.resolution - 1, centerRow + radiusCells);
        const int colStart = std::max(0, centerCol - radiusCells);
        const int colEnd = std::min(terrain.resolution - 1, centerCol + radiusCells);
        for (int row = rowStart; row <= rowEnd; ++row)
        {
            for (int col = colStart; col <= colEnd; ++col)
            {
                const float worldX = static_cast<float>(col) * cellSpacing - half;
                const float worldZ = half - static_cast<float>(row) * cellSpacing;
                const float distance = glm::length(glm::vec2(worldX - localHit.x, worldZ - localHit.z));
                if (distance > brushRadius)
                {
                    continue;
                }
                const float falloff = 1.0F - (distance / brushRadius);
                const std::size_t base =
                    (static_cast<std::size_t>(row) * static_cast<std::size_t>(terrain.resolution) +
                        static_cast<std::size_t>(col)) *
                    3;
                const float amount = std::clamp(strength * falloff * deltaTime, 0.0F, 1.0F);
                terrain.splatWeights[base + static_cast<std::size_t>(layerIndex)] = std::clamp(
                    terrain.splatWeights[base + static_cast<std::size_t>(layerIndex)] + amount, 0.0F, 1.0F);
                const float sum =
                    terrain.splatWeights[base] + terrain.splatWeights[base + 1] + terrain.splatWeights[base + 2];
                if (sum > 0.0001F)
                {
                    terrain.splatWeights[base] /= sum;
                    terrain.splatWeights[base + 1] /= sum;
                    terrain.splatWeights[base + 2] /= sum;
                }
            }
        }
    }

    glm::vec3 cameraOffsetDirection(const float yaw, const float pitch) noexcept
    {
        return glm::vec3(
            std::cos(pitch) * std::sin(yaw),
            std::sin(pitch),
            std::cos(pitch) * std::cos(yaw));
    }

    void updateCamera(
        EditorCameraState& camera,
        const bool canStartInteraction,
        const float deltaTime,
        const float minDistance = 1.5F)
    {
        ImGuiIO& io = ImGui::GetIO();

        if (camera.dragMode == CameraDragMode::None && canStartInteraction)
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            {
                camera.dragMode = io.KeyShift ? CameraDragMode::Pan : CameraDragMode::Orbit;
            }
            else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                // Right-click is a true FPS look, not pivot-orbit: Shift still pans.
                camera.dragMode = io.KeyShift ? CameraDragMode::Pan : CameraDragMode::Fly;
            }
        }

        const bool dragButtonDown =
            ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::IsMouseDown(ImGuiMouseButton_Right);
        if (!dragButtonDown)
        {
            camera.dragMode = CameraDragMode::None;
        }

        // Eye position under the OLD yaw/pitch, captured before any rotation this
        // frame — Fly mode needs this to keep the eye fixed while only the look
        // direction changes (Orbit intentionally swings the eye around target
        // instead, so it doesn't need this).
        const glm::vec3 eyeBeforeRotation =
            camera.target + camera.distance * cameraOffsetDirection(camera.yaw, camera.pitch);

        if (camera.dragMode == CameraDragMode::Orbit || camera.dragMode == CameraDragMode::Fly)
        {
            camera.yaw -= io.MouseDelta.x * 0.01F;
            camera.pitch += io.MouseDelta.y * 0.01F;
        }
        camera.pitch = std::clamp(camera.pitch, -1.45F, 1.45F);

        if (camera.dragMode == CameraDragMode::Fly)
        {
            // Re-solve target so the eye stays exactly where it was before the
            // look-direction change above — first-person rotation, not orbit.
            camera.target = eyeBeforeRotation - camera.distance * cameraOffsetDirection(camera.yaw, camera.pitch);
        }

        const glm::vec3 offsetDirection = cameraOffsetDirection(camera.yaw, camera.pitch);
        const glm::vec3 forward = -offsetDirection;
        // Y up, Z forward (right-handed): right = +X = cross(+Y, forward).
        // Was cross(forward, +Y) which returned -X and made middle-mouse-pan
        // feel reversed.
        const glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0.0F, 1.0F, 0.0F), forward));
        const glm::vec3 up = glm::cross(right, forward);

        if (camera.dragMode == CameraDragMode::Pan)
        {
            const float panSpeed = camera.distance * 0.0015F;
            camera.target -= right * io.MouseDelta.x * panSpeed;
            camera.target += up * io.MouseDelta.y * panSpeed;
        }

        if (canStartInteraction && io.MouseWheel != 0.0F)
        {
            camera.distance -= io.MouseWheel * camera.distance * 0.12F;
        }
        camera.distance = std::clamp(camera.distance, minDistance, 60.0F);

        // Deliberately NOT gated on `!io.WantCaptureKeyboard` here (unlike
        // the W/E/R/T/Y gizmo-mode hotkeys elsewhere, which should stay
        // silent while a text field is focused). dragMode only becomes
        // Fly/Orbit/Pan via an explicit right/middle-click that already
        // required the viewport to be hovered - that's an unambiguous
        // signal the user wants camera control right now. Dear ImGui only
        // clears a text field's focus on a LEFT click outside it, not a
        // right click, so WITH this guard, any previously-focused field
        // (AI Forge prompt, a rename box, the script editor...) would keep
        // WantCaptureKeyboard true and silently eat every WASD/QE press
        // while flying, even though the mouse button is actively held over
        // the viewport - reported as "WASD doesn't work even holding the
        // button," which is exactly what that looked like.
        if (camera.dragMode != CameraDragMode::None)
        {
            const float flySpeed = camera.distance * 1.2F * deltaTime;
            if (ImGui::IsKeyDown(ImGuiKey_W)) camera.target += forward * flySpeed;
            if (ImGui::IsKeyDown(ImGuiKey_S)) camera.target -= forward * flySpeed;
            if (ImGui::IsKeyDown(ImGuiKey_D)) camera.target += right * flySpeed;
            if (ImGui::IsKeyDown(ImGuiKey_A)) camera.target -= right * flySpeed;
            if (ImGui::IsKeyDown(ImGuiKey_E)) camera.target += glm::vec3(0.0F, 1.0F, 0.0F) * flySpeed;
            if (ImGui::IsKeyDown(ImGuiKey_Q)) camera.target -= glm::vec3(0.0F, 1.0F, 0.0F) * flySpeed;
        }
    }

    void upsertKeyframe(
        EditorScene& scene,
        const int entityId,
        const float time,
        const glm::vec3& position,
        const glm::vec3& rotation,
        const glm::vec3& scale)
    {
        SceneEntity* mutableEntity = scene.findEntityMutable(entityId);
        if (mutableEntity == nullptr)
        {
            return;
        }
        TransformKeyframe keyframe;
        keyframe.time = time;
        keyframe.position = position;
        keyframe.rotationEuler = rotation;
        keyframe.scale = scale;

        std::vector<TransformKeyframe>& keyframes = mutableEntity->animation.keyframes;
        const auto existing = std::find_if(
            keyframes.begin(),
            keyframes.end(),
            [&keyframe](const TransformKeyframe& candidate)
            {
                return std::abs(candidate.time - keyframe.time) < 0.001F;
            });
        if (existing != keyframes.end())
        {
            *existing = keyframe;
        }
        else
        {
            keyframes.push_back(keyframe);
            std::sort(
                keyframes.begin(),
                keyframes.end(),
                [](const TransformKeyframe& a, const TransformKeyframe& b) { return a.time < b.time; });
        }
    }

    void applyPose(AICommandBus& commandBus, const std::string& entityName, const AnimatedPose& pose)
    {
        commandBus.execute(SetPropertyCommand{entityName, "Transform", "position", pose.position});
        commandBus.execute(SetPropertyCommand{entityName, "Transform", "rotation", pose.rotationEuler});
        commandBus.execute(SetPropertyCommand{entityName, "Transform", "scale", pose.scale});
    }

    // Advances every OTHER animated entity (text/objects "attached" to the
    // cutscene by simply having their own animation enabled) to elapsedTime
    // on the cutscene's own timeline - mirrors tickPlayModeAnimations but is
    // driven by an explicit time instead of PlayModeState, keeping Cine
    // Camera preview/movie playback decoupled from real gameplay Play mode.
    // excludeEntityId skips the cine-camera itself, whose pose is driven
    // separately from the shot's own cameraPath (see the Cine Camera
    // Preview panel).
    void tickCineAnimatedEntities(
        EditorScene& scene, AICommandBus& commandBus, const float elapsedTime, const int excludeEntityId)
    {
        for (const SceneEntity& entity : scene.entities())
        {
            if (entity.id == excludeEntityId || !entity.animation.enabled || entity.animation.keyframes.empty())
            {
                continue;
            }
            applyPose(commandBus, entity.name, sampleAnimation(entity.animation, elapsedTime));
        }
    }

    void drawAiAnimationSection(
        EditorScene& scene,
        AICommandBus& commandBus,
        SelectionState& selection,
        AIAnimationState& aiAnimation,
        const AIProviderClient& aiProviderClient,
        const std::string& activeProviderId,
        ConsoleState& console)
    {
        ImGui::TextUnformatted("AI Animation");
        if (selection.multiSelectedIds.empty())
        {
            ImGui::TextDisabled("Select one or more objects (Ctrl/Shift-click for multiple) to generate an animation.");
            ImGui::Separator();
            return;
        }

        std::string namesLabel;
        for (const int id : selection.multiSelectedIds)
        {
            if (const SceneEntity* target = scene.findEntity(id))
            {
                if (!namesLabel.empty())
                {
                    namesLabel += ", ";
                }
                namesLabel += target->name;
            }
        }
        ImGui::TextWrapped("Targets: %s", namesLabel.c_str());
        ImGui::TextDisabled("e.g. \"make it swing open like a door\" or \"bounce them up and down twice\"");
        ImGui::InputTextMultiline(
            "##AIAnimationPrompt", aiAnimation.prompt.data(), aiAnimation.prompt.size(), ImVec2(-1.0F, 60.0F));

        const bool isGenerating = aiAnimation.generating.load();
        if (isGenerating)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Generate Animation with AI"))
        {
            std::vector<SceneEntity> targets;
            for (const int id : selection.multiSelectedIds)
            {
                if (const SceneEntity* target = scene.findEntity(id))
                {
                    targets.push_back(*target);
                }
            }
            if (aiAnimation.worker.joinable())
            {
                aiAnimation.worker.join();
            }
            aiAnimation.generating = true;
            aiAnimation.status = "Asking the AI...";
            {
                const std::lock_guard<std::mutex> lock(aiAnimation.resultMutex);
                aiAnimation.hasResult = false;
            }
            const std::string promptText = aiAnimation.prompt.data();
            const std::string providerId = activeProviderId;
            aiAnimation.worker = std::thread(
                [&aiAnimation, &aiProviderClient, targets, promptText, providerId]()
                {
                    AIAnimationResult result = generateAnimation(aiProviderClient, providerId, targets, promptText);
                    const std::lock_guard<std::mutex> lock(aiAnimation.resultMutex);
                    aiAnimation.result = std::move(result);
                    aiAnimation.hasResult = true;
                    aiAnimation.generating = false;
                });
        }
        if (isGenerating)
        {
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("Generating...");
        }
        ImGui::TextDisabled("Replaces each target's existing keyframes.");

        bool hasResultNow = false;
        AIAnimationResult resultNow;
        {
            const std::lock_guard<std::mutex> lock(aiAnimation.resultMutex);
            hasResultNow = aiAnimation.hasResult;
            if (hasResultNow)
            {
                resultNow = aiAnimation.result;
                aiAnimation.hasResult = false;
            }
        }
        if (hasResultNow)
        {
            aiAnimation.status = resultNow.message;
            aiAnimation.statusSuccess = resultNow.success;
            logMessage(
                console,
                resultNow.success ? LogLevel::Info : LogLevel::Error,
                "AI Animation: " + resultNow.message);
            for (const GeneratedEntityAnimation& generated : resultNow.animations)
            {
                // Route through the command bus so the mutation is undoable
                // like every other editor change - the prior direct write
                // here left the undo stack out of sync with the visible state.
                commandBus.execute(SetAnimationCommand{
                    generated.entityName,
                    /*enabled=*/true,
                    generated.looping,
                    generated.keyframes,
                });
            }
        }

        if (!aiAnimation.status.empty())
        {
            ImGui::TextColored(
                aiAnimation.statusSuccess ? ImVec4(0.35F, 0.85F, 0.45F, 1.0F) : ImVec4(0.95F, 0.35F, 0.35F, 1.0F),
                "%s",
                aiAnimation.status.c_str());
        }
        ImGui::Separator();
    }

    void drawAnimationPanel(
        EditorScene& scene,
        AICommandBus& commandBus,
        SelectionState& selection,
        AnimationPanelState& animPanel,
        AIAnimationState& aiAnimation,
        const AIProviderClient& aiProviderClient,
        const std::string& activeProviderId,
        ConsoleState& console,
        const float deltaTime)
    {
        ImGui::Begin("Animation");

        drawAiAnimationSection(scene, commandBus, selection, aiAnimation, aiProviderClient, activeProviderId, console);

        const SceneEntity* selected = selection.selectedEntityId.has_value()
            ? scene.findEntity(*selection.selectedEntityId)
            : nullptr;

        if (selected == nullptr)
        {
            animPanel.recordMode = false;
            animPanel.previewPlaying = false;
            ImGui::TextDisabled("Select an object in the Hierarchy or Viewport to edit its animation.");
            ImGui::End();
            return;
        }

        // Unmissable: this whole panel always acts on the current selection.
        ImGui::TextColored(ImVec4(0.4F, 0.8F, 1.0F, 1.0F), "Animating: %s", selected->name.c_str());
        ImGui::Separator();

        if (!selected->animation.enabled)
        {
            ImGui::TextWrapped(
                "'%s' isn't animated yet. Enable it here or via the Inspector's 'Animate Object' checkbox.",
                selected->name.c_str());
            if (ImGui::Button("Enable Animation"))
            {
                if (SceneEntity* mutableEntity = scene.findEntityMutable(selected->id))
                {
                    mutableEntity->animation.enabled = true;
                }
            }
            ImGui::End();
            return;
        }

        // Snapshot for this frame's reads; mutations below go through the live
        // pointer (bookkeeping) or the command bus (transform values).
        const SceneEntity entity = *selected;
        const std::string entityName = entity.name;
        const int entityId = entity.id;

        bool looping = entity.animation.looping;
        if (ImGui::Checkbox("Loop", &looping))
        {
            if (SceneEntity* mutableEntity = scene.findEntityMutable(entityId))
            {
                mutableEntity->animation.looping = looping;
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Timeline");
        const float maxTime = entity.animation.keyframes.empty()
            ? 5.0F
            : entity.animation.keyframes.back().time + 2.0F;
        const bool scrubbed = ImGui::SliderFloat("Time (s)", &animPanel.scrubTime, 0.0F, maxTime, "%.2f");

        if (animPanel.recordMode)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70F, 0.15F, 0.15F, 1.0F));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80F, 0.20F, 0.20F, 1.0F));
            if (ImGui::Button("* Recording - click to stop"))
            {
                animPanel.recordMode = false;
            }
            ImGui::PopStyleColor(2);
            ImGui::TextWrapped(
                "Pose '%s' with the gizmo or the Inspector's Transform fields - it's saved to t=%.2fs "
                "automatically, every frame, until you stop.",
                entityName.c_str(),
                animPanel.scrubTime);
        }
        else
        {
            if (ImGui::Button("* Start Recording"))
            {
                animPanel.recordMode = true;
                animPanel.previewPlaying = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Add/Update Keyframe Here"))
            {
                upsertKeyframe(scene, entityId, animPanel.scrubTime, entity.position, entity.rotationEuler, entity.scale);
            }
        }

        // Continuous capture while armed: whatever pose the entity is in gets
        // saved to the scrubbed time every frame (the whole point of Record mode).
        if (animPanel.recordMode)
        {
            upsertKeyframe(scene, entityId, animPanel.scrubTime, entity.position, entity.rotationEuler, entity.scale);
        }
        // Otherwise, moving the scrubber previews that instant of the animation
        // on the object — but only right when it's dragged, so it doesn't fight
        // manual gizmo/Inspector edits made afterwards at the same time value.
        else if (!animPanel.previewPlaying && scrubbed && !entity.animation.keyframes.empty())
        {
            applyPose(commandBus, entityName, sampleAnimation(entity.animation, animPanel.scrubTime));
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Keyframes:");
        for (std::size_t index = 0; index < entity.animation.keyframes.size(); ++index)
        {
            const TransformKeyframe& keyframe = entity.animation.keyframes[index];
            const bool isAtScrubTime = std::abs(keyframe.time - animPanel.scrubTime) < 0.005F;
            ImGui::PushID(static_cast<int>(index));
            if (isAtScrubTime)
            {
                ImGui::TextColored(ImVec4(1.0F, 0.8F, 0.2F, 1.0F), "-> %.2fs", keyframe.time);
            }
            else
            {
                ImGui::Text("   %.2fs", keyframe.time);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Go To"))
            {
                animPanel.scrubTime = keyframe.time;
                animPanel.recordMode = false;
                animPanel.previewPlaying = false;
                applyPose(commandBus, entityName, {keyframe.position, keyframe.rotationEuler, keyframe.scale});
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete"))
            {
                if (SceneEntity* mutableEntity = scene.findEntityMutable(entityId))
                {
                    if (index < mutableEntity->animation.keyframes.size())
                    {
                        mutableEntity->animation.keyframes.erase(
                            mutableEntity->animation.keyframes.begin() + static_cast<std::ptrdiff_t>(index));
                    }
                }
            }
            ImGui::PopID();
        }
        if (entity.animation.keyframes.empty())
        {
            ImGui::TextDisabled("No keyframes yet - click Start Recording, then pose the object.");
        }

        ImGui::Separator();
        const bool canPreview = entity.animation.keyframes.size() >= 2 && !animPanel.recordMode;
        if (!canPreview)
        {
            ImGui::BeginDisabled();
        }
        if (!animPanel.previewPlaying)
        {
            if (ImGui::Button("Preview Playback"))
            {
                animPanel.previewPlaying = true;
                animPanel.previewTime = entity.animation.keyframes.front().time;
            }
        }
        else
        {
            if (ImGui::Button("Stop Preview"))
            {
                animPanel.previewPlaying = false;
            }
        }
        if (!canPreview)
        {
            ImGui::EndDisabled();
        }

        if (animPanel.previewPlaying)
        {
            animPanel.previewTime += deltaTime;
            animPanel.scrubTime = animPanel.previewTime;
            applyPose(commandBus, entityName, sampleAnimation(entity.animation, animPanel.previewTime));

            if (!entity.animation.looping && animPanel.previewTime >= entity.animation.keyframes.back().time)
            {
                animPanel.previewPlaying = false;
            }
        }

        ImGui::End();
    }

    // True parent-child hierarchy (SceneEntity::parentName/localPosition/
    // localRotationEuler/localScale) - runs every frame regardless of
    // Play state, so a child visibly follows its parent while authoring
    // the scene too, not just during Play. Recomputes each parented
    // entity's WORLD position/rotation/scale (the same fields rendering/
    // physics/picking/scripts already read directly - unchanged meaning,
    // so nothing else in the app needed to change) by composing its
    // applyParentConstraints/tickPlayModeAnimations/tickScripts/
    // tickProjectiles moved to Engine (GameForger/Runtime/GameplayLoop.hpp)
    // so GameForgerRuntime can drive the identical gameplay tick loop
    // standalone - see the `using gameforger::editor::tick*` declarations
    // above.

    void drawEditorPanels(
        gameforger::editor::ViewportRenderer& viewportRenderer,
        EditorCameraState& camera,
        EditorScene& scene,
        AICommandBus& commandBus,
        SelectionState& selection,
        RenameState& renameState,
        ScriptCreatorState& scriptCreator,
        ScriptEditorState& scriptEditor,
        const AIProviderClient& aiProviderClient,
        const std::string& activeProviderId,
        const std::filesystem::path& projectRoot,
        AIForgeState& aiForge,
        PlayModeState& playMode,
        ScriptRuntime& scriptRuntime,
        AnimationPanelState& animPanel,
        AIAnimationState& aiAnimation,
        ConsoleState& console,
        ProjectBrowserState& projectBrowser,
        EditHistoryState& history,
        const std::filesystem::path& currentScenePath,
        ImGuizmo::OPERATION& gizmoOperation,
        TerrainSculptState& terrainSculpt,
        const float deltaTime,
        const HWND nativeWindowHandle)
    {
        const bool advanceSim = playMode.isPlaying && (!playMode.isPaused || playMode.stepOneFrame);
        const float simDt = advanceSim ? deltaTime : 0.0F;

        // Scripts read where the player is looking (fps_player.lua tilts its
        // hands with it) and whether the inventory grid has the mouse.
        playMode.gameplay.lookPitchDegrees = playMode.gameCameraLookPitchDegrees;
        playMode.gameplay.lookYawDegrees = playMode.gameCameraLookYawDegrees;
        playMode.gameplay.inventoryOpen = playMode.inventoryWindowOpen;

        applyParentConstraints(scene, commandBus);
        tickPlayModeAnimations(scene, commandBus, playMode.gameplay, advanceSim, simDt);
        tickScripts(scene, scriptRuntime, advanceSim, simDt);
        if (advanceSim)
        {
            // Again after scripts, so children of anything a script just
            // moved (the first-person hands/weapons under the view rig)
            // follow THIS frame instead of lagging one frame behind the
            // camera.
            applyParentConstraints(scene, commandBus);
        }
        tickProjectiles(scene, commandBus, playMode.gameplay, advanceSim, simDt);
        tickEffects(playMode.gameplay, advanceSim, simDt);

        // Enemy catapult auto-fire - the "two-sided battle" simplification
        // from the plan: no live targeting, just a periodic shot along
        // whatever fixed direction its arm/yaw mount were authored facing
        // (placed pointed at the player's castle). Frozen once
        // gameOverMessage is set, same as the player's own E/R input.
        if (advanceSim && playMode.gameplay.gameOverMessage.empty())
        {
            playMode.gameplay.enemyCatapultFireTimerSeconds -= simDt;
            if (playMode.gameplay.enemyCatapultFireTimerSeconds <= 0.0F)
            {
                playMode.gameplay.enemyCatapultFireTimerSeconds = 8.0F;
                for (const SceneEntity& entity : scene.entities())
                {
                    if (!entity.active || !entity.isCatapult ||
                        std::find(entity.tags.begin(), entity.tags.end(), "EnemyCatapult") == entity.tags.end())
                    {
                        continue;
                    }
                    const SceneEntity* yawEntity = scene.findEntity(entity.catapult.yawEntityName);
                    const SceneEntity* armEntity = scene.findEntity(entity.catapult.armEntityName);
                    const glm::vec3 launchOrigin = armEntity != nullptr ? armEntity->position : entity.position;
                    const float yawDegrees =
                        yawEntity != nullptr ? entityOwnYawDegrees(*yawEntity) : entity.rotationEuler.y;
                    const float pitchDegrees =
                        armEntity != nullptr ? -entityOwnPitchAxisDegrees(*armEntity) : 30.0F;
                    const glm::vec3 launchDirection = yawPitchForward(yawDegrees, pitchDegrees);
                    playMode.gameplay.projectiles.push_back(GameplayState::Projectile{
                        launchOrigin, launchDirection * entity.catapult.launchSpeed, "PlayerCastle", 6.0F, true});
                    break;
                }
            }
        }

        if (playMode.isPlaying && playMode.stepOneFrame)
        {
            playMode.stepOneFrame = false;
        }

        if (!playMode.isPlaying && !ImGui::GetIO().WantCaptureKeyboard && !ImGuizmo::IsUsing())
        {
            const ImGuiIO& shortcutIo = ImGui::GetIO();
            if (shortcutIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z))
            {
                performUndo(history, scene, selection);
            }
            else if (shortcutIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y))
            {
                performRedo(history, scene, selection);
            }
            else if (shortcutIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R))
            {
                saveSceneAndLog(scene, currentScenePath, console);
            }
            else if (shortcutIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A))
            {
                selection.multiSelectedIds.clear();
                for (const SceneEntity& entity : scene.entities())
                {
                    selection.multiSelectedIds.push_back(entity.id);
                }
                selection.shiftAnchorId.reset();
                syncPrimarySelection(selection);
            }
            else if (shortcutIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C))
            {
                if (history.focusedScriptEntityId.has_value() && !history.focusedScriptPath.empty())
                {
                    history.clipboardScriptPath = history.focusedScriptPath;
                    history.clipboardHoldsScript = true;
                }
                else if (!selection.multiSelectedIds.empty())
                {
                    history.clipboardEntities.clear();
                    for (const int id : selection.multiSelectedIds)
                    {
                        if (const SceneEntity* entity = scene.findEntity(id))
                        {
                            history.clipboardEntities.push_back(*entity);
                        }
                    }
                    history.clipboardHoldsScript = false;
                }
            }
            else if (shortcutIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V))
            {
                if (history.clipboardHoldsScript && !history.clipboardScriptPath.empty() &&
                    selection.selectedEntityId.has_value())
                {
                    if (const SceneEntity* target = scene.findEntity(*selection.selectedEntityId))
                    {
                        commandBus.execute(AttachScriptCommand{target->name, history.clipboardScriptPath});
                    }
                }
                else if (!history.clipboardHoldsScript && !history.clipboardEntities.empty())
                {
                    pasteClipboardEntities(scene, commandBus, selection, history.clipboardEntities);
                }
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Delete))
            {
                if (history.focusedScriptEntityId.has_value() && !history.focusedScriptPath.empty())
                {
                    if (const SceneEntity* scriptEntity = scene.findEntity(*history.focusedScriptEntityId))
                    {
                        commandBus.execute(DetachScriptCommand{scriptEntity->name, history.focusedScriptPath});
                    }
                    history.focusedScriptEntityId.reset();
                    history.focusedScriptPath.clear();
                }
                else if (selection.selectedEntityId.has_value())
                {
                    deleteSelected(scene, commandBus, selection);
                }
            }
            else if (shortcutIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D) && selection.selectedEntityId.has_value())
            {
                duplicateSelected(scene, commandBus, selection);
            }
        }

        drawHierarchyPanel(scene, commandBus, selection, renameState, console, projectRoot, nativeWindowHandle);
        drawInspector(
            scene,
            commandBus,
            selection,
            scriptCreator,
            scriptEditor,
            aiProviderClient,
            activeProviderId,
            projectRoot,
            console,
            history,
            scriptRuntime,
            terrainSculpt,
            nativeWindowHandle);

        drawProjectBrowser(projectBrowser, projectRoot, scriptEditor);

        ImGui::Begin("AI Forge");
        static std::array<char, 1024> prompt{};
        ImGui::TextWrapped(
            "Describe what you want in plain language (e.g. \"add a red sphere named Enemy above the "
            "cube\") and the active AI provider will plan it. Exact local commands (create_entity Name, "
            "set_position Name x y z, create_script path content) still run instantly, no AI call needed.");
        ImGui::InputTextMultiline(
            "##Prompt",
            prompt.data(),
            prompt.size(),
            ImVec2(-1.0F, 110.0F));

        const bool isPlanning = aiForge.planning.load();
        const bool aiForgeDisabled = isPlanning || playMode.isPlaying;
        if (aiForgeDisabled)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Plan Changes"))
        {
            const std::string promptText = prompt.data();
            if (const std::optional<AIEditorCommand> localCommand = parseEditorPrompt(promptText);
                localCommand.has_value())
            {
                const AICommandResult result = commandBus.preview(*localCommand);
                aiForge.status = result.message;
                aiForge.statusSuccess = result.success;
                if (result.success)
                {
                    aiForge.pendingCommands = {*localCommand};
                    aiForge.pendingDescriptions = {describeCommand(*localCommand)};
                }
                else
                {
                    aiForge.pendingCommands.clear();
                    aiForge.pendingDescriptions.clear();
                }
            }
            else if (promptText.empty())
            {
                aiForge.status = "Type a request first.";
                aiForge.statusSuccess = false;
            }
            else
            {
                if (aiForge.worker.joinable())
                {
                    aiForge.worker.join();
                }
                aiForge.pendingCommands.clear();
                aiForge.pendingDescriptions.clear();
                aiForge.status = "Asking the AI...";
                aiForge.statusSuccess = false;
                aiForge.planning = true;
                {
                    const std::lock_guard<std::mutex> lock(aiForge.resultMutex);
                    aiForge.hasPlanResult = false;
                }
                const std::string providerId = activeProviderId;
                const std::vector<SceneEntity> currentEntities = scene.entities();
                aiForge.worker = std::thread(
                    [&aiForge, &aiProviderClient, promptText, providerId, currentEntities]()
                    {
                        AIPlanResult result =
                            planEditorCommands(aiProviderClient, providerId, promptText, currentEntities);
                        const std::lock_guard<std::mutex> lock(aiForge.resultMutex);
                        aiForge.planResult = std::move(result);
                        aiForge.hasPlanResult = true;
                        aiForge.planning = false;
                    });
            }
        }
        if (aiForgeDisabled)
        {
            ImGui::EndDisabled();
            if (isPlanning)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("Planning...");
            }
        }

        {
            bool hasPlanResultNow = false;
            AIPlanResult planResultNow;
            {
                const std::lock_guard<std::mutex> lock(aiForge.resultMutex);
                hasPlanResultNow = aiForge.hasPlanResult;
                if (hasPlanResultNow)
                {
                    planResultNow = aiForge.planResult;
                    aiForge.hasPlanResult = false;
                }
            }
            if (hasPlanResultNow)
            {
                aiForge.status = planResultNow.message;
                aiForge.statusSuccess = planResultNow.success;
                logMessage(
                    console,
                    planResultNow.success ? LogLevel::Info : LogLevel::Error,
                    "AI Forge: " + planResultNow.message);
                aiForge.pendingCommands = std::move(planResultNow.commands);
                aiForge.pendingDescriptions = std::move(planResultNow.commandDescriptions);
            }
        }

        if (!aiForge.pendingDescriptions.empty())
        {
            ImGui::TextUnformatted("Planned changes:");
            for (const std::string& description : aiForge.pendingDescriptions)
            {
                ImGui::BulletText("%s", description.c_str());
            }
        }

        const bool canApply = !aiForge.pendingCommands.empty() && !playMode.isPlaying;
        if (!canApply)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Apply") && canApply)
        {
            int succeeded = 0;
            std::string firstError;
            for (const AIEditorCommand& command : aiForge.pendingCommands)
            {
                const AICommandResult result = commandBus.execute(command);
                if (result.success)
                {
                    ++succeeded;
                }
                else if (firstError.empty())
                {
                    firstError = result.message;
                }
            }
            aiForge.status = firstError.empty()
                ? ("Applied " + std::to_string(succeeded) + " command(s).")
                : ("Applied " + std::to_string(succeeded) + "/" +
                    std::to_string(aiForge.pendingCommands.size()) + " command(s); first error: " + firstError);
            aiForge.statusSuccess = firstError.empty();
            logMessage(
                console, aiForge.statusSuccess ? LogLevel::Info : LogLevel::Warning, "AI Forge: " + aiForge.status);
            aiForge.pendingCommands.clear();
            aiForge.pendingDescriptions.clear();
        }
        if (!canApply)
        {
            ImGui::EndDisabled();
        }
        if (!aiForge.status.empty())
        {
            ImGui::TextColored(
                aiForge.statusSuccess
                    ? ImVec4(0.35F, 0.85F, 0.45F, 1.0F)
                    : ImVec4(0.95F, 0.35F, 0.35F, 1.0F),
                "%s",
                aiForge.status.c_str());
        }
        ImGui::End();

        EditorCameraState& activeCamera = playMode.isPlaying ? playMode.playCamera : camera;

        ImGui::Begin("Viewport");

        if (playMode.isPlaying)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::RadioButton("Move (W)", gizmoOperation == ImGuizmo::TRANSLATE))
        {
            gizmoOperation = ImGuizmo::TRANSLATE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate (E)", gizmoOperation == ImGuizmo::ROTATE))
        {
            gizmoOperation = ImGuizmo::ROTATE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale (R)", gizmoOperation == ImGuizmo::SCALE))
        {
            gizmoOperation = ImGuizmo::SCALE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Rect (T)", gizmoOperation == ImGuizmo::BOUNDS))
        {
            gizmoOperation = ImGuizmo::BOUNDS;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Transform (Y)", gizmoOperation == ImGuizmo::UNIVERSAL))
        {
            gizmoOperation = ImGuizmo::UNIVERSAL;
        }

        static ImGuizmo::MODE gizmoMode = ImGuizmo::LOCAL;
        static bool gridSnap = false;
        static float snapTranslation = 1.0F;
        static float snapRotation = 15.0F;
        static float snapScale = 0.1F;

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ImGui::Button(gizmoMode == ImGuizmo::LOCAL ? "Local" : "Global"))
        {
            gizmoMode = (gizmoMode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Snap", &gridSnap);

        if (playMode.isPlaying)
        {
            ImGui::EndDisabled();
        }
        ImGui::TextDisabled(
            "Hold Right Mouse + WASD/QE to fly, Middle-drag to orbit (Shift = pan), scroll to zoom, F to frame "
            "selection");

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImVec2 imageScreenPos = ImGui::GetCursorScreenPos();
        const int viewportWidth = static_cast<int>(available.x);
        const int viewportHeight = static_cast<int>(available.y);
        const bool viewportReady = viewportRenderer.resize(viewportWidth, viewportHeight);

        if (viewportReady)
        {
            viewportRenderer.render(scene.entities(), selection.multiSelectedIds, projectRoot);
            ImGui::Image(
                static_cast<ImTextureID>(viewportRenderer.texture()),
                available,
                ImVec2(0.0F, 1.0F),
                ImVec2(1.0F, 0.0F));
        }
        else
        {
            ImGui::TextUnformatted("Viewport OpenGL indisponible.");
        }

        const bool viewportHovered = viewportReady && ImGui::IsItemHovered();

        if (!playMode.isPlaying && viewportHovered && activeCamera.dragMode == CameraDragMode::None)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_W)) gizmoOperation = ImGuizmo::TRANSLATE;
            if (ImGui::IsKeyPressed(ImGuiKey_E)) gizmoOperation = ImGuizmo::ROTATE;
            if (ImGui::IsKeyPressed(ImGuiKey_R)) gizmoOperation = ImGuizmo::SCALE;
            if (ImGui::IsKeyPressed(ImGuiKey_T)) gizmoOperation = ImGuizmo::BOUNDS;
            if (ImGui::IsKeyPressed(ImGuiKey_Y)) gizmoOperation = ImGuizmo::UNIVERSAL;
        }

        // F (or Ctrl+F - IsKeyPressed(F) fires either way, Ctrl+D was already
        // taken by Duplicate) centers/frames the camera on the selection.
        if (viewportHovered && ImGui::IsKeyPressed(ImGuiKey_F) && selection.selectedEntityId.has_value())
        {
            if (const SceneEntity* entity = scene.findEntity(*selection.selectedEntityId))
            {
                activeCamera.target = entity->position;
                const float radius = std::max({entity->scale.x, entity->scale.y, entity->scale.z});
                activeCamera.distance = std::clamp(radius * 3.0F, 2.0F, 40.0F);
            }
        }

        // While the sculpt brush is active on the selected terrain, the
        // gizmo stays hidden and left-click-drag paints height instead of
        // moving/rotating/scaling the terrain entity.
        const bool sculptingSelectedTerrain = terrainSculpt.active && selection.selectedEntityId.has_value() &&
            [&scene, &selection]()
            {
                const SceneEntity* entity = scene.findEntity(*selection.selectedEntityId);
                return entity != nullptr && entity->isTerrain;
            }();

        if (!playMode.isPlaying && viewportReady && selection.selectedEntityId.has_value() &&
            !sculptingSelectedTerrain)
        {
            if (const SceneEntity* entity = scene.findEntity(*selection.selectedEntityId))
            {
                ImGuizmo::SetOrthographic(false);
                ImGuizmo::SetDrawlist();
                ImGuizmo::SetRect(imageScreenPos.x, imageScreenPos.y, available.x, available.y);

                glm::mat4 model = composeEntityPivotFrame(*entity);
                // Every primitive mesh is generated inside this same [-1,1] box,
                // which is what the Rect (T) resize handles grab onto.
                constexpr std::array<float, 6> localBounds{-1.0F, -1.0F, -1.0F, 1.0F, 1.0F, 1.0F};
                float snapValues[3] = {1.0F, 1.0F, 1.0F};
                if (gridSnap)
                {
                    if (gizmoOperation == ImGuizmo::ROTATE)
                    {
                        snapValues[0] = snapValues[1] = snapValues[2] = snapRotation;
                    }
                    else if (gizmoOperation == ImGuizmo::SCALE)
                    {
                        snapValues[0] = snapValues[1] = snapValues[2] = snapScale;
                    }
                    else
                    {
                        snapValues[0] = snapValues[1] = snapValues[2] = snapTranslation;
                    }
                }
                ImGuizmo::Manipulate(
                    glm::value_ptr(viewportRenderer.view()),
                    glm::value_ptr(viewportRenderer.projection()),
                    gizmoOperation,
                    gizmoMode,
                    glm::value_ptr(model),
                    nullptr,
                    gridSnap ? snapValues : nullptr,
                    localBounds.data());

                if (ImGuizmo::IsUsing())
                {
                    // Parented entities need special handling here: `model`
                    // (just produced by the gizmo) is the entity's new WORLD
                    // transform, but SceneEntity::position/rotationEuler/
                    // scale are recomputed from parentWorld*local* every
                    // single frame by applyParentConstraints (below) -
                    // writing world Transform fields directly, as the
                    // unparented branch does, would get silently overwritten
                    // the very next frame (this was a real, reported bug:
                    // dragging/rotating/scaling a child with the gizmo
                    // visibly snapped back to its old transform). Instead
                    // solve for the local* that reproduces this exact world
                    // transform under the parent's current world matrix -
                    // the same inverse-solve reparentEntityKeepingWorldTransform
                    // already uses for drag-and-drop reparenting.
                    const SceneEntity* parent =
                        entity->parentName.empty() ? nullptr : scene.findEntity(entity->parentName);
                    if (parent != nullptr)
                    {
                        const glm::mat4 parentWorld = composeEntityPivotFrame(*parent);
                        const glm::mat4 localMatrix = glm::inverse(parentWorld) * model;
                        float localTranslation[3];
                        float localRotation[3];
                        float localScale[3];
                        ImGuizmo::DecomposeMatrixToComponents(
                            glm::value_ptr(localMatrix), localTranslation, localRotation, localScale);
                        commandBus.execute(SetPropertyCommand{
                            entity->name, "Parent", "localPosition",
                            glm::vec3(localTranslation[0], localTranslation[1], localTranslation[2])});
                        commandBus.execute(SetPropertyCommand{
                            entity->name, "Parent", "localRotation",
                            glm::vec3(localRotation[0], localRotation[1], localRotation[2])});
                        commandBus.execute(SetPropertyCommand{
                            entity->name, "Parent", "localScale",
                            glm::vec3(localScale[0], localScale[1], localScale[2])});
                    }
                    else
                    {
                        float translation[3];
                        float rotation[3];
                        float scale[3];
                        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), translation, rotation, scale);
                        commandBus.execute(SetPropertyCommand{
                            entity->name, "Transform", "position", glm::vec3(translation[0], translation[1], translation[2])});
                        commandBus.execute(SetPropertyCommand{
                            entity->name, "Transform", "rotation", glm::vec3(rotation[0], rotation[1], rotation[2])});
                        commandBus.execute(SetPropertyCommand{
                            entity->name, "Transform", "scale", glm::vec3(scale[0], scale[1], scale[2])});
                    }
                }
            }
        }

        if (!playMode.isPlaying && sculptingSelectedTerrain && viewportHovered &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const Ray ray = computeMouseRay(
                ImGui::GetMousePos(), imageScreenPos, available, viewportRenderer.view(), viewportRenderer.projection());
            if (const SceneEntity* entity = scene.findEntity(*selection.selectedEntityId))
            {
                if (const std::optional<glm::vec3> hitPoint = terrainRaycastHit(ray, *entity))
                {
                    if (terrainSculpt.paintMode)
                    {
                        paintTerrainSplat(
                            scene,
                            entity->id,
                            *hitPoint,
                            terrainSculpt.brushRadius,
                            terrainSculpt.brushStrength,
                            terrainSculpt.paintLayerIndex,
                            deltaTime);
                    }
                    else
                    {
                        sculptTerrain(
                            scene,
                            entity->id,
                            *hitPoint,
                            terrainSculpt.brushRadius,
                            terrainSculpt.brushStrength,
                            terrainSculpt.lowerMode != ImGui::GetIO().KeyShift,
                            deltaTime);
                    }
                }
            }
        }

        const bool gizmoBusy = !playMode.isPlaying && (ImGuizmo::IsUsing() || ImGuizmo::IsOver());

        if (!playMode.isPlaying && viewportHovered && !gizmoBusy && !sculptingSelectedTerrain &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const Ray ray = computeMouseRay(
                ImGui::GetMousePos(), imageScreenPos, available, viewportRenderer.view(), viewportRenderer.projection());
            const std::optional<int> hit = pickEntity(ray, scene, projectRoot);
            const ImGuiIO& pickIo = ImGui::GetIO();
            if (hit.has_value())
            {
                std::vector<int> orderedIds;
                orderedIds.reserve(scene.entities().size());
                for (const SceneEntity& sceneEntity : scene.entities())
                {
                    orderedIds.push_back(sceneEntity.id);
                }
                applySelectionClick(selection, orderedIds, *hit, pickIo.KeyCtrl, pickIo.KeyShift);
            }
            else if (!pickIo.KeyCtrl && !pickIo.KeyShift)
            {
                clearSelection(selection);
            }
        }

        // Normally the mouse wheel can't dolly in past 1.5 units - relax that
        // floor to (almost) zero when the orbit target IS the selected
        // object's position (i.e. right after F/Ctrl+F framed it and the
        // user hasn't panned away since), so "zoom in on the selection" can
        // scroll all the way to its center instead of stopping short.
        float cameraMinDistance = 1.5F;
        if (selection.selectedEntityId.has_value())
        {
            if (const SceneEntity* selectedEntity = scene.findEntity(*selection.selectedEntityId))
            {
                if (glm::distance(activeCamera.target, selectedEntity->position) < 0.01F)
                {
                    cameraMinDistance = 0.05F;
                }
            }
        }
        updateCamera(activeCamera, viewportHovered && !gizmoBusy, deltaTime, cameraMinDistance);
        viewportRenderer.setCamera(activeCamera.yaw, activeCamera.pitch, activeCamera.distance, activeCamera.target);

        ImGui::End();

        drawAnimationPanel(
            scene,
            commandBus,
            selection,
            animPanel,
            aiAnimation,
            aiProviderClient,
            activeProviderId,
            console,
            deltaTime);
    }
}

int main()
{
    const std::filesystem::path projectRoot = std::filesystem::current_path();
    ConsoleState console;
    AppearanceState appearance;
    LanguageState language;
    SettingsState settings;
    logMessage(console, LogLevel::Info, "GameForgerAI Editor starting.");

    glfwSetErrorCallback(glfwErrorCallback);
    if (glfwInit() != GLFW_TRUE)
    {
        return 1;
    }

    showSplashScreen(projectRoot / "Game" / "Branding" / "logo.jpg", 1.8);

    GLFWwindow* window = createWindow();
    if (window == nullptr)
    {
        std::fprintf(
            stderr,
            "OpenGL 4.6 window creation failed. Update the graphics driver "
            "or lower the requested version in Editor/src/main.cpp.\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    const int loadedVersion = gladLoadGL(glfwGetProcAddress);
    if (loadedVersion == 0)
    {
        std::fprintf(stderr, "Failed to load OpenGL functions.\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    // gladLoadGL returns the GLAD loader version (1 for desktop GL), NOT
    // the context's actual major.minor. Query the context itself before
    // handing it to ImGui with a #version 460 core GLSL string - if the
    // driver returned 4.5 or lower, every shader compile fails and the
    // viewport shows the only fallback "Viewport OpenGL indisponible.".
    {
        GLint major = 0;
        GLint minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        if (major < 4 || (major == 4 && minor < 6))
        {
            std::fprintf(
                stderr,
                "GameForgerAI requires OpenGL 4.6 - this machine reports %d.%d. Aborting.\n",
                static_cast<int>(major), static_cast<int>(minor));
            glfwDestroyWindow(window);
            glfwTerminate();
            return 1;
        }
    }

    const std::filesystem::path iconPath = projectRoot / "Game" / "Branding" / "icon.ico";
    const HWND nativeWindowHandle = glfwGetWin32Window(window);
    const std::wstring iconPathWide = iconPath.wstring();
    const HICON iconLarge = static_cast<HICON>(
        LoadImageW(nullptr, iconPathWide.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE));
    const HICON iconSmall = static_cast<HICON>(
        LoadImageW(nullptr, iconPathWide.c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE));

    NOTIFYICONDATAW trayIcon{};
    bool trayIconAdded = false;
    if (iconLarge != nullptr && iconSmall != nullptr)
    {
        SendMessageW(nativeWindowHandle, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(iconLarge));
        SendMessageW(nativeWindowHandle, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(iconSmall));

        trayIcon.cbSize = sizeof(trayIcon);
        trayIcon.hWnd = nativeWindowHandle;
        trayIcon.uID = 1;
        trayIcon.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
        trayIcon.hIcon = iconSmall;
        trayIcon.uCallbackMessage = WM_APP + 1;
        wcscpy_s(trayIcon.szTip, L"GameForgerAI Editor");
        trayIconAdded = Shell_NotifyIconW(NIM_ADD, &trayIcon) == TRUE;
    }
    else
    {
        std::fprintf(stderr, "Application icon could not be loaded from: %s\n", iconPath.string().c_str());
        logMessage(console, LogLevel::Warning, "Application icon could not be loaded from " + iconPath.string());
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = "GameForgerEditorLayout.ini";

    applyTheme(appearance.theme);
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 3.0F;
    style.FrameRounding = 3.0F;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 460 core");

    gameforger::editor::ViewportRenderer viewportRenderer;
    gameforger::editor::ViewportRenderer gameViewRenderer;
    gameforger::editor::ViewportRenderer cineCameraRenderer;
    if (!viewportRenderer.initialize() || !gameViewRenderer.initialize() || !cineCameraRenderer.initialize())
    {
        std::fprintf(stderr, "Failed to initialize the OpenGL viewport renderer.\n");
        viewportRenderer.shutdown();
        gameViewRenderer.shutdown();
        cineCameraRenderer.shutdown();
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    EditorCameraState camera;
    viewportRenderer.setCamera(camera.yaw, camera.pitch, camera.distance, camera.target);
    // Fixed, non-navigable "as the player would see it" default view.
    const EditorCameraState gameViewCamera;
    EditorScene scene(projectRoot);
    AICommandBus commandBus;
    EditHistoryState history;
    double currentFrameTime = glfwGetTime();
    commandBus.setHandler(
        [&scene, &history, &currentFrameTime](const AIEditorCommand& command)
        {
            const bool pushed = pushUndoSnapshot(history, scene, currentFrameTime);
            const AICommandResult result = scene.execute(command);
            if (!result.success && pushed && !history.undoStack.empty())
            {
                // Nothing actually changed - drop the snapshot we just pushed
                // rather than leaving a no-op step in the undo history.
                history.undoStack.pop_back();
            }
            return result;
        });
    AIProviderClient aiProviderClient(projectRoot);
    AISetupState aiSetup;
    selectProvider(aiSetup, 0);
    AIForgeState aiForge;
    SelectionState selection;
    RenameState renameState;
    ScriptCreatorState scriptCreator;
    ScriptEditorState scriptEditor;
    PlayModeState playMode;
    ScriptRuntime scriptRuntime;
    // Stateless (just wraps ImGui::IsKeyDown/IsKeyPressed calls) - lives for
    // the app's lifetime and is re-passed to scriptRuntime.initialize() on
    // every Play start, same lifetime pattern as scriptRuntime itself. See
    // GlfwInputSource for GameForgerRuntime's equivalent.
    ImGuiInputSource imguiInputSource;
    AnimationPanelState animPanel;
    AIAnimationState aiAnimation;
    ProjectBrowserState projectBrowser;
    TextMeshToolState textMeshTool;
    StoryboardState storyboard;
    TerrainSculptState terrainSculpt;
    ImGuizmo::OPERATION gizmoOperation = ImGuizmo::TRANSLATE;
    bool resetLayout = false;
    // What "Save Scene" (Ctrl+R) writes to and "Open Scene..." reads from most
    // recently; "Save Scene As..." and "Open Scene..." both update this.
    std::filesystem::path currentScenePath = projectRoot / "Game" / "Scenes" / "Castle.gfprod";
    double lastFrameTime = glfwGetTime();

    while (glfwWindowShouldClose(window) == GLFW_FALSE)
    {
        const double currentTime = glfwGetTime();
        currentFrameTime = currentTime;
        const float deltaTime = static_cast<float>(currentTime - lastFrameTime);
        lastFrameTime = currentTime;

        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        if (resetLayout)
        {
            ImGui::LoadIniSettingsFromMemory("", 0);
            resetLayout = false;
        }

        ImGui::DockSpaceOverViewport(
            0,
            ImGui::GetMainViewport(),
            ImGuiDockNodeFlags_PassthruCentralNode);

        drawMainMenu(
            scene, commandBus, selection, camera, playMode, scriptRuntime, imguiInputSource, projectRoot, console,
            settings, history, currentScenePath, nativeWindowHandle, resetLayout);
        drawSettingsWindow(settings, aiSetup, appearance, language, projectRoot, scene, aiProviderClient, console);
        const std::string activeProviderId = providers[static_cast<std::size_t>(aiSetup.selectedProvider)].id;
        drawEditorPanels(
            viewportRenderer,
            camera,
            scene,
            commandBus,
            selection,
            renameState,
            scriptCreator,
            scriptEditor,
            aiProviderClient,
            activeProviderId,
            projectRoot,
            aiForge,
            playMode,
            scriptRuntime,
            animPanel,
            aiAnimation,
            console,
            projectBrowser,
            history,
            currentScenePath,
            gizmoOperation,
            terrainSculpt,
            deltaTime,
            nativeWindowHandle);
        drawToolboxPanel(
            camera, textMeshTool, storyboard, terrainSculpt, scene, commandBus, selection, console, projectRoot,
            nativeWindowHandle);
        drawConsolePanel(console);
        drawGameViewPanel(
            gameViewRenderer, gameViewCamera, scene, commandBus, playMode, scriptRuntime, projectRoot, window);
        {
            const SceneEntity* followedEntity = (playMode.isPlaying && scriptRuntime.hasActiveCamera())
                ? scene.findEntity(scriptRuntime.activeCameraEntityId())
                : nullptr;
            drawInventoryWindow(playMode, scene, commandBus, followedEntity, projectRoot);
        }
        drawCineCameraPreviewPanel(
            cineCameraRenderer, scene, commandBus, selection, storyboard, projectRoot, deltaTime);
        drawStoryboardPanel(scene, selection, storyboard);

        ImGui::Render();

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.055F, 0.067F, 0.09F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    if (scriptCreator.worker.joinable())
    {
        scriptCreator.worker.join();
    }
    if (aiForge.worker.joinable())
    {
        aiForge.worker.join();
    }
    if (aiAnimation.worker.joinable())
    {
        aiAnimation.worker.join();
    }

    if (trayIconAdded)
    {
        Shell_NotifyIconW(NIM_DELETE, &trayIcon);
    }
    if (iconLarge != nullptr)
    {
        DestroyIcon(iconLarge);
    }
    if (iconSmall != nullptr)
    {
        DestroyIcon(iconSmall);
    }

    viewportRenderer.shutdown();
    gameViewRenderer.shutdown();
    cineCameraRenderer.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
