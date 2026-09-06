add_library(GameForgerEngine STATIC
    src/Core/Engine.cpp
    src/Core/ProjectPaths.cpp
    src/Core/AssetDatabase.cpp
    src/Core/Material.cpp
    src/Core/AudioEngine.cpp
    # Per-frame CPU timing split into named zones, with the worst frames kept
    # so a dip can be inspected after the fact. In Engine so Runtime can use
    # the same instrumentation later.
    src/Core/FrameProfiler.cpp
    src/Animation/Skeleton.cpp
    src/Animation/AnimationClip.cpp
    # Scene data model + serialization + mesh generation - shared between
    # GameForgerEditor and GameForgerRuntime (moved here from Editor/ so
    # Runtime can actually load and render a scene instead of only ever
    # opening a blank window - see NewAudit.md finding H1). Namespace stays
    # gameforger::editor and headers stay under GameForger/Editor/ even
    # though they now live in this library - purely a physical relocation,
    # not a rename, to avoid touching every #include/using site across the
    # whole codebase for a cosmetic change.
    src/Editor/Json.cpp
    src/Editor/AICommandBus.cpp
    # Typed view of Game/Project.json + Game/Settings.json, plus the command
    # bus that is the only writer to it. Lives in Engine (not Editor) because
    # GameForgerRuntime reads the same settings - notably the boot sequence,
    # which must play identically standalone and in the Editor's Play mode.
    src/Editor/ProjectSettings.cpp
    src/Editor/ProjectSettingsBus.cpp
    src/Editor/EditorScene.cpp
    src/Editor/SceneSerializer.cpp
    # CineShot + AudioCue, lifted out of main.cpp so shots can be serialised
    # into the scene file instead of being lost when the editor closes.
    src/Editor/Storyboard.cpp
    src/Editor/Transform.cpp
    src/Editor/PrimitiveMeshes.cpp
    src/Editor/Terrain.cpp
    src/Editor/TextMesh.cpp
    src/Editor/ModelImport.cpp
    src/Editor/Collision.cpp
    src/Editor/TerrainTexture.cpp
    src/Editor/StbImageImpl.cpp
    src/Editor/StbTrueTypeImpl.cpp
    src/Editor/Animation.cpp
    # Embeds/runs entity Lua scripts (see ScriptRuntime.hpp) - moved here
    # (from Editor/) alongside ViewportRenderer/InputSource below so
    # GameForgerRuntime can run the exact same Play-mode gameplay loop the
    # Editor does, standalone, with no ImGui context. ScriptRuntime never
    # calls ImGui directly - keyboard state comes through whichever
    # InputSource the host constructs (ImGuiInputSource for the Editor,
    # stays under Editor/ since it needs an active ImGui frame; GlfwInputSource
    # here, for Runtime).
    src/Editor/ScriptRuntime.cpp
    src/Editor/GlfwInputSource.cpp
    # Editor's authoring viewport - moved here (from Editor/) so Runtime can
    # render with full parity (textured/triplanar materials, terrain splat
    # blending, FPS hide-own-body) instead of the narrower SceneRenderer
    # below. Renders into its own offscreen framebuffer either way; Runtime
    # blits that into its real window via blitToCurrentFramebuffer().
    src/Editor/ViewportRenderer.cpp
    # Self-contained borderless splash-image window (own GLFW window/GL
    # context, stb_image + a trivial fullscreen-quad shader, no ImGui) -
    # moved here so GameForgerRuntime can show its own boot splash
    # (Game/Branding/engine.png) the same way the Editor shows logo.jpg.
    src/Editor/SplashScreen.cpp
    # The Play-mode gameplay tick loop (script updates, projectile travel,
    # keyframe animation, parent-constraint resolution) - moved here (from
    # Editor/'s main.cpp) so GameForgerRuntime can drive the exact same
    # logic standalone, not just the Editor's own Play sandbox.
    src/Runtime/GameplayLoop.cpp
    # Scripted-camera math (yaw/pitch/distance framing for the followed
    # entity's self.camera:setMode fps/third_person view) - moved here
    # alongside GameplayLoop.cpp for the same reason.
    src/Runtime/GameCamera.cpp
)

add_library(GameForger::Engine ALIAS GameForgerEngine)

target_include_directories(GameForgerEngine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_libraries(GameForgerEngine
    PUBLIC
        glm::glm
        assimp::assimp
        # ViewportRenderer.hpp includes glad/gl.h and calls glfwGetTime() for
        # skinning-animation playback time - PUBLIC since both consumers
        # (Editor, Runtime) need the actual GL function pointers/GLFW loaded
        # in their own main() anyway, not something worth hiding behind this
        # library's interface.
        GameForgerGLAD
        glfw
    PRIVATE
        # Transform.cpp's composeEntityPivotFrame reuses ImGuizmo's own
        # RecomposeMatrixFromComponents (pure TRS-composition math, no
        # dependency on an active ImGui frame/context) rather than
        # reimplementing the exact same euler-order math a second time and
        # risking a silent mismatch with every rotation gizmo/animation
        # already built against the original - PRIVATE since nothing in
        # this library's own public headers exposes ImGui/ImGuizmo types.
        GameForgerImGui
        GameForgerImGuizmo
        # ScriptRuntime.cpp embeds a Lua 5.4 VM (see ScriptRuntime.hpp) -
        # PRIVATE since no public header of this library exposes lua_State
        # or any other Lua type.
        GameForgerLua
        miniaudio
)

if(WIN32)
    # miniaudio WASAPI / COM on Windows.
    target_link_libraries(GameForgerEngine PRIVATE ole32)
endif()

target_compile_features(GameForgerEngine PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(GameForgerEngine PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(GameForgerEngine PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(GameForgerEngine PROPERTIES FOLDER "GameForger")

add_executable(GameForgerTests
    tests/TestMain.cpp
    # Provider response parsing lives in the Editor target, which is an
    # executable and so cannot be linked. Its only dependency is Engine's
    # Json.hpp, so compile the one translation unit straight into the test
    # binary rather than restructuring Editor into a library for it.
    ${CMAKE_CURRENT_SOURCE_DIR}/../Editor/src/AIChatResponse.cpp
)

target_link_libraries(GameForgerTests
    PRIVATE
        GameForgerEngine
)

target_include_directories(GameForgerTests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../Editor/include
)

target_compile_features(GameForgerTests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(GameForgerTests PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(GameForgerTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(GameForgerTests PROPERTIES FOLDER "Tests")

add_test(NAME EngineRegressionTests COMMAND GameForgerTests)
