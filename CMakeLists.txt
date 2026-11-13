cmake_minimum_required(VERSION 3.22.0)
project(Insland LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Diligent Engine backend selection
# ---------------------------------------------------------------------------
# Windows uses D3D11.  Linux and Android use the OpenGL/GLES backend, which
# also translates the engine's HLSL shaders to GLSL at runtime.
# INSLAND_FORCE_OPENGL builds the same OpenGL backend on Windows, which is
# handy for validating the Linux/Android renderer without leaving Windows.
option(INSLAND_FORCE_OPENGL "Build with the OpenGL backend on Windows" OFF)
option(INSLAND_BUILD_PROJECTS "Build the standalone projects under projects/" ON)

if(WIN32 AND NOT INSLAND_FORCE_OPENGL)
    set(DILIGENT_NO_DIRECT3D11 OFF CACHE BOOL "" FORCE)
    set(DILIGENT_NO_OPENGL     ON  CACHE BOOL "" FORCE)
else()
    set(DILIGENT_NO_DIRECT3D11 ON  CACHE BOOL "" FORCE)
    set(DILIGENT_NO_OPENGL     OFF CACHE BOOL "" FORCE)
endif()
set(DILIGENT_NO_DIRECT3D12 ON  CACHE BOOL "" FORCE)
set(DILIGENT_NO_VULKAN     ON  CACHE BOOL "" FORCE)
set(DILIGENT_NO_HLSL       OFF CACHE BOOL "" FORCE)

add_subdirectory(external/DiligentCore)

# ---------------------------------------------------------------------------
# Third-party libraries
# ---------------------------------------------------------------------------

add_library(glm INTERFACE)
target_include_directories(glm INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/external/glm)
add_library(glm::glm ALIAS glm)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/external/stb)
add_library(stb::stb ALIAS stb)

add_library(tinygltf INTERFACE)
target_include_directories(tinygltf INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/external/tinygltf)
add_library(tinygltf::tinygltf ALIAS tinygltf)

add_library(imgui STATIC
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
)
target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/backends
)
add_library(imgui::imgui ALIAS imgui)

set(DRACO_TESTS OFF CACHE BOOL "" FORCE)
set(DRACO_BUILD_EXECUTABLES OFF CACHE BOOL "" FORCE)
set(DRACO_INSTALL OFF CACHE BOOL "" FORCE)
set(DRACO_JS_GLUE OFF CACHE BOOL "" FORCE)
add_subdirectory(external/draco EXCLUDE_FROM_ALL)

set(ozz_build_tools OFF CACHE BOOL "" FORCE)
set(ozz_build_fbx OFF CACHE BOOL "" FORCE)
set(ozz_build_gltf OFF CACHE BOOL "" FORCE)
set(ozz_build_data OFF CACHE BOOL "" FORCE)
set(ozz_build_samples OFF CACHE BOOL "" FORCE)
set(ozz_build_howtos OFF CACHE BOOL "" FORCE)
set(ozz_build_tests OFF CACHE BOOL "" FORCE)
set(ozz_build_msvc_rt_dll ON CACHE BOOL "" FORCE)
add_subdirectory(external/ozz-animation EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Engine library
# ---------------------------------------------------------------------------

set(INSLAND_SOURCES
    source/pipeline.cpp
    source/core/logger.cpp
    source/core/memory.cpp
    source/core/serializer.cpp
    source/app/application.cpp
    source/assets/asset_manager.cpp
    source/assets/draco_loader.cpp
    source/raycast/raycast.cpp
    source/scene/scene.cpp
    source/world/world.cpp
    source/graphics/sky.cpp
    source/ui/ui.cpp
    source/engine/engine.cpp
    source/platform/renderer/renderer.cpp
)

if(WIN32 AND NOT INSLAND_FORCE_OPENGL)
    list(APPEND INSLAND_SOURCES
        source/platform/window/windows.cpp
        source/platform/renderer/d3d11.cpp
    )
    set(INSLAND_GRAPHICS_TARGET Diligent-GraphicsEngineD3D11-static)
elseif(WIN32)
    list(APPEND INSLAND_SOURCES
        source/platform/window/windows.cpp
        source/platform/renderer/opengl.cpp
    )
    set(INSLAND_GRAPHICS_TARGET Diligent-GraphicsEngineOpenGL-static)
elseif(ANDROID)
    list(APPEND INSLAND_SOURCES
        source/platform/window/android.cpp
        source/platform/renderer/opengl.cpp
    )
    set(INSLAND_GRAPHICS_TARGET Diligent-GraphicsEngineOpenGL-static)
else()
    list(APPEND INSLAND_SOURCES
        source/platform/window/linux.cpp
        source/platform/renderer/opengl.cpp
    )
    set(INSLAND_GRAPHICS_TARGET Diligent-GraphicsEngineOpenGL-static)
endif()

add_library(insland STATIC ${INSLAND_SOURCES})

target_include_directories(insland PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_compile_definitions(insland PUBLIC NOMINMAX)

if(WIN32 AND INSLAND_FORCE_OPENGL)
    target_compile_definitions(insland PUBLIC INSLAND_FORCE_OPENGL)
endif()

target_link_libraries(insland PUBLIC
    ${INSLAND_GRAPHICS_TARGET}
    Diligent-Common
    glm::glm
    stb::stb
    tinygltf::tinygltf
    imgui::imgui
    draco::draco
    ozz_base
    ozz_animation
    ozz_geometry
)

if(WIN32)
    target_compile_definitions(insland PUBLIC UNICODE _UNICODE)
    target_compile_options(insland PRIVATE /W3)
elseif(ANDROID)
    target_link_libraries(insland PUBLIC android log)
    # android.cpp pumps the NativeActivity event loop, so the engine needs the
    # glue header.  The glue source itself is only compiled into the game
    # shared library.
    target_include_directories(insland PRIVATE "${ANDROID_NDK}/sources/android/native_app_glue")
else()
    find_package(X11 REQUIRED)
    target_link_libraries(insland PUBLIC X11::X11)
endif()

# ---------------------------------------------------------------------------
# Game / demo
# ---------------------------------------------------------------------------

if(ANDROID)
    # NativeActivity entry point.  android_native_app_glue provides
    # ANativeActivity_onCreate and calls android_main() on the app thread.
    set(ANDROID_GLUE_DIR "${ANDROID_NDK}/sources/android/native_app_glue")
    add_library(InslandGame SHARED
        workspace/game/main.cpp
        workspace/game/android_entry.cpp
        "${ANDROID_GLUE_DIR}/android_native_app_glue.c"
    )
    target_include_directories(InslandGame PRIVATE "${ANDROID_GLUE_DIR}")
    # Keep the entry symbol even though nothing in our code references it.
    target_link_options(InslandGame PRIVATE "-u ANativeActivity_onCreate")
else()
    add_executable(InslandGame workspace/game/main.cpp)
endif()

target_link_libraries(InslandGame PRIVATE insland)

if(WIN32)
    set_target_properties(InslandGame PROPERTIES OUTPUT_NAME "InslandGame")
elseif(ANDROID)
    set_target_properties(InslandGame PROPERTIES OUTPUT_NAME "insland" PREFIX "lib")
endif()

# ---------------------------------------------------------------------------
# Standalone projects (each supplies its own CMakeLists.txt)
# ---------------------------------------------------------------------------

if(INSLAND_BUILD_PROJECTS)
    add_subdirectory(projects)
endif()
