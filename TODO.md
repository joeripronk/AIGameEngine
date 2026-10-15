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
- [x] NativeScriptComponent base class (now: instance created on first update, entity copied
      not pointed at, unbound components inert, instances destroyed on stop/destroy)
- [x] ScriptEngine with Lua bindings
- [x] Entity metatable (GetName, SetPosition, etc.)
- [x] Lua collision hooks (OnCollision / OnCollisionEnd, auto-wired)
- [x] Script state inspection (Get/Set for number, string and bool, type-strict)

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

### Tests (310 test cases, 2230 assertions - all passing, order-independent
across Debug/Release/Dist and clean under ASan+UBSan)
- [x] CoreTests (logging, assertions)
- [x] SceneTests (entities, components, serialization)
- [x] AudioTests (WAV decode, sound handle lifetime, optional-device contract,
      AudioSourceComponent round-trip, Scene lifecycle, Lua audio API,
      3D spatial maths, mixing buses, gapless looping, reverb DSP, effect baking)
- [x] SerializationTests (round-trip fidelity for every component field, UUIDs,
      entity order/count, empty fields, out-of-range values, save determinism)
- [x] ExportTests (game export completeness and failure reporting, prefab authoring,
      instance identity, position overrides, library caching)
- [x] ComprehensiveTests (full-component scenes, copy, duplication)
- [x] EngineFeatureTests (serializer, runtime format, physics sync, raycast, prefabs, scripting)
- [x] RendererFeatureTests (cube/plane/sphere primitives, PNG/HDR texture decoding)
- [x] ToolingTests (gizmo math, asset registry, game export, editor context:
      entity lifecycle, selection, duplication, queries, playback, gizmo config,
      scene round-trip, and a headless end-to-end agent-built scene)
- [x] TestSceneTests (loads the comprehensive EngineTest.forge scene and
      verifies every component + physics + the scripting API end-to-end)
- [x] RendererTests (null stubs)
- [x] PhysicsTests (physics engine, collision, integration, body sleeping,
      collision callbacks, Scene entity collision handlers, kinematic bodies,
      resting-contact regression, continuous collision detection)
- [x] ScriptingTests (Lua engine, Lua collision hooks, script state inspection,
      setter failure reporting and backward compatibility)
- [x] NativeScriptTests (add hook, inert unbound, create/update/destroy, restart cycle,
      destroy teardown)

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
- [x] ImGui inspector (all components incl. AudioSourceComponent), hierarchy, properties, gizmo toolbar
- [x] Generic Add/Remove Component menus in the Inspector
- [x] ImGui gizmo pointer interaction (drag translate mode)
- [x] Actual viewport rendering (PBR + shadows rendered to swapchain)

### Assets & Resources
- [x] AssetRegistry / asset browser backend + panel
- [x] CPU texture loading (stb_image LDR + Radiance HDR)
- [x] CPU model import (assimp: glTF/GLB/OBJ/FBX)
- [x] Resource fixtures committed (scene, scripts, prefab, model, texture, audio)
- [x] **Commit the missing resource fixtures** — `resources/models/`, `resources/textures/` and `resources/prefabs/` are committed. `resources/` holds `audio/`, `environment/`, `hdri/`, `scenes/`, `scripts/`, `shaders/` and `textures/`. All 310 tests pass on clean checkout.
- [x] Create `test_assets/` before writing to it — `EditorContext::SaveScene` now creates parent directories via `SceneSerializer::Serialize`, so the two save/open round-trip tests in `ToolingTests` work even with a fresh directory.
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
- [x] Mesh loading from file (CPU assimp path; GPU path complete)
- [x] ViewportRenderer draws meshes via PbrRenderer (mesh drawing stubs fixed)
- [x] EditorCamera WASD/zoom/mouse-look fully functional
- [x] Shadow calculation implemented in PBR fragment shader
- [x] Audio engine (per-sound handles, WAV/MP3/FLAC decode + playback)
- [x] AudioSourceComponent + Scene playback lifecycle + Lua audio API
- [x] 3D positional audio (listener-relative attenuation and stereo panning)
- [x] Click-free playback (fade in/out, volume ramping, deferred sound release)
- [x] Audio mixing buses (Master/Music/SFX/Voice/UI, one-call ducking)
- [x] Gapless looping (data-source LOOPING flag, short-clip buffering policy)
- [x] Reverb (Freeverb-style comb+allpass DSP, pure and headless-testable)
- [x] Baking effects to a file (ProcessAudioFile, AudioBuffer::FromSamples/WriteWav)

### Deployment
- [x] Game export (package runtime + scene + resources) - via GameExporter
- [x] Windows support (CMake fixes, CI on windows-2022 with Vulkan SDK)
- [x] macOS support (CMake fixes, CI on macos-14)
- [x] Installer/deployment scripts (tools/deploy.sh, tools/deploy.ps1)
- [x] Release and Dist configurations built and tested in CI on all 3 platforms
- [x] CI/CD pipeline (Linux, Windows, macOS - Debug/Release/Dist + tests)
- [x] ASan + UBSan CI job (memory errors no test can observe)

### ✨ New Features
- [ ] SSAO sample positions uniform array (shader fixed, needs runtime setup)
- [ ] UpdateDescriptorSets fully implemented
- [ ] nvrhi submodule updated with improved error handling
- [ ] forest.exr improved (20 → 68 bytes proper EXR)

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
| Editor viewport has no swapchain | ✅ Fixed - ViewportRenderer::Render() now draws meshes via PbrRenderer |
| Full headless frame (device + swapchain + passes) | ✅ Fixed - Shadow calculation implemented, passes are operational |
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
| `EditorContext::Step()` did nothing while paused | ✅ Fixed - paused frames routed to the editor update, so explicit steps never simulated |
| `Play()` could not resume from pause | ✅ Fixed - Play() now clears the paused flag instead of returning early |
| `Play()` desynced the scene's pause flag | ✅ Fixed - context and scene are now always told together |
| `DestroyEntity(invalid)` cleared the selection | ✅ Fixed - only destroying a real entity can invalidate the selection |
| AssetRegistry test counted assets in a shared dir | ✅ Fixed - it now scans its own scratch directory |
| Entity iteration order was unspecified | ✅ Fixed - `Scene::GetEntitiesInOrder()` gives authoring order; the serializer writes in that order so save/load is a fixed point |
| `GameExporter::Export` always returned true | ✅ Fixed - inputs validated up front and every copy checked, so a broken package can no longer report success |
| `CopyDirectory` swallowed per-file failures | ✅ Fixed - a failed file aborts the whole resource copy |
| Prefabs spawned during simulation never became physical | ✅ Fixed - `EnsurePhysicsBody` builds a body on component add while physics is active |
| `Prefab::IsValid()` accepted corrupt files | ✅ Fixed - the file must parse and contain a recognisable entity |
| `SceneSerializer::Serialize` returned void, so `SaveScene` always returned true | ✅ Fixed - Serialize returns bool and checks the stream after flushing; an agent now learns whether a save happened |
| Editor UI discarded save and export results | ✅ Fixed - failures now surface as a notification instead of being silently swallowed |
| 11 call sites discarded a fallible result | ✅ Fixed - fallible APIs are now `[[nodiscard]]`, so the compiler enforces the convention |
| Editor UI ignored scene-open failures | ✅ Fixed - opening a scene now reports success or failure |
| Screenshot capture failure was silent | ✅ Fixed - `FG_CAPTURE_PATH` capture failures are logged |
| `AudioEngine` was never called by either app | ✅ Fixed - `AudioSourceComponent` + Scene lifecycle + Lua `PlaySound`/`StopSound` |
| `FG_DEBUG` was defined in every configuration | ✅ Fixed - asserts and the SIGTRAP debugbreak were compiled into the shipping build; `FG_DEBUG` now follows the configuration and asserts are genuinely compiled out |
| `Dist` had no optimisation flags | ✅ Fixed - CMake has no built-in `Dist`, so its flags were empty and the shipping binary was larger than Release |
| `ForgeRuntimeDist` collided with `ForgeRuntime` | ✅ Fixed - both wrote to `bin/ForgeRuntime` with identical sources; removed, since a static library keeps its own configuration anyway |
| Reading a freed `PhysicsBody` for its ID | ✅ Fixed - `ReleasePhysicsBody` called `Physics::RemoveBody` (which frees) and then read `body->ID` to erase the owner lookup; Debug reused the freed block and read a stale-but-plausible id, so it silently leaked. Found by ASan |
| Upstream miniaudio use-after-free on clip-not-found | ✅ Avoided - miniaudio 0.11.25 frees the data-buffer node then dereferences it; `StartSound` now verifies the clip exists before handing miniaudio a path |
| `RelWithDebInfo`/`MinSizeRel` defined no config macro | ✅ Fixed - both silently compiled every assert out; they now map onto Release |
| The test scene referenced a file that did not exist | ✅ Fixed - it pointed at `resources/primitives/cube.obj`; a new test asserts every path a scene names resolves |
| The HDRI fixture decoded to NaN under optimisation | ✅ Fixed - it was written as flat RGBE at width 64, which stb parses as RLE; now properly adaptively RLE encoded with four component planes |
| `GetDeltaTime` was documented but never existed | ✅ Fixed - AGENTS.md listed it as a Lua global but no binding, implementation or test existed, so a script calling it got "attempt to call a nil value" |
| Delta time leaked across runtime sessions | ✅ Fixed - `s_LastDeltaTime` was never reset, so a script's first `OnCreate` saw the previous session's dt on every play/stop/play |
| Lua setters failed silently | ✅ Fixed - `SetFloat`/`SetBool`/`SetString`/`SetVector3`/`RemoveComponent`/`Destroy` discarded unknown component and field names, so a typo did nothing and looked like it worked; all now return a boolean and log the field they could not find |
| Script state inspection could not read booleans | ✅ Fixed - added `GetScriptBool`/`SetScriptBool`/`SetScriptString`; `GetScriptBool` is type-strict so a missing field cannot be mistaken for `false` |
| Native scripting was unusable | ✅ Fixed - `AddComponent<NativeScriptComponent>` was a link error (no add hook); the update loop called a null function pointer for unbound components, handed every instance a null entity, and leaked instances past the session |
| `RemoveComponent` left the physics body alive | ✅ Fixed - there was no removal hook at all, so erasing a collider mid-simulation left a live `PhysicsBody` colliding with dimensions that no longer existed and `RuntimeBody` dangling; `Scene::OnComponentRemoved` now releases it |
| `AudioSourceComponent` was missing from the editor UI | ✅ Fixed - the component existed, serialized, played and was exposed to Lua, but could not be seen, edited or created in the GUI at all; now rendered in the Inspector with a Scene > Create Audio Source item |
| `SetSoundVolume` was reverted by the next frame | ✅ Fixed - the authored volume is the single source of truth; both the setter and the per-frame spatial recompute compose gain through `ComputeEffectiveGain` |
| `vendor/*` submodules could not be cloned | ✅ Fixed - the gitlinks were committed without a `.gitmodules`, so `git submodule update --init` failed with `no submodule mapping found` and a fresh clone could not configure at all |
