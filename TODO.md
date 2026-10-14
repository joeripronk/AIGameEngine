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
- [x] Viewport panel
- [x] Toolbar (file operations)
- [x] AI-agent controllable API

### Runtime
- [x] Standalone runtime application
- [x] Simulation step system

### Build System
- [x] CMake with ForgeCore + vulkan separation
- [x] Lua compiled as C (PUBLIC dependency)
- [x] FG_NO_VULKAN_RUNTIME for testability
- [x] Catch2 test targets

### Tests (52 test cases, 116 assertions - all passing)
- [x] CoreTests (logging, assertions)
- [x] SceneTests (entities, components, serialization)
- [x] RendererTests (null stubs)
- [x] PhysicsTests (14 tests - physics engine, collision, integration)
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

### Physics
- [x] Custom 3D physics engine (AABB collision, rigid body dynamics)
- [x] Box2D integration (stub - custom physics used instead)
- [x] PhysX 3D integration (vendor directory empty - custom physics used)
- [x] Collision detection (AABB, sphere)
- [x] Collision response (impulse-based with friction)
- [ ] Physics-driven transform updates (needs Scene integration)

### Editor
- [ ] Actual viewport rendering (currently ImGui image call has no swapchain)
- [ ] Gizmo manipulation
- [ ] Asset browser
- [ ] Properties panel for global settings

---

## ❌ Blocked / Pending

### Assets & Resources
- [ ] Resources directory populated with meshes, HDRIs, textures
- [ ] IBL (Image-Based Lighting) with HDRIs
- [ ] Soft shadow maps
- [ ] Default cube primitive

### Engine Polish
- [ ] Physics world cleanup (m_PhysicsWorld managed externally)
- [ ] DuplicateEntity copies all components (currently copies only name)
- [ ] SerializeRuntime/DeserializeRuntime (runtime-only serialization)
- [ ] Mesh loading from file (needs Assimp + vulkan)
- [ ] Camera viewport resize handling

### Platform & Deployment
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
| Editor viewport has no swapchain | ⚠️ Requires Vulkan implementation |
| Resources directory empty | ⚠️ No assets for testing |
