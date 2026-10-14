# Forge Engine - Feature Checklist

## ✅ Completed

### Core Engine
- [x] Application, Window, LayerStack
- [x] Event system (Event, KeyEvent, MouseEvent, ApplicationEvent)
- [x] Logging system (spdlog integration)
- [x] UUID generation and serialization
- [x] Timestep system

### ECS (Entity Component System)
- [x] Scene with enTT registry
- [x] Entity with component add/remove/query
- [x] Scene serialization (YAML `.forge` format)
- [x] Scene round-trip serialization/deserialization
- [x] Scene copy functionality
- [x] Entity duplication
- [x] Find entity by name/UUID

### Components
- [x] IDComponent
- [x] TagComponent
- [x] TransformComponent
- [x] CameraComponent
- [x] ScriptComponent
- [x] RigidbodyComponent
- [x] BoxColliderComponent
- [x] SphereColliderComponent
- [x] LightComponent
- [x] PBRMaterialComponent
- [x] MeshComponent

### Scripting
- [x] Lua scripting engine
- [x] NativeScriptComponent base class
- [x] ScriptEngine with Lua bindings
- [x] Entity metatable (GetName, SetPosition, etc.)

### Editor (ImGui)
- [x] Scene hierarchy panel
- [x] Inspector panel
- [x] Viewport panel with render target
- [x] Toolbar (file operations)
- [x] AI-agent controllable API
- [ ] Gizmo manipulation
- [ ] Asset browser
- [ ] Properties panel for global settings

### Runtime
- [x] Standalone runtime application
- [x] Simulation step system

### Build System
- [x] CMake with ForgeCore + vulkan separation
- [x] Lua compiled as C (PUBLIC dependency)
- [x] FG_NO_VULKAN_RUNTIME for testability
- [x] Catch2 test targets

### Build & Architecture
- [x] ForgeCore (headless, testable) + ForgeApp (platform) + Vulkan backend split
- [x] ForgeEditor and ForgeRuntime link and build
- [x] Clean ImGui v1.91.9 vendored (fixes missing BeginMenu/Separator/MenuItem)
- [x] Vulkan dispatch-loader glue for nvrhi + newer Vulkan-Hpp
- [x] VulkanTexture decodes via shared CpuTexture + real GPU upload

### Tests (107 test cases, 1034 assertions - all passing)
- [x] CoreTests (logging, assertions)
- [x] SceneTests (entities, components, serialization)
- [x] ComprehensiveTests (full-component scenes, copy, duplication)
- [x] EngineFeatureTests (serializer, runtime format, physics sync, raycast, prefabs, scripting)
- [x] RendererFeatureTests (cube/plane/sphere primitives, PNG/HDR texture decoding)
- [x] ToolingTests (gizmo math, asset registry, game export, editor context)
- [x] TestSceneTests (loads the comprehensive EngineTest.forge scene and
      verifies every component + physics + the scripting API end-to-end)
- [x] RendererTests (null stubs)
- [x] PhysicsTests (physics engine, collision, integration)
- [x] ScriptingTests (Lua engine)

---

## 🔨 In Progress

### Rendering Pipeline (vulkan target)
- [x] Vulkan bootstrap: instance + adapter selection + logical device + graphics queue
- [x] nvrhi device initialization (static build dispatcher fix)
- [x] GLSL shaders + SPIR-V compilation (tools/compile_shaders.sh; .spv committed)
- [x] VulkanShader loads SPIR-V -> real nvrhi shader modules
- [x] CPU model importer (glTF/OBJ/FBX via assimp)
- [x] Default cube/plane/sphere primitives (CPU, testable)
- [x] Texture loading (stb_image, LDR + Radiance HDR)
- [x] GPU vertex/index buffer upload (nvrhi buffers + deferred writeBuffer upload)
- [x] Vertex input layouts built from shader SPIR-V (createInputLayout)
- [x] Frame command list lifecycle (open / record / close / execute)
- [x] Swapchain + present (VkSwapchainKHR, acquire/present, out-of-date recreate)
- [x] PBR pipeline (PbrRenderer class with nvrhi pipelines, binding layouts, uniform buffers)
- [x] Shadow map (2048x2048 depth texture, shadow pass from light perspective)
- [x] Shadow pass integration with PBR (real shadow view-projection from directional light)
- [x] PBR shader uses real shadow map (not fallback)
- [x] Post-processing framebuffer infrastructure (tonemapped, SSAO buffers)
- [ ] SSAO pass (depth reconstruction - shader requires G-Buffer inputs)
- [ ] HDR tonemapping pass (reads swapchain, applies ACES tone mapping)
- [ ] Final composite pass (reads tonemapped + SSAO, outputs to swapchain)
- [ ] IBL prefiltering / irradiance maps
- [ ] ImGui Vulkan backend init + viewport present

### Editor (AI-controllable)
- [x] Headless EditorContext (scene, selection, play/pause/step, gizmo, save/load)
- [x] Gizmo manipulation (math layer)
- [x] AssetRegistry / asset browser backend
- [x] ImGui asset browser panel
- [x] ImGui inspector (all components), hierarchy, properties, gizmo toolbar
- [ ] ImGui gizmo pointer interaction (drag) — math layer done
- [ ] Actual viewport rendering (needs real Vulkan device + swapchain)

### Assets & Resources
- [x] AssetRegistry / asset browser backend + panel
- [x] CPU texture loading (stb_image LDR + Radiance HDR)
- [x] CPU model import (assimp: glTF/GLB/OBJ/FBX)
- [x] Resource fixtures committed (scene, scripts, prefab, model, texture, audio)
- [ ] IBL (Image-Based Lighting) with HDRIs (needs GPU prefilter passes)
- [ ] Soft shadow maps (needs GPU shadow pass)

### Engine Polish
- [x] Physics-driven transform updates (Scene sync)
- [x] DuplicateEntity copies all components
- [x] SerializeRuntime/DeserializeRuntime (binary format)
- [x] Camera viewport resize handling (CameraComponent::UpdateProjection)
- [x] Mesh loading from file (CPU assimp path; GPU path pending)
- [x] Audio engine (miniaudio ma_engine mixer, WAV/MP3/FLAC decode + playback)

### Deployment
- [ ] Game export (package runtime + scene + resources)
- [ ] Windows support (CMake has WIN32 blocks but no actual testing)
- [ ] macOS support
- [ ] Installer/deployment scripts
- [ ] CI/CD pipeline

---

## 🐛 Known Issues

| Issue | Status |
|-------|--------|
| UUID YAML formatting (scientific notation) | ✅ Fixed - now uses string encoding |
| `glm::vec3` YAML convert template not working | ✅ Fixed - explicit node construction |
| Physics completely stubbed | ✅ Fixed - custom 3D physics engine implemented |
| Editor/runtime could not link | ✅ Fixed - ForgeCore/ForgeApp split, dedup factories, ImGui 1.91.9 |
| Runtime launched the Lua REPL instead of the engine | ✅ Fixed - lua.c removed, cross-platform entry point |
| Init stack overflow (VulkanRendererAPI<->RenderCommand) | ✅ Fixed - removed circular init |
| SetVSync called glfwSwapInterval (OpenGL-only) | ✅ Fixed - no-op for Vulkan surfaces |
| Vulkan-Hpp header version mismatch (system vs fetched) | ✅ Fixed - all Vulkan targets use nvrhi's fetched headers |
| Editor viewport has no swapchain | ⚠️ Renderer backend still largely stubs; needs full pipeline + real GPU |
| Full headless frame (device + swapchain + passes) | 🔄 Device now boots (instance+adapter+queue+nvrhi); passes pending shader compilation |
| Runtime launched the Lua REPL instead of the engine | ✅ Fixed - lua.c removed, cross-platform entry point |
