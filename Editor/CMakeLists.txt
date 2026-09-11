add_executable(GameForgerEditor
    src/main.cpp
    src/AIProviderClient.cpp
    src/AIChatResponse.cpp
    src/ScriptGenerator.cpp
    src/ScriptsPanel.cpp
    src/AICommandPlanner.cpp
    src/AIAnimationGenerator.cpp
    # Blender MCP integration (Phase A of integration_plan_allinone.md).
    # HTTP + JSON-RPC 2.0 client that talks to the upstream
    # MCP_Server_blender addon on http://127.0.0.1:8765/mcp.
    src/BlenderClient.cpp
    # Detects blender.exe (env / registry / %ProgramFiles% glob / PATH)
    # and spawns it via CreateProcessW with the MCP addon enabled and
    # its HTTP server started, so BlenderClient can immediately connect.
    src/BlenderLauncher.cpp
    # Phase C of integration_plan_allinone.md. Multi-turn agentic loop that
    # routes tool calls to scene (AICommandBus) or Blender (BlenderClient),
    # gates destructive ops on user approval, and pushes an undo entry per
    # successful mutation into a bounded ring.
    src/AICockpit.cpp
    # Typed inspector over Game/Project.json + Game/Settings.json, including
    # the startup-sequence editor. Every edit goes through
    # ProjectSettingsBus (Engine) so the panel and the AI share one
    # validated write path. Its own TU rather than main.cpp, which is
    # already ~8k lines.
    src/ProjectSettingsPanel.cpp
    # Default dock arrangement (first run / Reset Editor Layout). Separate TU
    # because it needs imgui_internal.h for DockBuilder, whose ImGuiInputSource
    # enum collides with this project's class of the same name in main.cpp.
    src/EditorLayout.cpp
    src/MindGraphPanel.cpp
    # Track view over a storyboard shot: camera keyframes and audio cues on
    # one shared clock, with scrub/drag/delete. Its own TU like the other
    # panels rather than growing main.cpp.
    src/TimelinePanel.cpp
    src/AudioPanel.cpp
    # FPS + frame-time graph + per-zone breakdown of recorded dips.
    src/PerformancePanel.cpp
    # Editor-only InputSource implementation (ImGui-backed) - see
    # GameForger/Editor/InputSource.hpp (Engine, shared interface) and
    # Engine/src/Editor/GlfwInputSource.cpp (Runtime's equivalent).
    # ScriptRuntime/ViewportRenderer/Animation themselves moved to Engine/
    # so GameForgerRuntime can run the same Play-mode gameplay loop and
    # renderer standalone - see Engine/CMakeLists.txt.
    src/ImGuiInputSource.cpp
)

if(WIN32)
    target_sources(GameForgerEditor PRIVATE resources/app.rc)
endif()

# Generates Version.hpp from this project's own project(... VERSION x.y.z)
# (root CMakeLists.txt) - single source of truth for the version shown in
# the window title/title bar, instead of a 4th place to remember to bump
# alongside README.md's Version History and CMakeLists.txt itself.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/GameForger/Editor/Version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/GameForger/Editor/Version.hpp
    @ONLY
)

target_include_directories(GameForgerEditor
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(GameForgerEditor
    PRIVATE
        GameForger::Engine
        GameForgerImGui
        GameForgerImGuizmo
        GameForgerNodeEditor
        GameForgerGLAD
        glfw
        glm::glm
)

if(WIN32)
    # winhttp: AIProviderClient + BlenderClient outbound HTTP.
    # shell32: file dialogs + ShellExecute (Blender install fallback URL).
    # Advapi32: RegOpenKeyEx / RegQueryValueEx for BlenderLauncher's HKCR
    #           blendfile\shell\open\command lookup.
    target_link_libraries(GameForgerEditor PRIVATE winhttp shell32 Advapi32)
endif()

target_compile_features(GameForgerEditor PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(GameForgerEditor PRIVATE /W4 /permissive- /EHsc)
endif()

set_target_properties(GameForgerEditor PROPERTIES
    FOLDER "GameForger"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    # No separate console window on launch (CMake links /SUBSYSTEM:WINDOWS
    # and auto-selects the matching CRT entry point, so plain int main()
    # still works without a WinMain) - the app has its own in-editor Console
    # panel for user-facing messages; the raw stderr/stdout stream this
    # hides is a developer-debugging aid only (still capturable by
    # redirecting the process's stderr/stdout when launching it, e.g. from
    # PowerShell - that doesn't require an attached console window either).
    WIN32_EXECUTABLE ON
)

if(MSVC)
    # WIN32_EXECUTABLE alone links /SUBSYSTEM:WINDOWS but MSVC's CRT then
    # expects a WinMain, not the plain int main() this app actually has -
    # force the CRT startup routine back to the normal one.
    target_link_options(GameForgerEditor PRIVATE "/ENTRY:mainCRTStartup")
endif()
