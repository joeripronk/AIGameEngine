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
        ${CMAKE_SOURCE_DIR}/vendor/nvrhi/vulkan
        ${CMAKE_SOURCE_DIR}/vendor/assimp/include
        ${CMAKE_BINARY_DIR}/vendor/assimp/include
        ${CMAKE_SOURCE_DIR}/vendor/physx/include
        /usr/include
        ${CMAKE_SOURCE_DIR}/vendor/glfw/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/Platform/Vulkan  # For null stubs in RendererAPI.cpp
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
        Forge/Core/Window.h
        Forge/Core/Window.cpp
        Forge/Core/Layer.h
        Forge/Core/Layer.cpp
        Forge/Core/LayerStack.h
        Forge/Core/LayerStack.cpp
        Forge/Core/Application.h
        Forge/Core/Application.cpp
        Forge/Core/EntryPoint.h
        Forge/Events/Event.h
        Forge/Events/ApplicationEvent.h
        Forge/Events/KeyEvent.h
        Forge/Events/MouseEvent.h
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
        Forge/Renderer/Texture.h
        Forge/Renderer/Texture.cpp
        Forge/Renderer/CpuTexture.h
        Forge/Renderer/CpuTexture.cpp
        Forge/Renderer/CpuMesh.h
        Forge/Renderer/CpuMesh.cpp
        Forge/Renderer/Mesh.h
        Forge/Renderer/Mesh.cpp
        Forge/Renderer/Camera.h
        Forge/Renderer/Camera.cpp
        Forge/Renderer/EditorCamera.h
        Forge/Renderer/EditorCamera.cpp
        Forge/Renderer/Shaders.h
        Forge/Renderer/ViewportRenderer.h
        Forge/Renderer/ViewportRenderer.cpp
        Forge/Renderer/VertexArray.h
        Forge/Renderer/VertexBuffer.h
        Forge/Renderer/IndexBuffer.h
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
    ${CMAKE_SOURCE_DIR}/vendor/lua/lua.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lundump.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lutf8lib.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lvm.c
    ${CMAKE_SOURCE_DIR}/vendor/lua/lzio.c
)
target_compile_options(lua PRIVATE -std=gnu90 -Wno-unused-parameter)
target_include_directories(lua PUBLIC ${CMAKE_SOURCE_DIR}/vendor/lua)

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
    Platform/Vulkan/VulkanWindow.cpp
)
target_include_directories(vulkan
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/vendor/glm
        ${CMAKE_SOURCE_DIR}/vendor/entt/single_include
        ${CMAKE_SOURCE_DIR}/vendor/spdlog/include
        ${CMAKE_SOURCE_DIR}/vendor/fmt/include
        ${CMAKE_SOURCE_DIR}/vendor/nvrhi/include
        ${CMAKE_SOURCE_DIR}/vendor/nvrhi/vulkan
        ${CMAKE_SOURCE_DIR}/vendor/glfw/include
        ${CMAKE_SOURCE_DIR}/vendor/stb
        ${CMAKE_SOURCE_DIR}/vendor/assimp/include
        ${CMAKE_BINARY_DIR}/vendor/assimp/include
        /usr/include
)
target_compile_definitions(vulkan PRIVATE FG_DEBUG)
target_compile_options(vulkan PRIVATE -std=gnu++20)

# Dependencies
find_package(Threads REQUIRED)

# GLFW
set(GLFW_BUILD_WAYLAND OFF)
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/glfw ${CMAKE_BINARY_DIR}/vendor/glfw)

# nvrhi (header-only for core)
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/nvrhi ${CMAKE_BINARY_DIR}/vendor/nvrhi)

# glm (header-only)
# entt (header-only)

# spdlog
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/spdlog ${CMAKE_BINARY_DIR}/vendor/spdlog)

# fmt
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/fmt ${CMAKE_BINARY_DIR}/vendor/fmt)

# miniaudio
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/miniaudio ${CMAKE_BINARY_DIR}/vendor/miniaudio)

# stb (header-only)

# assimp (engine only - skip its bundled test suite and tools)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/assimp ${CMAKE_BINARY_DIR}/vendor/assimp EXCLUDE_FROM_ALL)

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

# Vulkan backend depends on GLFW (window/input) and the nvrhi Vulkan device.
target_link_libraries(vulkan
    PRIVATE
        glfw
        nvrhi_vk
)

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
