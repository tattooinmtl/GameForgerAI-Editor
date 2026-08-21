add_executable(GameForgerEditor
    src/main.cpp
    src/AIProviderClient.cpp
    src/AIChatResponse.cpp
    src/ScriptGenerator.cpp
    src/AICommandPlanner.cpp
    src/AIAnimationGenerator.cpp
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
        GameForgerGLAD
        glfw
        glm::glm
)

if(WIN32)
    target_link_libraries(GameForgerEditor PRIVATE winhttp shell32)
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
