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
        Forge/Renderer/Texture.h
        Forge/Renderer/Texture.cpp
        Forge/Renderer/Mesh.h
        Forge/Renderer/Mesh.cpp
        Forge/Renderer/Camera.h
        Forge/Renderer/Camera.cpp
        Forge/Renderer/EditorCamera.h
        Forge/Renderer/EditorCamera.cpp
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
)

# Dependencies
find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)

# GLFW
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/glfw)

# nvrhi
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/nvrhi)

# glm (header-only)
# entt (header-only)

# spdlog
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/spdlog)

# fmt
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/fmt)

# miniaudio
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/miniaudio)

# lua
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/lua)

# stb (header-only)

# assimp
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/assimp)

# physx
# add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/physx)

target_link_libraries(Forge
    PRIVATE
        glfw
        nvrhi
        spdlog::spdlog
        fmt::fmt
        miniaudio
        ${Vulkan_LIBRARIES}
        Threads::Threads
)

# Set properties
set_target_properties(Forge PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)
