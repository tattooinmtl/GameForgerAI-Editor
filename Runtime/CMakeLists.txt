add_executable(GameForgerRuntime
    src/main.cpp
    # Runtime-exclusive Esc-to-pause menu (mouse sensitivity + FPS cap) -
    # kept local rather than moved to Engine/ like Phases 1-3's shared
    # gameplay/rendering code, since nothing else (the Editor has ImGui)
    # would ever use it - same reasoning as keeping findPickupCandidate
    # local to main.cpp.
    src/GameMenu.cpp
)

if(WIN32)
    target_sources(GameForgerRuntime PRIVATE resources/app.rc)
endif()

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
    # See GameForgerEditor's matching property for why.
    WIN32_EXECUTABLE ON
)

if(MSVC)
    target_link_options(GameForgerRuntime PRIVATE "/ENTRY:mainCRTStartup")
endif()
