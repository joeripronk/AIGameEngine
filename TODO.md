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
- [x] Script hot reload (Hazel workflow): HotReloadScript/HotReloadAll rebuild
      live instances from disk with OnCreate, a failed reload keeps the
      previous script running, the editor watches .lua files and reports
      reload success/failure as a notification

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

### Tests (414 test cases, 11705 assertions - all passing, order-independent
across Debug/Release/Dist and clean under ASan+UBSan)
- [x] CoreTests (logging, assertions, event contracts: window-drop path/type/
      category, dispatcher routing to its handler and silence for unrelated
      events)
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
- [x] ImGuiLayerTests (overlay layer contract: inert before attach, window-less
      attach rolls back and is retryable, style keeps the scene visible,
      clean detach and reuse, context configuration contract: keyboard
      navigation + docking flags applied additively)
- [x] AssetWatcherTests (file signatures: existence/size/timestamp, rewrite
      with restored timestamp means no reload; added/modified/removed/returned
      event edges, multi-file polls, unwatched silence; albedo hot-reload:
      failed load retried on file creation, rewrite re-decodes, unchanged file
      returns the identical texture, failed reload keeps the working texture,
      recovery on re-creation)
- [x] PhysicsTests (physics engine, collision, integration, body sleeping,
      collision callbacks, Scene entity collision handlers, kinematic bodies,
      resting-contact regression, continuous collision detection)
- [x] ScriptingTests (Lua engine, Lua collision hooks, script state inspection,
      setter failure reporting and backward compatibility)
- [x] Hot-reload tests (fresh instance rebuild with OnCreate, source re-read
      from disk, failed compile/removed file keeps the previous script,
      recovery after fixing the file, failure outside a runtime session,
      HotReloadAll counting clean reloads)
- [x] NativeScriptTests (add hook, inert unbound, create/update/destroy, restart cycle,
      destroy teardown)
- [x] ProfilerTests (configuration gating, inert session API in the compiled-out
      branch, one Chrome-trace document per session with exactly one closing
      bracket, nested scopes, pre-session scopes write nothing, a second
      session truncates the first, concurrent threads serialise whole events,
      unopenable path reports failure; overlay-panel stats: per-scope sample
      counts and min/mean/max, total-first sorting, the session gate on
      aggregation, reset and a new session starting clean, and the Dist
      branch answering "no samples" with the same API)
- [x] FrustumTests (frustum corners from the unprojected NDC cube, point tests
      per plane, AABB tests that keep straddling boxes and cull only provable
      misses, camera position/rotation following, renderer statistics
      accumulation and reset, DecomposeTransform round-trip and rejection of
      a degenerate matrix)
- [x] AssetDatabaseTests (handle assignment and lookup agreement, idempotent
      Add and rejection of non-assets, database round-trip preserving handles,
      handle reuse rules, corrupt/truncated database reporting false with the
      registry unchanged, AssetImporter proving decode for texture/model/audio/
      scene and refusing a fake png, a missing file and shader/script paths)
- [x] RTTITests (registry lookup by name/UUID/C++ type agreeing by
      construction, the full component set registered with correct sizes,
      re-registration by name and by type reporting false with the original
      metadata kept, name-derived stable UUIDs, flags and declared base types
      round-tripping, event-bus delivery with unregister, registration-order
      enumeration, EditorContext add/remove-component-by-name incl. the
      structural-component rule and a mid-sim collider added by name becoming
      physical)
- [x] BloomTests (luminance filter: silence below threshold, positive and
      bounded above, saturation past the knee - the anti-crawl normalization
      - zero-knee degeneracy staying finite, scale linearity; 13-tap
      downsample box: bounds, centre tap, distinct taps; 16-tap golden-spiral
      blur: unit-disc bound, distinct taps, radii filling outward, no
      directional bias)

### Hazel parity (upstream `TheCherno/Hazel`)
- [x] `Core/Timer.h` + an engine clock (`Time::GetTime`) that tests can replace
- [x] `Core/Input.h` + `KeyCodes.h` / `MouseCodes.h` (values verified against GLFW)
- [x] `Core/FileSystem` whole-file read/write, adopted by every loader that had
      its own copy
- [x] `LayerStack` detaches layers it pops or destroys; `PopLayer` / `PopOverlay`
      search only their own region; `Layer::OnUpdate` takes a `Timestep`
- [x] `UI::ScopedStyleColor` (RAII ImGui style push/pop)
- [x] `Project` / `ProjectSerializer` — a `.forgeproject` file, `--project` in
      both applications, and the asset root the exporter and asset registry take
      their defaults from
- [x] Orthographic projection (`CameraComponent::ProjectionType`,
      `OrthographicSize`) with serialization and an inspector widget
- [x] `EditorCamera::SetViewportSize` and a zoom level the projection uses
- [x] `Math::DecomposeTransform` (`Forge/Math`), the glm-era decomposition
      Hazel ships, with a round-trip test through the engine's own composition
- [x] Camera frustum corners (unproject the NDC cube, Hazel's method) with
      point and AABB tests - and the culling that uses it:
      `Renderer::RenderScene` skips entities whose transformed bounds lie
      entirely outside the primary camera's frustum, and counts them
- [x] Renderer draw statistics (Hazel's `Renderer2D::Statistics` pattern as the
      TODO's named "useful slice"): `RendererStatistics` in ForgeCore, filled
      by the real draw loop (draw calls, triangles, textures bound,
      frustum-culled) and displayed in the editor Properties panel
- [x] SceneTree click-to-select: `Scene::FindEntityOnRay` (ray-AABB slab +
      ray-sphere against authored colliders, and the mesh's own bounds for
      the objects that have no collider, nearest hit wins) driving
      `EditorContext::SelectEntityAtRay`, wired to a viewport click release;
      a miss leaves the selection alone
- [x] Editor keyboard shortcuts as a layer (the Hazel `OnKeyPressed` set):
      Ctrl+N new scene, Ctrl+S save (to the scene the work came from),
      Ctrl+D duplicate, Delete destroy, 1/2/3 gizmo mode, F5 play toggle;
      held-key autorepeat and typing in a field never fire them
- [x] Lua input bindings (Hazel ScriptGlue's `Input_IsKeyDown` pattern):
      `GetKeyDown(key)`, `GetKeyReleased(key)`, `GetMousePosition()`,
      answering false/zero with no window like the rest of `Input`
- [x] Asset manager handles and importer (Hazel `EditorAssetManager` /
      `AssetImporter` / `TextureImporter`): `AssetHandle` in the registry,
      `Add`, `Serialize`/`Deserialize` of the asset database, and
      `AssetImporter::Import` proving a file decodes with the engine's loaders
- [x] Editor status bar (the Hazel bottom strip): scene path, play state,
      selection, last-frame draw calls
- [x] DockSpace layout (from the public editor layer's ImGui docking
      pattern; the docking feature itself required the ImGui vendor swap
      noted under the editor section)
- [x] Instrumentor overlay panel (the live profiling view): per-scope
       aggregation in the Instrumentor (`GetProfileSnapshot`: samples, total,
       min, mean, max per scope, heaviest first; `ResetProfileStats`), the
       same API in the Dist branch answering "no samples", and an editor
       "Profiler" panel with a Reset control, reading the same measurements
       the trace file records
- [x] RTTI / type registry (`Forge/Core/RTTI`: RuntimeTypeInfo, TypeRegistry,
       TypeEventBus) - the design from Cherno's public Toy Engine / Workshop
       material, not the Hazel repo (see note below; the original claim that
       Hazel ships it was not verifiable in any public branch). All 14 engine
       components registered; lookups by name, UUID and C++ type agree by
       construction; one C++ type can only be registered under one name;
       registrations publish to the event bus; the consumer is
       `EditorContext::GetRegisteredComponentTypes` +
       `AddComponentByName`/`RemoveComponentByName` - the hand-list the
       registry replaces is exactly the drift this repo has repeatedly fixed
- [ ] Sprite / 2D renderer (`Renderer2D`, `SpriteRendererComponent`) and text.
      Not ported as-is: a texture-array binding set and an MSDF atlas pipeline
      nothing in the engine, the editor or Tetris currently renders. The useful
      slice (`Texture::Bind(slot)` + draw-call statistics) is now done above.
- [x] Bloom post-processing pass (the luminance → 13-tap downsample → 3x
      golden-spiral blur → composite chain Cherno presents publicly): half-res
      `bloom.frag` (soft-threshold luminance + 13-tap box) and `blur.frag`
      (16-tap golden spiral, widening radius per iteration), ping-ponging
      between two half-res RGBA16_FLOAT targets, composed **before**
      tonemapping so glow rolls off with the frame through the same ACES
      curve. The maths lives in `Forge/Renderer/Bloom.h` where unit tests
      hold it; the GLSL evaluates the same formulas. The public Hazel
      repository contains no bloom code (checked: every public branch), so
      this is the documented chain, built and compiled - the GPU path is
      verified by the Vulkan target building and the passes recording, not
      by a headless assertion.

> Checked against every public Hazel branch (master, projects, scripting,
> asset-manager): there is no ProfilerOverlayPanel, no FileSystem
> directory/path utilities, no Lua ScriptGlue, no RTTI and no bloom code in
> the public source - every published branch is Mono-based. The Lua glue,
> the overlay panel, the type registry and the bloom chain here follow
> Cherno's documented patterns (the Workshop / Toy Engine RTTI and the
> public bloom videos are public material; the Hazel code itself is not),
> not verbatim ports, and the parity list stays honest about that: items
> are claimed when the behaviour exists and is tested, not when a file name
> matches.

### Fixed while comparing against Hazel
- [x] A saved scene never loaded `MeshComponent::ModelPath`, so every modelled
      entity rendered as a unit cube
- [x] `Mesh::Create` omitted the base vertex every mesh's indices need, and
      ignored node transforms: multi-mesh models indexed past the vertex buffer
      and collapsed into the origin. One importer now serves both paths, and
      bounds-checks before reporting success
- [x] `Scene::Copy` / `DuplicateEntity` silently dropped
      `EnvironmentMapComponent`, `AudioSourceComponent` and `NativeScriptComponent`
- [x] The runtime `.rt` format dropped camera parameters, a material's albedo
      texture and the entire audio component
- [x] The simulation rendered: `Scene::RenderScene` opened and closed the
      command list the application had opened
- [x] Primary-camera resolution was duplicated, and two cameras could both be
      primary
- [x] `FG_ERROR` and friends dereferenced a null client logger before
      `Log::Init`, killing the caller of a failure path
- [x] `ReverbTests` failed to compile at `-O2` (GCC's `stringop-overflow`
      false positive on a loop-invariant vector copy), so Release was not green

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
- [x] Bloom pass (soft-threshold luminance + 13-tap downsample to half res,
      3x 16-tap golden-spiral blur ping-pong, composite into the tonemap
      before ACES; `Renderer::BeginBloomPass` between the SSAO and HDR
      passes in all three apps, `Renderer::SetBloomIntensity` with an editor
      control, half-res targets rebuilt on resize)
- [x] Final composite pass (reads tonemapped + SSAO, outputs to swapchain)
- [x] IBL prefiltering / irradiance maps (irradiance cubemap + prefilter mipmapped cubemap)
- [x] ImGui Vulkan backend init + viewport present (ImGui draw data rendered to swapchain)
- [x] PBR albedo textures (PBRMaterialComponent::AlbedoTexturePath, serialized,
      loaded once through Renderer::GetAlbedoTexture and shared by every pass)
- [x] Render scale / supersampling (offscreen targets above the swapchain extent,
      edge-aware downsample in the Final pass)
- [x] Real-time reflection probe (six-face cubemap scene draw; polished metal
      mirrors the live scene, probe draws bind the fallback env to avoid feedback)
- [x] Depth-sorted PBR draws (far-to-near, stable overdraw)
- [x] Frustum culling of PBR draws (transformed-bounds AABB test against the
      primary camera's frustum corners; culled entities counted, not drawn)
- [x] Renderer draw statistics (`RendererStatistics`: draw calls, triangles
      submitted, textures bound, frustum-culled) read from the real draw loop

### Editor (AI-controllable)
- [x] Headless EditorContext (scene, selection, play/pause/step, gizmo, save/load)
- [x] Gizmo manipulation (math layer)
- [x] AssetRegistry / asset browser backend
- [x] ImGui asset browser panel
- [x] ImGui inspector (all components incl. AudioSourceComponent), hierarchy, properties, gizmo toolbar
- [x] Generic Add/Remove Component menus in the Inspector
- [x] ImGui gizmo pointer interaction (drag translate mode)
- [x] Actual viewport rendering (PBR + shadows rendered to swapchain)
- [x] Forge::ImGuiLayer: engine-owned ImGui context + GLFW/Vulkan backends
      (Hazel pattern - apps push a layer instead of hand-rolling the lifecycle);
      Tetris drives its HUD through it, and a missing backend leaves the game
      running without the overlay
- [x] Viewport click-to-select (ray from the editor camera through the click
      through `EditorContext::SelectEntityAtRay`), keyboard shortcuts as an
      input layer (Ctrl+N/S/D, Delete, W/E/R gizmo, F5 play), a per-frame
      draw-statistics display, and saving to the scene file the work came from
- [x] Status bar (the Hazel bottom strip): current scene path, play state,
      selection and last-frame draw calls, always visible
- [x] Live Profiler panel: per-scope samples, total/mean/max milliseconds,
      heaviest first, reset control - the same measurements as the trace file
- [x] Dockable editor layout (the DockSpace pattern from Hazel's public
      editor layer): fullscreen docking host, every panel dockable by
      dragging, layout persisted in imgui.ini. Needed the vendored ImGui
      upgraded from vanilla 1.91.9 to **v1.91.9-docking** (the docking
      feature is branch-only in this version; the swap is file-for-file,
      no engine patches, full suite green on it). The initial arrangement
      (hierarchy left, inspector/properties right, viewport centre, asset
      shelf bottom) is built by DockBuilder exactly once and only when the
      ini holds no saved layout - rebuilding it every launch would discard
      what the author arranged
- [x] Chrome stays chrome: the gizmo toolbar, status bar and notifications
      are NoDocking windows - they float above the layout and cannot be
      dragged into it
- [x] Startup integration checks: `tests/integration/startup_check.sh`
      (a `StartupCheck` ctest) launches the real editor and classifies
      startup failures with troubleshooting hints - display, Vulkan
      loader/ICD, stale `.spv` - skipping gracefully where no window is
      possible (CI-safe; forces lavapipe under xvfb so vendor drivers in a
      synthetic session cannot flake it). `StartupIntegrityTests` checks
      headless what `Renderer::Init` loads: every shader in the loader's
      list is a valid SPIR-V module, every `.vert`/`.frag` has its `.spv`
      beside it. `run-editor-debug.sh` gained preflight checks and exit
      classification with hints
- [x] Startup failure is a reported exit, not a crash: `glfwInit` was
      asserted as `1` (literally always true) and its result discarded;
      window creation asserted, then the app ran on a null handle.
      `Window::IsValid()`, `Application::StartupSucceeded()`, apps skip
      renderer init and teardown when startup failed, and the entry point
      exits **1** with the reason in the log - verified headless on editor,
      runtime and tetris
- [x] Real crash found by the startup check: hand-built
      `nvrhi::BindingSetItem`s in the SSAO/HDR/Final/bloom passes left
      `arrayElement` as stack garbage (a leftover 1.0f became
      `dstArrayElement = 0x3F800000` in the descriptor write) - accepted
      by the desktop driver, SIGSEGV in lavapipe and nvidia under a
      synthetic session. All six sites now go through nvrhi's factories,
      which set every field - the rule the MakeSamplerItem comment already
      stated, applied where it had been broken
- [x] Play / Simulate / Stop state machine (the public editor layer's
      `SceneState::Play`/`Simulate`): **Play** runs the scripts under the
      author-steered editor camera; **Simulate** runs them on the scene's
      own primary camera, which the viewport then renders through
      (`EditorContext::GetViewProjection` switches, `SyncEditorCamera`
      stands down, F9 toggles, buttons flip to Stop while their mode is
      live). Simulate from Play stops the session first, Play from
      Simulate hands the camera back, Stop clears both. The scene
      cameras' projections now follow the viewport panel's real aspect
      (`OnViewportResize` was never wired to the editor - latent since
      the 16:9 projection was drawn into arbitrary panel shapes)
- [x] Window drag-and-drop opens files (the public editor layer's
      `OnWindowDrop` - its event class is not in the public core, so
      `WindowDropEvent` was built to the same shape as the events here):
      GLFW drop callback, one event per dropped file, a `.forge` opens as
      the scene, a `.forgeproject` as the project, anything else is
      reported as a notification rather than ignored
- [x] Native file dialogs for the editor menu: **Open Scene...**, **Save
      Scene As...**, **Open Project...**, **Save Project As...** pick real
      paths (cancel is not an error; failures surface as notifications).
      Vendored nativefiledialog-extended, portal backend on Linux - D-Bus
      to the desktop's own dialog service, Win32 on Windows, Cocoa on
      macOS - chosen because the engine must not drag a GUI toolkit in
      beside Vulkan/GLFW. Ctrl+S keeps saving silently to the current path
- [x] Component types enumerable and name-addressable for agents:
      `GetRegisteredComponentTypes`, `AddComponentByName`,
      `RemoveComponentByName` (structural components refused)

### Assets & Resources
- [x] AssetRegistry / asset browser backend + panel
- [x] CPU texture loading (stb_image LDR + Radiance HDR)
- [x] CPU model import (assimp: glTF/GLB/OBJ/FBX)
- [x] Resource fixtures committed (scene, scripts, prefab, model, texture, audio)
- [x] **Commit the missing resource fixtures** — `resources/models/`, `resources/textures/` and `resources/prefabs/` are committed. `resources/` holds `audio/`, `environment/`, `hdri/`, `scenes/`, `scripts/`, `shaders/` and `textures/`. All 310 tests pass on clean checkout.
- [x] Create `test_assets/` before writing to it — `EditorContext::SaveScene` now creates parent directories via `SceneSerializer::Serialize`, so the two save/open round-trip tests in `ToolingTests` work even with a fresh directory.
- [x] IBL (Image-Based Lighting) with HDRIs (irradiance + prefilter cubemaps)
- [x] Soft shadow maps (implemented)
- [x] Asset hot-reload (Hazel AssetManager pattern): FileSignature + AssetWatcher
      in ForgeCore; the albedo cache re-stat-s each path per frame and
      re-decodes/re-uploads on a size/timestamp change, retries a failed load
      as soon as the file appears, and a failed reload keeps the texture that
      worked; the editor polls the watcher on a 0.5s cadence and surfaces
      added/modified/removed as notifications
- [x] Asset handles + serialized asset database (Hazel's EditorAssetManager:
      handle→metadata registry, ImportAsset, Serialize/DeserializeAssetRegistry):
      `AssetHandle` per asset, idempotent `Add` for mid-session files,
      `Serialize`/`Deserialize` with magic+version checks where a corrupt file
      reports false and changes nothing, handles stable across save/load and
      never reused by `Add`
- [x] AssetImporter dispatch (Hazel's AssetImporter → TextureImporter pattern):
      `AssetImporter::Import(path)` proves the file decodes with the engine's
      own loaders (texture, model, audio, scene) and reports false for a
      missing file, a fake png and types with no loader

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
- [x] Profiler (Hazel Instrumentor): FG_PROFILE_SCOPE/FG_PROFILE_FUNCTION stream
      Chrome-trace JSON (chrome://tracing, Perfetto); compiled in for
      Debug/Release/RelWithDebInfo/MinSizeRel, compiled OUT of Dist;
      --profile[=path] starts a session in every app, the editor has
      Start/Stop Profiling controls with failure notifications; frame, scene
      update, physics, audio update and each render pass are instrumented
- [x] Lua input bindings: GetKeyDown/GetKeyReleased/GetMousePosition (Hazel's
      ScriptGlue Input pattern), answering the headless no-window contract
- [x] Lua mouse buttons: GetMouseButtonDown/GetMouseButtonUp
- [x] Type registry (RTTI): RuntimeTypeInfo / TypeRegistry / TypeEventBus
      with all 14 components registered; the registry is the single answer
      to "what components exist" for agents, tools and scripts

### Deployment
- [x] Game export (package runtime + scene + resources) - via GameExporter
- [x] Windows support (CMake fixes, CI on windows-2022 with Vulkan SDK)
- [x] macOS support (CMake fixes, CI on macos-14)
- [x] Installer/deployment scripts (tools/deploy.sh, tools/deploy.ps1)
- [x] Release and Dist configurations built and tested in CI on all 3 platforms
- [x] CI/CD pipeline (Linux, Windows, macOS - Debug/Release/Dist + tests)
- [x] ASan + UBSan CI job (memory errors no test can observe)

> All three platform jobs previously failed at Configure and had never run a
> single build. Configure now succeeds on all of them, but only Linux has ever
> been built end to end; the Windows and macOS jobs still need one green run
> each before they can be called verified rather than configured. Both now dump
> `CMakeError.log` and `CMakeConfigureLog.yaml` on a configure failure, because
> the plain error text names a variable but not the target that wanted it —
> which is how the macOS Threads failure cost a wrong hypothesis first.

### ✨ New Features
- [x] SSAO sample positions uniform array (shader fixed, runtime setup complete)
- [x] UpdateDescriptorSets fully implemented
- [x] nvrhi submodule updated with improved error handling
- [x] forest.exr improved (20 → 68 bytes proper EXR)

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
| CI had never once passed, while being marked done | ✅ Fixed - every run failed at Configure, so the boxes for "CI on all 3 platforms" were claims, not observations |
| Linux CI installed no X11 headers | ✅ Fixed - GLFW's X11 backend is on by default on UNIX and hard-errors on missing Xrandr/Xinerama/Xcursor/Xi/XShape, so Configure died with "Could NOT find X11"; every header it demands now has its package |
| Windows CI could not find Vulkan | ✅ Fixed - `choco install vulkan-sdk` reported success, but a later step cannot see an environment variable an earlier step set, so `find_package(Vulkan REQUIRED)` failed; `VULKAN_SDK` is now re-read from the registry and re-exported |
| macOS CI could not find Threads | ✅ Fixed - Homebrew's CMake 3.31 took precedence over the image's own and its `FindThreads` probe rejects Apple Clang's `-pthread`; CMake is pinned to match the Ubuntu runner. Not reproducible locally, so the job also reports its toolchain |
| The sanitizers CI job ran out of memory | ✅ Fixed - assimp's IFC reader peaks at 1.27 GB per translation unit, 2.36 GB under ASan, so one job per core on a 4-core runner needed ~9.4 GB and died as `Killed signal terminated program cc1plus`, which reads as a compiler crash rather than an out-of-memory one. That job now builds at `-j2` |
| Every configure cloned Vulkan-Headers from GitHub | ✅ Fixed - `find_package(Vulkan)` ran *after* `add_subdirectory(nvrhi)`, so nvrhi could not see `Vulkan::Headers` and fell back to FetchContent. Configure now depends on the SDK the project already requires, with no network fetch |
| Post-process binding sets declared zero-byte ranges | ✅ Fixed - the sampler and constant-buffer items were hand-assembled and set `range = BufferRange(0, 0)`, which fails nvrhi's own `isEntireBuffer()` (it wants `~0ull` or the descriptor size), while `unused`, `rawData` and `overrideComponentMapping` were never assigned at all. They now go through nvrhi's factories |
| Every build compiled an unused C++20 fmt module | ✅ Fixed - `fmt-module` was built in all configurations with no `import fmt` and no target linking it; GCC 15 ICEs in `cp/module.cc` on it, reported as an internal compiler error naming neither fmt nor CMake. `FMT_MODULE` is now off |
| GLFW dragged in `<GL/gl.h>` on a Vulkan engine | ✅ Fixed - `GLFW_INCLUDE_NONE` is now defined at each of the three sites that include `GLFW/glfw3.h` (`fgpch.h`, `MouseEvent.h`, `TetrisTests.cpp`) and on the `imgui` target, whose GLFW backend includes it without defining the macro itself. Preprocessing all 80 project translation units went from 282 OpenGL-header lines in one file, plus 281 in ImGui's backend, to none |
| macOS configure died on `find_package(Threads)` | ✅ Fixed - not a CMake version problem (3.28.6 failed exactly like 3.31). `-Wl,--start-group` was in `CMAKE_EXE_LINKER_FLAGS`, which is also what `try_compile` links with, and Apple's linker does not accept that GNU ld option. Every compile-and-link probe after it failed, so a platform with pthreads in libSystem reported none. The link group is now Linux/Clang-only; macOS uses `-force_load` |
| `Dist` was appended to the configuration list unconditionally | ✅ Fixed - `string(REPLACE ";" ";" ...)` was a no-op used as an append, and CI passed `CMAKE_CONFIGURATION_TYPES=Debug,Release,Dist` with commas, so the list CMake worked from was `Debug,Release,Dist;Dist`. The list is rebuilt and de-duplicated now, and CI passes a real `;`-separated list |
| Scene serialization needed a library nothing provided | ✅ Fixed - `yaml-cpp` was linked as a bare name with no submodule, no `find_package` and no target behind it. The build only worked on a machine whose distribution happened to ship `libyaml-cpp-dev`, so CI failed with `yaml-cpp/yaml.h: No such file or directory` on a build that had been green locally for months. Now vendored as a submodule like every other dependency |
| MSVC was handed GCC compiler flags | ✅ Fixed - `target_compile_options(lua PRIVATE -std=gnu90 -Wno-unused-parameter)` was applied unconditionally, and MSVC reads `-std=gnu90` as switches: `D8021: invalid numeric argument`. Now split per compiler, `/std:c11` on MSVC |
| `imgui` could not find the Vulkan headers | ✅ Fixed - `imgui_impl_vulkan.h` includes `<vulkan/vulkan.h>` directly and nothing put the SDK on that target. Linux hid it because `/usr/include` is a default search path; Windows reported `C1083: Cannot open include file` |
| nvrhi did not compile against current Vulkan-Hpp | ✅ Fixed in the nvrhi fork - `DispatchLoaderDynamic::init` lost its default arguments when Vulkan-Hpp split the overload, so nvrhi's three-argument call no longer matched anything. The four-argument form is valid under both spellings, verified by compiling against headers 341 and 365 |
| macOS CI had no Vulkan SDK | ✅ Fixed - `find_package(Vulkan REQUIRED)` found nothing on the image. Installed via `brew install vulkan-loader`, which depends on `vulkan-headers` and so provides both |
| `UUID.h` did not compile with libc++ | ✅ Fixed - the `std::hash` specialisation re-declared the primary template (`template <typename T> struct hash;`). libc++ declares `std::hash` in an inline namespace, so the unqualified name matched both `std::hash` and `std::__1::hash` and the specialisation was ambiguous. libstdc++ resolved it differently, so it only ever failed on macOS. The redundant re-declaration is gone; the standard header is included instead |
| nvrhi was pointed at the system's Vulkan headers | ✅ Fixed - finding `Vulkan` before `add_subdirectory(nvrhi)` looks tidier and removes nvrhi's header fetch, but it silently substitutes the system headers for the pinned ones. nvrhi needs >= 1.4.318 and Ubuntu 24.04 ships 1.4.303, so the build died with "Vulkan SDK version 1.4.318 or later is required to compile NVRHI". `find_package(Vulkan)` now runs after nvrhi, deliberately |
| `vendor/physx` was on the include path but does not exist | ✅ Fixed - no PhysX has ever been vendored and no source includes it; physics is a native solver. The stale path is gone and `AGENTS.md` no longer lists Box2D/PhysX as a dependency |
| Staging textures were released twice | ✅ Fixed - `VulkanTexture` and the shadow mip pass called `->Release()` on objects owned by nvrhi's `RefCountPtr`; the refcount went negative, the object died while the lifetime tracker still referenced it, and the crash surfaced later in `runGarbageCollection`, far from the line that caused it |
| The albedo cache never reloaded and never retried | ✅ Fixed - a texture edited on disk was never reflected in the viewport, and a failed load (missing file) was cached as a permanent null for the session, so an asset created later by a tool was invisible until restart. The cache now carries a `FileSignature`; a changed signature re-decodes, a failed load retries as soon as the file exists, a failed reload keeps the last working texture, and the replaced GPU texture is evicted from the PBR draw cache before it dies |
| `HEAD` did not compile after the WIP commit | ✅ Fixed - the commit snapshotted `ViewportRenderer` mid-edit with an unmatched brace, so the default-cube PBR loop referenced out-of-scope variables; the three hand-rolled albedo-texture caches are now one `Renderer::GetAlbedoTexture`, which also logs a failed load once per path instead of once per frame |
| A saved scene never loaded its models | ✅ Fixed - `MeshComponent::ModelPath` was written and read but nothing imported it, so every modelled entity rendered a unit cube; the model is imported on the component hook, by the serializers after assignment, and by `entity:SetString("Mesh", "ModelPath", ...)` |
| `Mesh::Create` produced corrupt geometry for multi-mesh files | ✅ Fixed - each mesh's indices were appended without the vertex offset, and node transforms were never applied; one importer now serves the headless and GPU paths, offsets indices, applies transforms and rejects an out-of-range index |
| Duplicating an entity dropped three components | ✅ Fixed - `EnvironmentMapComponent`, `AudioSourceComponent` and `NativeScriptComponent` were in neither copy list; both paths now share `Scene::CopyComponentsTo` |
| The runtime scene format dropped authored data | ✅ Fixed - camera parameters, a material's albedo texture and the whole audio component never reached a `.rt` file; each addition is behind its own flag bit so older files still load |
| The simulation opened and closed the render command list | ✅ Fixed - `Scene::RenderScene` is gone; the application owns the frame and calls `Renderer::RenderScene` |
| "The primary camera" had no single answer | ✅ Fixed - the renderer had its own inline copy of the search, and any number of cameras could be marked primary; `Scene::GetPrimaryCameraEntity` is the one resolver and `SetPrimaryCamera` demotes the rest |
| `FG_ERROR` crashed the caller of a failure path | ✅ Fixed - the client log macros, unlike the core ones, dereferenced a null logger, so logging before `Log::Init` (a headless tool, a static initialiser) took the process down |
| The editor camera's projection ignored the panel | ✅ Fixed - the aspect ratio was fixed at 16:9 for the life of the editor while gizmo picking used the real viewport, so every drag picked in the wrong place; zoom level was stored and never used |
| Release did not compile | ✅ Fixed - `ReverbTests` tripped GCC's `stringop-overflow` false positive on a loop-invariant vector copy at `-O2`, and `-Werror` made it a build failure |
| The albedo hot-reload test failed on a fast machine | ✅ Fixed - it rewrote a file with identical byte size inside one filesystem timestamp tick, which a size+timestamp signature cannot see; the rewrite now changes the size |
| The polled input always answered "not pressed" | ✅ Fixed - `Input` asked GLFW for "the current context", which is an OpenGL convention: a Vulkan window has no context, so the handle was null in every application this engine builds and every polled key reported "not pressed". The platform layer now registers the window it opened (`Input::SetNativeWindow`), and the editor camera flies |
| A click in the viewport never selected anything | ✅ Fixed - the pointer interaction was drawn outside any ImGui window, so ImGui put it in its implicit fallback window ("Debug##Default") - a 400x400 box that is not the picture. The interaction now runs on the viewport image itself, and the ray is unprojected through the image's rectangle |
| The editor camera could not be moved | ✅ Fixed - nobody called `EditorCamera::OnUpdate`, and the scene camera's transform was mirrored into the editor camera every frame, so the keys moved the camera and the next frame put it back. The camera is driven by the frame loop now, and the scene camera is followed only while it is being changed |
| The editor's default scene was empty | ✅ Fixed - a mesh's path is not its geometry, and the geometry is imported from the path: the default scene assigned paths after the component was added, so nothing was imported, nothing was drawn and nothing could be picked. The import runs wherever a path is set, and the component remembers the path its geometry came from |
| The Lua source cache outlived the session | ✅ Fixed - `s_ScriptSourceCache` was only cleared at `Shutdown`, so editing a script and pressing Play again answered with the previous session's file; a session now reads its scripts from disk, and the cache only serves repeated spawns within that session |
| The next piece was drawn in the middle of the board | ✅ Fixed - the game loads the scene it ships, and that path created the preview root without placing it, so the queued piece hung at the origin - on the playfield instead of beside it. The tray's place is one rule now (`BoardView::GetPreviewTray`), used by the preview, by the tray slab under it and by the camera that frames both; and a game started in 3D builds the game's own well, which the scene's flat board is not |
