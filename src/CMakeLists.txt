# Forge Engine Core Library (no Vulkan)
add_library(ForgeCore STATIC)

target_include_directories(ForgeCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/vendor/glm
        ${CMAKE_SOURCE_DIR}/vendor/entt/single_include
        ${CMAKE_SOURCE_DIR}/vendor/spdlog/include
        ${CMAKE_SOURCE_DIR}/vendor/fmt/include
        ${CMAKE_SOURCE_DIR}/vendor/miniaudio
        ${CMAKE_SOURCE_DIR}/vendor/lua
        ${CMAKE_SOURCE_DIR}/vendor/stb
        ${CMAKE_SOURCE_DIR}/vendor/nvrhi/include
        ${CMAKE_SOURCE_DIR}/vendor/assimp/include
        ${CMAKE_BINARY_DIR}/vendor/assimp/include
        ${CMAKE_SOURCE_DIR}/vendor/glfw/include
        ${CMAKE_SOURCE_DIR}/vendor/yaml-cpp/include
)

target_sources(ForgeCore
    PRIVATE
        Forge/Core/Base.h
        Forge/Core/PlatformDetection.h
        Forge/Core/Log.h
        Forge/Core/Log.cpp
        Forge/Core/Assert.h
        Forge/Core/UUID.h
        Forge/Core/UUID.cpp
        Forge/Core/Timestep.h
        Forge/Core/Layer.h
        Forge/Core/Layer.cpp
        Forge/Core/LayerStack.h
        Forge/Core/LayerStack.cpp
        Forge/Core/EntryPoint.h
        Forge/Events/Event.h
        Forge/Events/ApplicationEvent.h
        Forge/Events/KeyEvent.h
        Forge/Events/MouseEvent.h
        Forge/Renderer/Camera.h
        Forge/Renderer/Camera.cpp
        Forge/Renderer/EditorCamera.h
        Forge/Renderer/EditorCamera.cpp
        Forge/Renderer/CpuTexture.h
        Forge/Renderer/CpuTexture.cpp
        Forge/Renderer/CpuMesh.h
        Forge/Renderer/CpuMesh.cpp
        Forge/Renderer/Mesh.h
        Forge/Renderer/Mesh.cpp
        Forge/Renderer/Texture.h
        Forge/Renderer/Texture.cpp
        Forge/Scene/Components.h
        Forge/Scene/Entity.h
        Forge/Scene/Entity.cpp
        Forge/Scene/Scene.h
        Forge/Scene/Scene.cpp
        Forge/Scene/SceneSerializer.h
        Forge/Scene/SceneSerializer.cpp
        Forge/Scene/Prefab.h
        Forge/Scene/Prefab.cpp
        Forge/Editor/EditorContext.h
        Forge/Editor/EditorContext.cpp
        Forge/Editor/Gizmo.h
        Forge/Editor/Gizmo.cpp
        Forge/Assets/AssetRegistry.h
        Forge/Assets/AssetRegistry.cpp
        Forge/Project/GameExporter.h
        Forge/Project/GameExporter.cpp
        Forge/Physics/Physics.h
        Forge/Physics/Physics.cpp
        Forge/Scripting/ScriptEngine.h
        Forge/Scripting/ScriptEngine.cpp
        Forge/Scripting/ScriptableEntity.h
        Forge/Scripting/ScriptableEntity.cpp
        Forge/Audio/AudioEngine.h
        Forge/Audio/AudioEngine.cpp
        Forge/Audio/ReverbProcessor.h
        Forge/Audio/ReverbProcessor.cpp
        Forge/Core/BuildConfig.h
        Forge/Core/BuildConfig.cpp
        Forge/Debug/Instrumentor.h
)

# Lua: compile as a separate C library
add_library(lua STATIC
    ${CMAKE_SOURCE_DIR}/vendor/lua/lapi.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lauxlib.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lbaselib.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lcode.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lcorolib.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lctype.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/ldblib.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/ldebug.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/ldo.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/ldump.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lfunc.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lgc.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/linit.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/liolib.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/llex.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lmathlib.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lmem.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/loadlib.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lobject.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lopcodes.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/loslib.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lparser.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lstate.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lstring.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lstrlib.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/ltable.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/ltablib.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/ltm.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lundump.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lutf8lib.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lvm.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lzio.c
)
# GNU/Clang only. MSVC rejects both with D8021 "invalid numeric argument" and
# fails the build outright: it reads "-std=gnu90" as switches. The C standard
# matters - Lua 5.4 is C89 - but the warning suppression is a convenience for
# upstream's own style, not something to impose on a compiler that cannot parse
# it.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lua PRIVATE -std=gnu90 -Wno-unused-parameter)
elseif(MSVC)
    # MSVC has no C89/C90 switch. /std:c11 is the closest available and Lua
    # compiles cleanly under it.
    target_compile_options(lua PRIVATE /std:c11)
endif()
target_include_directories(lua PUBLIC ${CMAKE_SOURCE_DIR}/vendor/lua)

# ---------------------------------------------------------------------------
# Third-party dependencies (declared before targets so their include dirs and
# aliases are available to every target below).
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)

set(GLFW_BUILD_WAYLAND OFF)
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/glfw ${CMAKE_BINARY_DIR}/vendor/glfw)


# The Vulkan SDK: the loader library to link. The loader is what matters at run
# time, and it comes from the system/SDK package rather than from nvrhi's fetch.
#
# Deliberately placed *after* nvrhi. nvrhi must not be given the SDK headers: it
# requires >= 1.4.318 and refuses to compile against anything older, and Ubuntu
# 24.04 ships 1.4.303. nvrhi only falls back to fetching its own pinned headers
# when no Vulkan::Headers target exists yet, so finding Vulkan first silently
# substitutes the system headers for the version nvrhi is written against. The
# order reads backwards on purpose; the comment is here because it was tried the
# other way round and only failed on a machine with an old SDK.
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/nvrhi ${CMAKE_BINARY_DIR}/vendor/nvrhi)
find_package(Vulkan REQUIRED)

add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/spdlog ${CMAKE_BINARY_DIR}/vendor/spdlog)

# Scene serialization is the only consumer, and it is what every .forge file is
# read through, so yaml-cpp is a hard dependency. It was previously linked as a
# bare name with nothing providing it: not a submodule, no find_package, no
# target. The build only worked on a machine whose distribution happened to
# ship libyaml-cpp-dev, so CI failed with "yaml-cpp/yaml.h: No such file or
# directory" on a build that had been green locally for months.
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/yaml-cpp ${CMAKE_BINARY_DIR}/vendor/yaml-cpp)

# yaml-cpp 0.8 exports yaml-cpp::yaml-cpp. The bare name is kept as an alias so
# the existing link lines below name the same library they always did.
if(TARGET yaml-cpp::yaml-cpp AND NOT TARGET yaml-cpp)
    add_library(yaml-cpp ALIAS yaml-cpp::yaml-cpp)
endif()

# fmt as a plain library. Its C++20 module target is off because nothing in
# this project imports it - there is no `import fmt` and no target links
# fmt-module - so building it is work whose only effect is to add a fragile
# compiler path. GCC 15 ICEs in cp/module.cc compiling fmt.cc as a module,
# which fails the build with an internal compiler error that names neither fmt
# nor CMake and gives no hint that an unused target is the cause.
set(FMT_MODULE OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/fmt ${CMAKE_BINARY_DIR}/vendor/fmt)

add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/miniaudio ${CMAKE_BINARY_DIR}/vendor/miniaudio)

# assimp (engine only - skip its bundled test suite and tools)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/assimp ${CMAKE_BINARY_DIR}/vendor/assimp EXCLUDE_FROM_ALL)

# The include directory of the Vulkan headers actually in use, so every Vulkan
# target compiles against the same ones rather than whatever the system happens
# to have. Imported targets do not propagate out of the directory that created
# them, so the property is captured here and passed on explicitly; editor/ does
# the same for its own targets.
#
# The value is a generator expression ($<BUILD_INTERFACE:...>) because it comes
# from a FetchContent target, and it works in target_include_directories - the
# expansion happens per-consuming-target rather than here. Reading it as a plain
# string would give the unexpanded text.
set(FORGE_VK_HEADERS_INCLUDE "")
foreach(_forge_vk_headers_target Vulkan::Headers Vulkan-Headers)
    if(TARGET ${_forge_vk_headers_target})
        get_target_property(FORGE_VK_HEADERS_INCLUDE ${_forge_vk_headers_target} INTERFACE_INCLUDE_DIRECTORIES)
        break()
    endif()
endforeach()
unset(_forge_vk_headers_target)




# nvrhi's Vulkan backend forces VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 but only
# initializes the Vulkan-Hpp dispatcher when NVRHI_SHARED_LIBRARY_BUILD is
# defined (normally shared builds). Because we link nvrhi statically, define it
# here so createDevice() initializes the dynamic dispatcher; otherwise every
# vk::Device call goes through an uninitialized (null) loader and aborts.
if(TARGET nvrhi AND NOT NVRHI_BUILD_SHARED)
    target_compile_definitions(nvrhi PRIVATE NVRHI_SHARED_LIBRARY_BUILD=1)
endif()
if(TARGET nvrhi_vk AND NOT NVRHI_BUILD_SHARED)
    target_compile_definitions(nvrhi_vk PRIVATE NVRHI_SHARED_LIBRARY_BUILD=1)
endif()

target_compile_definitions(ForgeCore PRIVATE LUA_USE_C89 FG_NO_VULKAN_RUNTIME)

# Vulkan platform implementation (separate from core for testability)
add_library(vulkan STATIC)
target_sources(vulkan PRIVATE
    Platform/Vulkan/VulkanRendererAPI.cpp
    Platform/Vulkan/VulkanMesh.cpp
    Platform/Vulkan/VulkanTexture.cpp
    Platform/Vulkan/VulkanShader.cpp
    Platform/Vulkan/VulkanVertexArray.cpp
    Platform/Vulkan/VulkanVertexBuffer.cpp
    Platform/Vulkan/VulkanIndexBuffer.cpp
    Platform/Vulkan/VulkanSwapchain.cpp
    Platform/Vulkan/PbrRenderer.cpp
    Platform/Vulkan/VulkanWindow.cpp
)
target_include_directories(vulkan
    PRIVATE
        ${FORGE_VK_HEADERS_INCLUDE}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/vendor/glm
        ${CMAKE_SOURCE_DIR}/vendor/entt/single_include
        ${CMAKE_SOURCE_DIR}/vendor/spdlog/include
        ${CMAKE_SOURCE_DIR}/vendor/fmt/include
        ${CMAKE_SOURCE_DIR}/vendor/nvrhi/include
        ${CMAKE_SOURCE_DIR}/vendor/glfw/include
        ${CMAKE_SOURCE_DIR}/vendor/stb
        ${CMAKE_SOURCE_DIR}/vendor/assimp/include
        ${CMAKE_BINARY_DIR}/vendor/assimp/include
)
target_compile_definitions(vulkan PRIVATE FG_DEBUG GLFW_INCLUDE_VULKAN)
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(vulkan PRIVATE /std:c++20)
else()
    target_compile_options(vulkan PRIVATE -std=c++20)
endif()

# stb (compile definitions for implementation)
target_compile_definitions(ForgeCore PRIVATE STBI_STATIC STB_IMAGE_IMPLEMENTATION)

target_link_libraries(ForgeCore PUBLIC lua)
target_link_libraries(ForgeCore
    PRIVATE
        glfw
        nvrhi
        spdlog::spdlog
        fmt::fmt
        miniaudio
        yaml-cpp
        assimp
        Threads::Threads
)

# Vulkan backend depends on GLFW (window/input), the nvrhi Vulkan device and
# ForgeCore (for CpuTexture decoding used by VulkanTexture).
# find_package(Vulkan) already ran above nvrhi, which is what lets nvrhi use
# these imported targets instead of fetching its own headers.
target_link_libraries(vulkan
    PRIVATE
        glfw
        nvrhi_vk
        nvrhi
        ForgeCore
        Vulkan::Vulkan
)

# ---------------------------------------------------------------------------
# ForgeApp: the platform layer (window + renderer + application shell).
#
# ForgeCore above is deliberately headless (FG_NO_VULKAN_RUNTIME) so unit tests
# can exercise the ECS, physics and scripting without a graphics device.
# ForgeApp carries the window/renderer/app sources, links the Vulkan backend,
# and is what the editor and runtime executables link against.
# ---------------------------------------------------------------------------
add_library(ForgeApp STATIC)
target_sources(ForgeApp
    PRIVATE
        Forge/Core/Window.h
        Forge/Core/Window.cpp
        Forge/Core/Application.h
        Forge/Core/Application.cpp
        Forge/Renderer/Renderer.h
        Forge/Renderer/Renderer.cpp
        Forge/Renderer/RenderCommand.h
        Forge/Renderer/RenderCommand.cpp
        Forge/Renderer/RendererAPI.h
        Forge/Renderer/RendererAPI.cpp
        Forge/Renderer/Shader.h
        Forge/Renderer/Shader.cpp
        Forge/Renderer/ShaderLibrary.h
        Forge/Renderer/ShaderLibrary.cpp
        Forge/Renderer/Shaders.h
        Forge/Renderer/ViewportRenderer.h
        Forge/Renderer/ViewportRenderer.cpp
        Forge/Renderer/VertexArray.h
        Forge/Renderer/VertexBuffer.h
        Forge/Renderer/IndexBuffer.h
)
target_include_directories(ForgeApp
    PUBLIC
        ${FORGE_VK_HEADERS_INCLUDE}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/vendor/glm
        ${CMAKE_SOURCE_DIR}/vendor/nvrhi/include
        ${CMAKE_SOURCE_DIR}/vendor/glfw/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/Vulkan
)
target_compile_definitions(ForgeApp PRIVATE STBI_STATIC)
target_link_libraries(ForgeApp
    PUBLIC
        ForgeCore
        vulkan
        nvrhi_vk
        glfw
        spdlog::spdlog
        fmt::fmt
        yaml-cpp
        Threads::Threads
        nvrhi
)
set_target_properties(ForgeApp PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ForgeApp PRIVATE -Wno-pedantic -Werror)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(ForgeApp PRIVATE /WX)
endif()

# nvrhi's Vulkan backend expects the Vulkan-Hpp dynamic dispatch storage
# (vk::detail::defaultDispatchLoaderDynamic) to be provided by a single
# translation unit. Newer Vulkan-Hpp releases gate that definition behind the
# C++20 module path, so we provide it explicitly in a tiny static library. Link
# this AFTER nvrhi_vk (editor/runtime do) so the symbol resolves.
add_library(forge_vk_dispatch STATIC
    Platform/Vulkan/VulkanDispatch.cpp
)
target_link_libraries(forge_vk_dispatch PUBLIC nvrhi_vk)
target_include_directories(forge_vk_dispatch PRIVATE ${FORGE_VK_HEADERS_INCLUDE})

# Set properties
set_target_properties(ForgeCore PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

# Enable -Werror for engine code only
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ForgeCore PRIVATE -Wno-pedantic)
    target_compile_options(ForgeCore PRIVATE -Werror)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(ForgeCore PRIVATE /WX)
endif()
