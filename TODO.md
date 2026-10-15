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
- [x] Lua collision hooks (OnCollision / OnCollisionEnd, auto-wired)
- [x] Script state inspection (GetScriptNumber/String/SetScriptNumber)

### Editor (ImGui)
- [x] Scene hierarchy panel
- [x] Inspector panel
- [x] Viewport panel with render target
- [x] Toolbar (file operations)
- [x] AI-agent controllable API
- [x] Gizmo manipulation (math layer + pointer interaction)
- [x] Asset browser
- [x] Properties panel for global settings
- [x] Gizmo drag interaction (translate mode with axis picking)

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

### Tests (146 test cases, 1130 assertions - all passing, order-independent)
- [x] CoreTests (logging, assertions)
- [x] SceneTests (entities, components, serialization)
- [x] ComprehensiveTests (full-component scenes, copy, duplication)
- [x] EngineFeatureTests (serializer, runtime format, physics sync, raycast, prefabs, scripting)
- [x] RendererFeatureTests (cube/plane/sphere primitives, PNG/HDR texture decoding)
- [x] ToolingTests (gizmo math, asset registry, game export, editor context)
- [x] TestSceneTests (loads the comprehensive EngineTest.forge scene and
      verifies every component + physics + the scripting API end-to-end)
- [x] RendererTests (null stubs)
- [x] PhysicsTests (physics engine, collision, integration, body sleeping,
      collision callbacks, Scene entity collision handlers, kinematic bodies,
      resting-contact regression, continuous collision detection)
- [x] ScriptingTests (Lua engine, Lua collision hooks, script state inspection)

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
- [x] SSAO pass (depth reconstruction - shader requires G-Buffer inputs)
- [x] HDR tonemapping pass (reads HDR buffer, applies ACES tone mapping)
- [x] Final composite pass (reads tonemapped + SSAO, outputs to swapchain)
- [x] IBL prefiltering / irradiance maps (irradiance cubemap + prefilter mipmapped cubemap)
- [x] ImGui Vulkan backend init + viewport present (ImGui draw data rendered to swapchain)

### Editor (AI-controllable)
- [x] Headless EditorContext (scene, selection, play/pause/step, gizmo, save/load)
- [x] Gizmo manipulation (math layer)
- [x] AssetRegistry / asset browser backend
- [x] ImGui asset browser panel
- [x] ImGui inspector (all components), hierarchy, properties, gizmo toolbar
- [x] ImGui gizmo pointer interaction (drag translate mode)
- [x] Actual viewport rendering (PBR + shadows rendered to swapchain)

### Assets & Resources
- [x] AssetRegistry / asset browser backend + panel
- [x] CPU texture loading (stb_image LDR + Radiance HDR)
- [x] CPU model import (assimp: glTF/GLB/OBJ/FBX)
- [x] Resource fixtures committed (scene, scripts, prefab, model, texture, audio)
- [x] IBL (Image-Based Lighting) with HDRIs (irradiance + prefilter cubemaps)
- [x] Soft shadow maps (implemented)

### Engine Polish
- [x] Physics-driven transform updates (Scene sync, both directions)
- [x] Continuous collision detection (adaptive substepping, opt-in)
- [x] Physics body sleeping (configurable velocity/time thresholds, wake on contact)
- [x] Physics collision events (per-body callbacks + Scene entity handlers)
- [x] DuplicateEntity copies all components
- [x] SerializeRuntime/DeserializeRuntime (binary format)
- [x] Camera viewport resize handling (CameraComponent::UpdateProjection)
- [x] Mesh loading from file (CPU assimp path; GPU path pending)
- [x] Audio engine (miniaudio ma_engine mixer, WAV/MP3/FLAC decode + playback)

### Deployment
- [x] Game export (package runtime + scene + resources) - via GameExporter
- [x] Windows support (CMake fixes, CI on windows-2022 with Vulkan SDK)
- [x] macOS support (CMake fixes, CI on macos-14)
- [ ] Installer/deployment scripts
- [x] CI/CD pipeline (Linux, Windows, macOS - Debug builds + tests)

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
| Physics collision normal was inverted | ✅ Fixed - overlapping bodies were pushed together, so boxes sank through the ground |
| Impulse solver skipped approaching pairs | ✅ Fixed - resting bodies accumulated velocity without bound and never came to rest |
| Positional correction split 50/50 | ✅ Fixed - now weighted by inverse mass so an immovable partner absorbs none |
| Friction impulse was unclamped | ✅ Fixed - clamped by the normal impulse so it cannot reverse tangential motion |
| `BodyType::Kinematic` was unimplemented | ✅ Fixed - treated as static; now drives by velocity with infinite mass |
| `PhysicsBody::SleepTime` uninitialised in component ctors | ✅ Fixed - bodies began with garbage sleep time |
| Test suite was order-dependent | ✅ Fixed - tests leaking the global script/prefab directories now restore them |
| Discrete detection let fast bodies tunnel | ✅ Fixed - optional adaptive CCD substepping replaces the velocity clamp |
| Contacts were lost when CCD resolved them mid-frame | ✅ Fixed - contacts accumulate across all substeps of a frame |
| `Physics::Init()` leaked solver configuration | ✅ Fixed - CCD, max substeps and sleeping thresholds now reset per session |
