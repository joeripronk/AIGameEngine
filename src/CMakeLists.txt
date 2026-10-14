# Forge Engine Core Library
add_library(Forge STATIC)

target_include_directories(Forge
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/vendor/glm
        ${CMAKE_SOURCE_DIR}/vendor/entt/src/include
        ${CMAKE_SOURCE_DIR}/vendor/spdlog/include
        ${CMAKE_SOURCE_DIR}/vendor/fmt/include
        ${CMAKE_SOURCE_DIR}/vendor/miniaudio
        ${CMAKE_SOURCE_DIR}/vendor/lua
        ${CMAKE_SOURCE_DIR}/vendor/stb
        ${CMAKE_SOURCE_DIR}/vendor/nvrhi
        ${CMAKE_SOURCE_DIR}/vendor/nvrhi/vulkan
        ${CMAKE_SOURCE_DIR}/vendor/assimp/include
        ${CMAKE_SOURCE_DIR}/vendor/physx/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_sources(Forge
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
        Forge/Renderer/Mesh.h
        Forge/Renderer/Mesh.cpp
        Forge/Renderer/Camera.h
        Forge/Renderer/Camera.cpp
        Forge/Renderer/EditorCamera.h
        Forge/Renderer/EditorCamera.cpp
        Forge/Renderer/Shaders.h
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
        Forge/Physics/Physics.h
        Forge/Physics/Physics.cpp
        Forge/Scripting/ScriptEngine.h
        Forge/Scripting/ScriptEngine.cpp
        Forge/Scripting/ScriptableEntity.h
        Forge/Scripting/ScriptableEntity.cpp
        Forge/Audio/AudioEngine.h
        Forge/Audio/AudioEngine.cpp
        Forge/Debug/Instrumentor.h
        Platform/Vulkan/VulkanWindow.h
        Platform/Vulkan/VulkanWindow.cpp
        Platform/Vulkan/VulkanRendererAPI.h
        Platform/Vulkan/VulkanRendererAPI.cpp
        Platform/Vulkan/VulkanVertexArray.h
        Platform/Vulkan/VulkanVertexArray.cpp
        Platform/Vulkan/VulkanVertexBuffer.h
        Platform/Vulkan/VulkanVertexBuffer.cpp
        Platform/Vulkan/VulkanIndexBuffer.h
        Platform/Vulkan/VulkanIndexBuffer.cpp
        Platform/Vulkan/VulkanShader.h
        Platform/Vulkan/VulkanShader.cpp
        Platform/Vulkan/VulkanMesh.h
        Platform/Vulkan/VulkanMesh.cpp
        Platform/Vulkan/VulkanTexture.h
        Platform/Vulkan/VulkanTexture.cpp
        # Lua sources (C files compiled as C++)
        ${CMAKE_SOURCE_DIR}/vendor/lua/lapi.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lapi.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/lauxlib.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lauxlib.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/lbaselib.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lcode.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lcode.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/lcorolib.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lctype.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lctype.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/ldblib.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/ldebug.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/ldebug.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/ldo.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/ldo.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/ldump.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lfunc.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lfunc.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/lgc.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lgc.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/linit.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/liolib.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/ljumptab.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/llex.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/llex.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/llimits.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/lmathlib.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lmem.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lmem.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/loadlib.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lobject.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lobject.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/lopcodes.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lopcodes.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/lopnames.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/loslib.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lparser.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lparser.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/lprefix.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/lstate.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lstate.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/lstring.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lstring.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/lstrlib.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/ltable.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/ltable.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/ltablib.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/ltm.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/ltm.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/lua.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lua.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/luaconf.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/lualib.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/lundump.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lundump.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/lutf8lib.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lvm.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lvm.h
        ${CMAKE_SOURCE_DIR}/vendor/lua/lzio.c
        ${CMAKE_SOURCE_DIR}/vendor/lua/lzio.h
)

target_compile_definitions(Forge PRIVATE LUA_USE_C89)

# Dependencies
find_package(Threads REQUIRED)

# GLFW
set(GLFW_BUILD_WAYLAND OFF)
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/glfw ${CMAKE_BINARY_DIR}/vendor/glfw)

# nvrhi
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/nvrhi ${CMAKE_BINARY_DIR}/vendor/nvrhi)

# glm (header-only)
# entt (header-only)

# spdlog
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/spdlog ${CMAKE_BINARY_DIR}/vendor/spdlog)

# fmt
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/fmt ${CMAKE_BINARY_DIR}/vendor/fmt)

# miniaudio
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/miniaudio ${CMAKE_BINARY_DIR}/vendor/miniaudio)

# lua (included directly as sources)

# stb (header-only)

# assimp
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/assimp ${CMAKE_BINARY_DIR}/vendor/assimp)

# physx
# add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/physx)

target_link_libraries(Forge
    PRIVATE
        glfw
        nvrhi
        spdlog::spdlog
        fmt::fmt
        miniaudio
        Threads::Threads
)

# Set properties
set_target_properties(Forge PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)
