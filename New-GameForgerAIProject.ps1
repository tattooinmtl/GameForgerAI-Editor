[CmdletBinding()]
param(
    [Parameter()]
    [string]$Destination = 'C:\GameForgerAI-Editor',

    [Parameter()]
    [ValidatePattern('^[A-Za-z][A-Za-z0-9_-]*$')]
    [string]$ProjectName = 'GameForgerAI',

    [Parameter()]
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Write-ProjectFile {
    param(
        [Parameter(Mandatory)]
        [string]$RelativePath,

        [Parameter(Mandatory)]
        [string]$Content
    )

    $targetPath = Join-Path $Destination $RelativePath
    $parent = Split-Path -Parent $targetPath

    if (-not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }

    if ((Test-Path -LiteralPath $targetPath) -and -not $Force) {
        throw "The file already exists: $targetPath`nRun again with -Force to replace generated files. Unrelated files will not be deleted."
    }

    $resolvedContent = $Content.Replace('__PROJECT_NAME__', $ProjectName)
    Set-Content -LiteralPath $targetPath -Value $resolvedContent -Encoding utf8
    Write-Host "Created $RelativePath"
}

if (-not (Test-Path -LiteralPath $Destination)) {
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
}

$cmakeLists = @'
cmake_minimum_required(VERSION 3.28)

project(__PROJECT_NAME__
    VERSION 0.1.0
    DESCRIPTION "AI-assisted OpenGL RPG editor and runtime"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(MSVC)
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.3
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.8
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.3-docking
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(glfw glm glad imgui)

glad_add_library(GameForgerGLAD STATIC REPRODUCIBLE API gl:core=4.6)

add_library(GameForgerImGui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(GameForgerImGui
    PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(GameForgerImGui
    PUBLIC
        glfw
        GameForgerGLAD
)

target_compile_definitions(GameForgerImGui PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD2)
set_target_properties(GameForgerImGui PROPERTIES FOLDER "Dependencies")

add_subdirectory(Engine)
add_subdirectory(Editor)
add_subdirectory(Runtime)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT GameForgerEditor)
'@

$engineCmake = @'
add_library(GameForgerEngine STATIC
    src/Core/Engine.cpp
    src/Animation/Skeleton.cpp
    src/Animation/AnimationClip.cpp
)

add_library(GameForger::Engine ALIAS GameForgerEngine)

target_include_directories(GameForgerEngine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(GameForgerEngine
    PUBLIC
        glm::glm
)

target_compile_features(GameForgerEngine PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(GameForgerEngine PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(GameForgerEngine PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(GameForgerEngine PROPERTIES FOLDER "GameForger")
'@

$editorCmake = @'
add_executable(GameForgerEditor
    src/main.cpp
)

target_link_libraries(GameForgerEditor
    PRIVATE
        GameForger::Engine
        GameForgerImGui
        GameForgerGLAD
        glfw
        glm::glm
)

target_compile_features(GameForgerEditor PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(GameForgerEditor PRIVATE /W4 /permissive- /EHsc)
endif()

set_target_properties(GameForgerEditor PROPERTIES
    FOLDER "GameForger"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
'@

$runtimeCmake = @'
add_executable(GameForgerRuntime
    src/main.cpp
)

target_link_libraries(GameForgerRuntime
    PRIVATE
        GameForger::Engine
        GameForgerGLAD
        glfw
        glm::glm
)

target_compile_features(GameForgerRuntime PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(GameForgerRuntime PRIVATE /W4 /permissive- /EHsc)
endif()

set_target_properties(GameForgerRuntime PROPERTIES
    FOLDER "GameForger"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
'@

$presets = @'
{
  "version": 8,
  "cmakeMinimumRequired": {
    "major": 3,
    "minor": 28,
    "patch": 0
  },
  "configurePresets": [
    {
      "name": "windows-base",
      "hidden": true,
      "generator": "Ninja Multi-Config",
      "binaryDir": "${sourceDir}/out/build/${presetName}",
      "installDir": "${sourceDir}/out/install/${presetName}",
      "cacheVariables": {
        "CMAKE_CONFIGURATION_TYPES": "Debug;Release;RelWithDebInfo"
      },
      "condition": {
        "type": "equals",
        "lhs": "${hostSystemName}",
        "rhs": "Windows"
      }
    },
    {
      "name": "windows-x64",
      "displayName": "Windows x64",
      "description": "Visual Studio 2026 C++ toolchain with Ninja Multi-Config",
      "inherits": "windows-base",
      "architecture": {
        "value": "x64",
        "strategy": "external"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "editor-debug",
      "displayName": "Editor - Debug",
      "configurePreset": "windows-x64",
      "configuration": "Debug",
      "targets": [
        "GameForgerEditor"
      ]
    },
    {
      "name": "runtime-debug",
      "displayName": "Runtime - Debug",
      "configurePreset": "windows-x64",
      "configuration": "Debug",
      "targets": [
        "GameForgerRuntime"
      ]
    },
    {
      "name": "all-release",
      "displayName": "All - Release",
      "configurePreset": "windows-x64",
      "configuration": "Release"
    }
  ]
}
'@

$vsConfig = @'
{
  "version": "1.0",
  "components": [
    "Microsoft.VisualStudio.Workload.NativeDesktop",
    "Microsoft.VisualStudio.Component.VC.CMake.Project",
    "Microsoft.VisualStudio.Component.Windows11SDK.26100"
  ]
}
'@

$gitIgnore = @'
/.vs/
/out/
/build/
/cmake-build-*/
*.user
*.suo
*.VC.db
*.VC.VC.opendb
*.log
'@

$engineHeader = @'
#pragma once

#include <string_view>

namespace gameforger
{
    class Engine final
    {
    public:
        [[nodiscard]] static std::string_view name() noexcept;
        [[nodiscard]] static std::string_view version() noexcept;
    };
}
'@

$engineSource = @'
#include "GameForger/Core/Engine.hpp"

namespace gameforger
{
    std::string_view Engine::name() noexcept
    {
        return "GameForgerAI";
    }

    std::string_view Engine::version() noexcept
    {
        return "0.1.0";
    }
}
'@

$skeletonHeader = @'
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
'@

$skeletonSource = @'
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

            if (bone.parent >= static_cast<BoneIndex>(index))
            {
                errors.emplace_back(
                    "Bone '" + bone.name + "' must reference an earlier parent bone.");
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
'@

$animationHeader = @'
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
'@

$animationSource = @'
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
'@

$aiCommandHeader = @'
#pragma once

#include <string>
#include <variant>

#include <glm/vec3.hpp>

namespace gameforger::editor
{
    struct CreateEntityCommand
    {
        std::string name;
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

    using AIEditorCommand = std::variant<
        CreateEntityCommand,
        SetPositionCommand,
        RequestAnimationCommand>;
}
'@

$editorMain = @'
#include <array>
#include <cstdio>
#include <string>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "GameForger/Core/Engine.hpp"

namespace
{
    void glfwErrorCallback(const int error, const char* description)
    {
        std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
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
            "GameForgerAI Editor",
            nullptr,
            nullptr);
    }

    void drawMainMenu()
    {
        if (!ImGui::BeginMainMenuBar())
        {
            return;
        }

        if (ImGui::BeginMenu("File"))
        {
            ImGui::MenuItem("New Scene", "Ctrl+N");
            ImGui::MenuItem("Open Scene...", "Ctrl+O");
            ImGui::MenuItem("Save Scene", "Ctrl+S");
            ImGui::Separator();
            ImGui::MenuItem("Build Game...");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            ImGui::MenuItem("Undo", "Ctrl+Z");
            ImGui::MenuItem("Redo", "Ctrl+Y");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Character"))
        {
            ImGui::MenuItem("Import GLB...");
            ImGui::MenuItem("Map Humanoid Skeleton...");
            ImGui::MenuItem("Animation Library...");
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    void drawEditorPanels()
    {
        ImGui::DockSpaceOverViewport(
            0,
            ImGui::GetMainViewport(),
            ImGuiDockNodeFlags_PassthruCentralNode);

        ImGui::Begin("Scene");
        ImGui::Selectable("World", true);
        ImGui::TreeNodeEx("Camera", ImGuiTreeNodeFlags_Leaf);
        ImGui::TreePop();
        ImGui::TreeNodeEx("Directional Light", ImGuiTreeNodeFlags_Leaf);
        ImGui::TreePop();
        ImGui::End();

        ImGui::Begin("Inspector");
        ImGui::TextUnformatted("World");
        ImGui::Separator();
        std::array<float, 3> position{0.0F, 0.0F, 0.0F};
        ImGui::DragFloat3("Position", position.data(), 0.05F);
        ImGui::TextDisabled("Component editing starts here.");
        ImGui::End();

        ImGui::Begin("Asset Browser");
        ImGui::TextUnformatted("Game/");
        ImGui::BulletText("Characters");
        ImGui::BulletText("Animations");
        ImGui::BulletText("Scenes");
        ImGui::BulletText("Scripts");
        ImGui::End();

        ImGui::Begin("AI Forge");
        static std::array<char, 1024> prompt{};
        ImGui::TextWrapped(
            "Prompts will be translated into validated editor commands "
            "with undo/redo support.");
        ImGui::InputTextMultiline(
            "##Prompt",
            prompt.data(),
            prompt.size(),
            ImVec2(-1.0F, 110.0F));
        if (ImGui::Button("Plan Changes"))
        {
            // The AI service and command validator will be connected here.
        }
        ImGui::SameLine();
        ImGui::BeginDisabled();
        ImGui::Button("Apply");
        ImGui::EndDisabled();
        ImGui::End();

        ImGui::Begin("Viewport");
        const ImVec2 available = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("SceneViewport", available);
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(
            min,
            max,
            IM_COL32(24, 29, 38, 255));
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(min.x + 18.0F, min.y + 18.0F),
            IM_COL32(190, 205, 225, 255),
            "OpenGL scene render target will appear here.");
        ImGui::End();

        ImGui::Begin("Animation");
        ImGui::TextUnformatted("Skeleton: HumanoidV1");
        ImGui::TextUnformatted("Clip: None");
        ImGui::Separator();
        ImGui::Button("|<");
        ImGui::SameLine();
        ImGui::Button("Play");
        ImGui::SameLine();
        ImGui::Button(">|");
        ImGui::End();
    }
}

int main()
{
    glfwSetErrorCallback(glfwErrorCallback);
    if (glfwInit() != GLFW_TRUE)
    {
        return 1;
    }

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

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = "GameForgerEditorLayout.ini";

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 3.0F;
    style.FrameRounding = 3.0F;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 460 core");

    while (glfwWindowShouldClose(window) == GLFW_FALSE)
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        drawMainMenu();
        drawEditorPanels();

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

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
'@

$runtimeMain = @'
#include <cstdio>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "GameForger/Core/Engine.hpp"

namespace
{
    void glfwErrorCallback(const int error, const char* description)
    {
        std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
    }
}

int main()
{
    glfwSetErrorCallback(glfwErrorCallback);
    if (glfwInit() != GLFW_TRUE)
    {
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(
        1280,
        720,
        "GameForgerAI Runtime",
        nullptr,
        nullptr);

    if (window == nullptr)
    {
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (gladLoadGL(glfwGetProcAddress) == 0)
    {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    glEnable(GL_DEPTH_TEST);

    while (glfwWindowShouldClose(window) == GLFW_FALSE)
    {
        glfwPollEvents();

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.025F, 0.035F, 0.055F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glfwSwapBuffers(window);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
'@

$humanoidProfile = @'
{
  "format": "GameForgerSkeletonProfile",
  "version": 1,
  "name": "HumanoidV1",
  "units": "meters",
  "upAxis": "+Y",
  "forwardAxis": "+Z",
  "referencePose": "T",
  "bones": [
    { "id": "rootMotion", "required": true, "parent": null },
    { "id": "hips", "required": true, "parent": "rootMotion" },
    { "id": "spine", "required": true, "parent": "hips" },
    { "id": "chest", "required": true, "parent": "spine" },
    { "id": "neck", "required": true, "parent": "chest" },
    { "id": "head", "required": true, "parent": "neck" },
    { "id": "shoulderLeft", "required": false, "parent": "chest" },
    { "id": "upperArmLeft", "required": true, "parent": "shoulderLeft" },
    { "id": "lowerArmLeft", "required": true, "parent": "upperArmLeft" },
    { "id": "handLeft", "required": true, "parent": "lowerArmLeft" },
    { "id": "shoulderRight", "required": false, "parent": "chest" },
    { "id": "upperArmRight", "required": true, "parent": "shoulderRight" },
    { "id": "lowerArmRight", "required": true, "parent": "upperArmRight" },
    { "id": "handRight", "required": true, "parent": "lowerArmRight" },
    { "id": "upperLegLeft", "required": true, "parent": "hips" },
    { "id": "lowerLegLeft", "required": true, "parent": "upperLegLeft" },
    { "id": "footLeft", "required": true, "parent": "lowerLegLeft" },
    { "id": "toeLeft", "required": false, "parent": "footLeft" },
    { "id": "upperLegRight", "required": true, "parent": "hips" },
    { "id": "lowerLegRight", "required": true, "parent": "upperLegRight" },
    { "id": "footRight", "required": true, "parent": "lowerLegRight" },
    { "id": "toeRight", "required": false, "parent": "footRight" }
  ]
}
'@

$playerLua = @'
local PlayerController = {}

function PlayerController:on_start()
    self.walk_speed = 2.2
    self.run_speed = 5.5
end

function PlayerController:on_update(delta_time)
    -- Engine input, character movement, and animation APIs will be exposed here.
end

return PlayerController
'@

$readme = @'
# __PROJECT_NAME__

Starter structure for an AI-assisted OpenGL RPG editor and standalone runtime.
This is a CMake folder project; do not create a Visual Studio solution or select
a Visual Studio project template.

## Requirements

- Windows 10 or 11
- Visual Studio 2026 with **Desktop development with C++**
- Git
- CMake and Ninja components included with Visual Studio
- A graphics driver supporting OpenGL 4.6
- Internet access during the first CMake configure

The `.vsconfig` file allows Visual Studio Installer to detect the required C++
components. CMake downloads pinned versions of GLFW, GLAD, GLM, and Dear ImGui
into the build directory. They are not installed globally.

## Open in Visual Studio 2026

1. Start Visual Studio 2026.
2. Select **Open a local folder**.
3. Open this project folder.
4. Allow CMake configuration to finish.
5. Select `GameForgerEditor.exe` as the startup item if it is not already selected.
6. Press `F5`.

You can also right-click this folder in Explorer and choose **Open with Visual
Studio** when that shell integration is installed.

## Command-line build

Run from a Visual Studio Developer PowerShell:

```powershell
cmake --preset windows-x64
cmake --build --preset editor-debug
.\out\build\windows-x64\Editor\Debug\GameForgerEditor.exe
```

## Generated targets

- `GameForgerEngine`: reusable C++ engine library
- `GameForgerEditor`: Dear ImGui docking editor
- `GameForgerRuntime`: small standalone game runner

## Included foundations

- OpenGL 4.6 context through GLFW and GLAD
- Dear ImGui docking editor shell
- Separate engine, editor, and runtime targets
- GLM mathematics
- Skeleton and animation clip data types
- `HumanoidV1` semantic skeleton profile
- AI editor-command type placeholders
- Game assets, scenes, characters, animations, and Lua script folders

## Next implementation milestones

1. Add a framebuffer-backed scene viewport and camera.
2. Add ECS scene entities and serialization.
3. Add glTF/GLB import with skeletal skinning.
4. Add animation sampling, blending, root motion, and IK.
5. Embed Lua and expose safe gameplay APIs.
6. Add the validated AI command bus and undo/redo.

Do not let an AI response modify scene memory directly. Parse it into typed
commands, validate every command, then apply it through the same undoable command
system used by the editor UI.
'@

$openProject = @'
@echo off
setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo Visual Studio Installer's vswhere.exe was not found.
    echo Open Visual Studio 2026 and select "Open a local folder", then choose:
    echo %~dp0
    pause
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Workload.NativeDesktop -property productPath`) do (
    set "DEVENV=%%i"
)

if not defined DEVENV (
    echo A Visual Studio installation with Desktop development with C++ was not found.
    echo Open .vsconfig in this folder to install the required components.
    pause
    exit /b 1
)

start "" "%DEVENV%" "%~dp0"
'@

$projectJson = @'
{
  "format": "GameForgerProject",
  "version": 1,
  "name": "__PROJECT_NAME__",
  "startupScene": "Game/Scenes/Startup.gfai",
  "skeletonProfile": "Game/Characters/HumanoidV1.skeleton.json",
  "assetDirectories": [
    "Game/Characters",
    "Game/Animations",
    "Game/Textures",
    "Game/Audio"
  ],
  "scriptDirectory": "Game/Scripts"
}
'@

$startupScene = @'
{
  "format": "GameForgerScene",
  "version": 1,
  "name": "Startup",
  "entities": [
    {
      "name": "World",
      "components": {
        "Transform": {
          "position": [0.0, 0.0, 0.0],
          "rotation": [0.0, 0.0, 0.0, 1.0],
          "scale": [1.0, 1.0, 1.0]
        }
      }
    }
  ]
}
'@

$keep = @'
This file keeps the otherwise empty directory in source control.
'@

Write-ProjectFile 'CMakeLists.txt' $cmakeLists
Write-ProjectFile 'CMakePresets.json' $presets
Write-ProjectFile '.vsconfig' $vsConfig
Write-ProjectFile '.gitignore' $gitIgnore
Write-ProjectFile 'README.md' $readme
Write-ProjectFile 'Open-Project.cmd' $openProject

Write-ProjectFile 'Engine/CMakeLists.txt' $engineCmake
Write-ProjectFile 'Engine/include/GameForger/Core/Engine.hpp' $engineHeader
Write-ProjectFile 'Engine/include/GameForger/Animation/Skeleton.hpp' $skeletonHeader
Write-ProjectFile 'Engine/include/GameForger/Animation/AnimationClip.hpp' $animationHeader
Write-ProjectFile 'Engine/src/Core/Engine.cpp' $engineSource
Write-ProjectFile 'Engine/src/Animation/Skeleton.cpp' $skeletonSource
Write-ProjectFile 'Engine/src/Animation/AnimationClip.cpp' $animationSource

Write-ProjectFile 'Editor/CMakeLists.txt' $editorCmake
Write-ProjectFile 'Editor/include/GameForger/Editor/AICommand.hpp' $aiCommandHeader
Write-ProjectFile 'Editor/src/main.cpp' $editorMain

Write-ProjectFile 'Runtime/CMakeLists.txt' $runtimeCmake
Write-ProjectFile 'Runtime/src/main.cpp' $runtimeMain

Write-ProjectFile 'Game/Project.json' $projectJson
Write-ProjectFile 'Game/Characters/HumanoidV1.skeleton.json' $humanoidProfile
Write-ProjectFile 'Game/Scenes/Startup.gfai' $startupScene
Write-ProjectFile 'Game/Scripts/player_controller.lua' $playerLua
Write-ProjectFile 'Game/Animations/.keep' $keep
Write-ProjectFile 'Game/Audio/.keep' $keep
Write-ProjectFile 'Game/Textures/.keep' $keep

Write-Host ''
Write-Host 'GameForgerAI project created successfully.' -ForegroundColor Green
Write-Host "Location: $Destination"
Write-Host ''
Write-Host 'Next: double-click Open-Project.cmd or use Visual Studio 2026 -> Open a local folder.'
Write-Host 'The first CMake configure downloads the pinned OpenGL/editor dependencies.'
