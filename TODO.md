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

### Tests (101 test cases, 1001 assertions - all passing)
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
- [ ] PBR shader (mesh rendering with PBR materials)
- [ ] SSAO pass (screen-space ambient occlusion)
- [ ] HDR pass (HDR accumulation and tonemapping)
- [ ] Final pass (composite with SSAO overlay)
- [ ] Shadow map pass
- [ ] Assimp glTF model loader (code complete, needs assets)
- [ ] VertexArray/VertexBuffer/IndexBuffer (null stubs in tests)
- [ ] Texture loading (stb_image)
- [ ] Shader library management

### Editor (AI-controllable)
- [x] Headless EditorContext (scene, selection, play/pause/step, gizmo, save/load)
- [x] Gizmo manipulation (math layer)
- [x] AssetRegistry / asset browser backend
- [ ] ImGui asset browser panel (backend done)
- [ ] ImGui gizmo interaction (math layer done)
- [x] Properties panel for global settings
- [ ] Actual viewport rendering (needs real Vulkan device + swapchain)

### Assets & Resources
- [ ] AssetRegistry / asset browser backend
- [ ] IBL (Image-Based Lighting) with HDRIs
- [ ] Soft shadow maps
- [ ] Default cube primitive
- [ ] Resources directory populated with meshes, HDRIs, textures

### Engine Polish
- [x] Physics-driven transform updates (Scene sync)
- [x] DuplicateEntity copies all components
- [x] SerializeRuntime/DeserializeRuntime (binary format)
- [ ] Mesh loading from file (needs Assimp + vulkan)
- [ ] Camera viewport resize handling

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
| Full headless frame (device + swapchain + passes) | ⚠️ Blocked on renderer implementation, not verifiable headless |
