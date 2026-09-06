cmake_minimum_required(VERSION 3.28)

# Version tracks the Alpha/Beta progression in README.md's Version History -
# bump the patch number (0.31 -> 0.32 -> ...) each time a meaningful change
# lands, through Beta 1.0.0 at first public try-out release.
project(GameForgerAI
    VERSION 0.79.0
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

FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG 5ab7676402ace03cdf930b2d972f59c7d03c6fa8
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG v5.4.8
    GIT_SHALLOW TRUE
)

# Model import (Character menu > Import Model...) - only the formats this
# editor's file dialog actually offers are enabled, to keep the build
# reasonable; Assimp supports many more formats than that.
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_3DS_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_BLEND_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INJECT_DEBUG_POSTFIX OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG v5.4.3
    GIT_SHALLOW TRUE
)

# Single-header audio backend (WAV/MP3/FLAC). No CMakeLists in the repo, so
# MakeAvailable only populates the source dir; we wrap it as an INTERFACE
# include below. Public-domain / MIT-0, same class of vendoring as stb_*.
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.22
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(glfw glm glad imgui imguizmo lua assimp miniaudio)

if(NOT TARGET miniaudio)
    add_library(miniaudio INTERFACE)
    target_include_directories(miniaudio INTERFACE ${miniaudio_SOURCE_DIR})
endif()

# Upstream ImGuizmo's own "imguizmo" target compiles GraphEditor.cpp/ImCurveEdit.cpp/ImGradient.cpp
# without an imgui include path (it only adds one under IMGUIZMO_BUILD_EXAMPLE, which we don't set).
# GameForgerImGuizmo below builds ImGuizmo.cpp itself with the correct include dirs, so the upstream
# target is unused here - exclude it from the default build so `all-release`/ALL_BUILD doesn't fail on it.
if(TARGET imguizmo)
    set_target_properties(imguizmo PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

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

add_library(GameForgerImGuizmo STATIC
    ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp
)

target_include_directories(GameForgerImGuizmo
    PUBLIC
        ${imguizmo_SOURCE_DIR}/src
)

target_link_libraries(GameForgerImGuizmo
    PUBLIC
        GameForgerImGui
)

set_target_properties(GameForgerImGuizmo PROPERTIES FOLDER "Dependencies")

# Lua ships no CMakeLists.txt - build the library ourselves from its sources,
# excluding lua.c/onelua.c (each defines its own main()) and ltests.c (an
# internal test harness that needs special build defines).
file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/*.c)
list(FILTER LUA_SOURCES EXCLUDE REGEX ".*/(lua|onelua|ltests)\\.c$")

add_library(GameForgerLua STATIC ${LUA_SOURCES})

target_include_directories(GameForgerLua
    PUBLIC
        ${lua_SOURCE_DIR}
)

set_target_properties(GameForgerLua PROPERTIES
    FOLDER "Dependencies"
    LINKER_LANGUAGE C
)

if(MSVC)
    # Lua's sources are plain C; MSVC needs this to treat them as such rather
    # than inferring from the project's default C++ mode.
    set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE C)
endif()

enable_testing()

add_subdirectory(Engine)
add_subdirectory(Editor)
add_subdirectory(Runtime)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT GameForgerEditor)
