# Forge Engine - Development Guidelines

## Architecture Overview

Forge Engine is a production-grade 3D game engine built with C++ and CMake. It follows the Hazel Engine architecture pattern (see [Hazel docs](https://docs.hazelengine.com/HazelForEngineers/DeveloperGuide)).

### Directory Structure

```
ForgeEngine/
├── src/Forge/          # Core engine library (static lib)
│   ├── Core/           # Base, Logging, Window, Layers, Application
│   ├── Events/         # Event system
│   ├── Renderer/       # Rendering pipeline (Vulkan/nvrhi)
│   ├── Scene/          # ECS (entt-based)
│   ├── Physics/        # Physics integration
│   ├── Scripting/      # Lua scripting
│   ├── Audio/          # Audio (miniaudio)
│   └── Debug/          # Profiling/Instrumentation
├── src/Platform/Vulkan/ # Platform-specific Vulkan implementations
├── vendor/             # Third-party dependencies
├── editor/             # Editor application (ImGui-based)
├── runtime/            # Standalone runtime application
├── tests/              # Unit tests (Catch2)
└── resources/          # Game assets (scenes, models, textures, HDRIs)
```

### Build System

- **CMake** (3.22+) for all builds
- Static library: `Forge` (core engine)
- Executables: `ForgeEditor`, `ForgeRuntime`
- Tests: `ForgeTests`
- Output: `bin/` directory

### Dependencies

| Library | Purpose |
|---------|---------|
| Vulkan SDK + nvrhi | Rendering abstraction |
| GLFW | Windowing/input |
| GLM | Math |
| enTT | ECS |
| spdlog | Logging |
| fmt | String formatting |
| Lua | Scripting |
| miniaudio | Audio |
| stb | Image loading |
| assimp | Model loading (glTF) |
| Box2D/PhysX | Physics |
| Catch2 | Testing |
| yaml-cpp | Scene serialization |
| ImGui | Editor UI |

## Code Style (Hazel Convention)

### Naming Conventions

- **Classes**: PascalCase (`Application`, `Renderer`, `Scene`, `Entity`)
- **Structs**: PascalCase for components (`TransformComponent`, `TagComponent`)
- **Enums**: PascalCase (`EventType`, `EventCategory`)
- **Member variables**: `m_` prefix (`m_Window`, `m_Running`)
- **Static members**: `s_` prefix (`s_Instance`, `s_CoreLogger`)
- **Namespaces**: PascalCase (`Forge`, `Forge::Renderer`)
- **Macros**: `FG_` prefix (`FG_CORE_INFO`, `FG_EVENT_CLASS_TYPE`)
- **Private methods**: `On` prefix for event handlers (`OnEvent`, `OnUpdate`)
- **Getters**: `Get` prefix (`GetWindow`, `GetWidth`)

### Smart Pointers

```cpp
// Use Forge's custom wrappers (Hazel convention)
Forge::Scope<T> = std::unique_ptr<T>
Forge::Ref<T> = std::shared_ptr<T>

// Factory functions
auto scope = Forge::CreateScope<MyClass>(args...);
auto ref = Forge::CreateRef<MyClass>(args...);
```

### Precompiled Header

All source files must include `fgpch.h` as the first include. This header includes all common dependencies.

### Assert Macros

```cpp
// Core assertions (Debug builds; message is the stringified condition)
FG_CORE_ASSERT(condition);
FG_CORE_ASSERT_MSG(condition, "message {0}", arg);

// Client assertions (FG_ENABLE_ASSERTS)
FG_ASSERT(condition, "message {0}", arg);
```

### Logging

```cpp
// Core logger (engine internals)
FG_CORE_INFO("Message");
FG_CORE_ERROR("Error: {0}", value);

// Client logger (application/game code)
FG_INFO("Message");
FG_ERROR("Error: {0}", value);
```

### Event System

```cpp
// Define event class
class MyEvent : public Event
{
public:
    MyEvent(int data) : m_Data(data) {}

    std::string ToString() const override
    {
        std::stringstream ss;
        ss << "MyEvent: " << m_Data;
        return ss.str();
    }

    FG_EVENT_CLASS_TYPE(MyEvent)
    FG_EVENT_CLASS_CATEGORY(EventCategoryApplication)
private:
    int m_Data;
};

// Handle events in OnEvent()
bool OnMyEvent(MyEvent& e)
{
    // Handle...
    return false; // Don't consume
}
```

### Entity-Component System

```cpp
// Create entity
auto entity = scene.CreateEntity("Player");

// Add component
auto& transform = entity.AddComponent<TransformComponent>();
transform.Translation = { 0.0f, 0.0f, 0.0f };

// Access component
auto& tag = entity.GetComponent<TagComponent>();

// Query entities
auto view = scene.GetAllEntitiesWith<TransformComponent, RigidbodyComponent>();
for (auto entity : view)
{
    auto [transform, rb] = view.get<TransformComponent, RigidbodyComponent>(entity);
}
```

### Scene Serialization

Scenes are serialized using YAML (.forge format):
```yaml
Scene: MyScene
Entities:
  - Entity: 1234567890
    TagComponent:
      Tag: "Player"
    TransformComponent:
      Translation: [0, 0, 0]
      Rotation: [0, 0, 0]
      Scale: [1, 1, 1]
```

## Testing

- Use **Catch2** for all unit tests
- Tests live in `tests/src/`
- Run with: `ctest --output-on-failure`
- Every new feature must have at least one test
- Tests should be fast and deterministic (no real Vulkan calls in unit tests)
- Test categories: `[core]`, `[scene]`, `[renderer]`, `[physics]`, `[scripting]`, `[audio]`

## Git Workflow

1. **Commit early, commit often** - atomic commits with descriptive messages
2. **Code review** - verify against these guidelines before committing
3. **Test before commit** - ensure all tests pass
4. **No hacks** - production-grade code only
5. **Push to GitHub** after all checks pass

## Production Requirements

- No raw pointers where smart pointers work
- No memory leaks (verified with valgrind/sanitizers)
- No undefined behavior
- Thread-safe where applicable
- Proper RAII for all resources
- Comprehensive error handling with logging
- Platform-agnostic core (Vulkan abstraction)

### Error handling convention

The engine has been bitten repeatedly by operations that reported success by
default rather than by verification, so the rule is explicit:

- **Every fallible public API is `[[nodiscard]]`.** The compiler enforces it.
- **Every fallible call site must do something with the result.** Handle it,
  log it, forward it, or cast to `void` with a comment saying why it is
  intentionally ignored.
- **`bool` means "verify, don't assume."** A function returning `bool` has
  actually checked. Never return `true` unconditionally as a placeholder.
- **UI and tooling must surface failures.** The editor reports save, open and
  export failures as notifications rather than discarding them; an agent driving
  `EditorContext` relies on these return values to know whether work persisted.
- **Predicates are exempt.** `IsValid()`, `HasScript()`, `IsWorldAtRest()` and
  similar are not `[[nodiscard]]`; ignoring a predicate is normal.

## Editor Requirements

The editor must be fully controllable by AI agents:
- Scene graph navigation via scripting
- Entity creation/deletion via API
- Component property modification
- Asset import/export
- Scene save/load
- Play/Stop simulation
- Camera control
- Gizmo manipulation

### Editor Architecture

- **Scene Hierarchy Panel**: Shows all entities in the scene, supports selection
- **Inspector Panel**: Shows component properties of selected entity, allows editing
- **Viewport**: 3D rendering area with editor camera
- **Toolbar**: File operations, entity creation, play/pause/stop
- **Properties Panel**: Global editor settings

### Editor API for AI Agents

The editor model lives in `Forge::EditorContext` (window-independent, fully
unit-tested). Both the ImGui editor and AI agents drive the same API, so a
headless agent can build a complete game without a GUI.

```cpp
Forge::EditorContext editor;

// Scene management
editor.NewScene();
editor.OpenScene("resources/scenes/level.forge");
editor.SaveScene("resources/scenes/level.forge");

// Create entities (TransformComponent is added automatically)
auto entity = editor.CreateEntity("Player");
entity.GetComponent<Forge::TransformComponent>().Translation = { 0.0f, 1.0f, 0.0f };

// Add / remove components
auto& pbr = entity.AddComponent<Forge::PBRMaterialComponent>();
pbr.Albedo = { 1.0f, 0.0f, 0.0f };

// Selection + queries
editor.SelectEntity(entity);
editor.FindEntityByName("Player");
editor.GetAllEntities();

// Duplicate / delete
auto copy = editor.DuplicateEntity(entity);
editor.DestroyEntity(entity);

// Prefabs
editor.InstantiatePrefab("Enemy", /*usePosition*/ true, { 3.0f, 0.0f, 3.0f });

// Playback (drive it manually for deterministic simulation)
editor.Play();
editor.Pause();
editor.Step(1);
editor.Update(Forge::Timestep(1.0f / 60.0f));
editor.Stop();
```

## Build Architecture

- **ForgeCore** — headless engine core (ECS, physics, scripting, serialization,
  CPU primitives/textures, assets, editor model). Built with
  `FG_NO_VULKAN_RUNTIME` so unit tests exercise it without a graphics device.
- **ForgeApp** — platform layer (window + renderer + application shell). Links
  the Vulkan backend (`vulkan` target built on nvrhi).
- **vulkan** — nvrhi Vulkan backend + Forge's platform classes.
- **ForgeTests** — links only `ForgeCore`.
- **ForgeEditor / ForgeRuntime** — link `ForgeApp` + `forge_vk_dispatch`.

## Renderer Architecture

### Pipeline Stages

1. **G-Buffer Pass**: Render position, normal, material to textures
2. **Shadow Pass**: Render shadow map from light perspective
3. **HDR Pass**: Apply PBR lighting, accumulate to HDR buffer
4. **SSAO Pass**: Screen-space ambient occlusion
5. **Tonemapping Pass**: ACES tonemapping, convert to LDR
6. **Final Pass**: Composite SSAO, output to swapchain

### Shader Types

- **PBR Shader**: Main mesh rendering with PBR materials
- **SSAO Shader**: Screen-space ambient occlusion
- **HDR Shader**: HDR accumulation and tonemapping
- **Final Shader**: Final composite with SSAO overlay
- **Shadow Shader**: Shadow map rendering

## Audio System

Audio is **optional**. A machine with no output device still runs the game: the
engine logs a warning and every playback call returns false.

- Decoding (`AudioEngine::LoadAudioBuffer`) is device independent, so it works
  headless and is testable in CI.
- Every playing sound has a stable handle, so one sound can be stopped, pitched
  or re-volumed without touching any other.
- `AudioSourceComponent` plays a clip, and the Scene starts, updates and stops
  sources around playback. Authored fields are serialized; `SoundHandle` and
  `Finished` are per-session and never written to disk.
- A finished one-shot releases its handle rather than holding it for the session.
- Adding the component mid-simulation starts playback, so a script that attaches
  a source at runtime is actually heard.

```cpp
AudioEngine::SoundHandle sound = AudioEngine::InvalidSound;
AudioEngine::PlaySound("resources/audio/jump.ogg", 1.0f, 1.0f, false, &sound);
AudioEngine::SetSoundVolume(sound, 0.5f);
AudioEngine::StopSound(sound);      // only this sound
```

### Click-free playback

Changing volume or stopping a sound instantly produces an audible click: the
waveform jumps discontinuously. Sounds therefore fade in on start, ramp volume
changes, and fade out before release.

```cpp
AudioEngine::SetFadeDuration(0.02f);   // 0 = instant, clicking playback
```

`StopSound` begins a fade-out and the sound is reclaimed on the next
`AudioEngine::Update()` (called once per frame by the Scene), because it has to
keep playing for the duration of the fade — releasing it immediately would cut
the decay off, which is exactly the click being avoided. So after `StopSound`
the handle stays valid and `IsSoundPlaying` stays true until the fade finishes;
a second `StopSound` on the same handle returns false, letting callers tell
"stopped" from "stopping".

`StopAllSounds` releases outright instead of fading, because it is teardown
(shutdown, leaving playback) and sounds must not outlive the scene that owns
them.

### Reverb

A Freeverb-style algorithmic reverb (8 feedback comb filters then 4 allpass
filters, per channel) lives in the engine as **pure DSP on PCM**:

```cpp
Forge::ReverbSettings settings;      // RoomSize, Dampening, WetLevel, DryLevel, Width
settings.RoomSize = 0.8f;

Forge::ReverbProcessor reverb(settings, 48000);
std::vector<float> processed(samples.size());
reverb.Process(samples.data(), processed.data(), frames);
```

This is implemented here rather than delegated to miniaudio's node graph,
because **the vendored miniaudio exposes only `sound` and `group` node types** —
there are no DSP nodes to attach a reverb to. Being pure arithmetic, every
property is unit-testable with no audio device: the tail decays, the dry signal
passes through untouched, wet+dry cannot clip, and the parameters actually change
the output.

Two details that are easy to get wrong, and were:

- **Comb feedback must be applied when writing back into the delay line**
  (`buffer = input + filterstore * feedback`), not when reading forward
  (`output = delayed + filterstore * feedback`). The latter raises the loop gain
  above one and the bank diverges to infinity within a second.
- **Width applies to the wet signal only.** Applying it to the dry path alters
  the original signal, which is not what a width control should do. At width 0
  the tail is mono; at width 1 each channel keeps its own.

Processing is a separate step from playback: load a clip, run it through a
processor to bake in the effect, then play the result. That suits music and
ambience, where the tail does not need to react to anything.

### Gapless looping

A looping sound needs `MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_LOOPING` on its data
source, **not** just `ma_sound_set_looping()`. Without the flag the resource
manager stops filling its buffer at the end of the clip, so the wrap is
discontinuous and audible as a seam — the classic click between loops.

Streaming interacts with this: the resource manager pre-fills its buffer at
init, and streaming a clip shorter than that fill glitches audibly. So short
clips are buffered whole and only larger ones are streamed:

```cpp
AudioEngine::SetStreamingThreshold(4ull << 20);   // 4 MB
AudioEngine::PlaySound("music.ogg", 1.0f, 1.0f, /*loop*/ true, &handle);
```

`ShouldStream(size, loop)` is the pure policy function, so the rule is testable
without a device.

### Mixing buses

Sounds route through one of five buses — `Master`, `Music`, `SFX`, `Voice`,
`UI`. A sound's effective volume is `bus volume x its own volume x spatial
gain`, so ducking an entire category is one call no matter how many sounds it
holds:

```cpp
AudioEngine::SetBusVolume(AudioBus::Music, 0.3f);   // duck under dialogue
```

`Master` multiplies into every other bus, so it is the global limiter.

Bus names parse case-insensitively and accept aliases (`sfx`, `sound`, `ui`), so
a scene file or script can use the obvious word. An unknown name returns false
rather than being silently mapped somewhere — a typo that quietly did nothing
would leave music playing over dialogue.

From Lua:

```lua
SetBusVolume("Music", 0.3)
GetBusVolume("Music")
PlaySound("resources/audio/jump.ogg", 1.0, 1.0, false, "SFX")
```

`AudioSourceComponent::Bus` assigns an authored source to a bus, and is
serialized by name. An unrecognised name in a scene file falls back to the
default rather than failing the load.

### 3D positional audio

`AudioSourceComponent::Spatial` makes a sound's world position come from its
`TransformComponent`; it is then attenuated and panned relative to the listener,
which the Scene drives from the primary camera. Music and UI leave it off, so
they play at full volume and centred regardless of camera motion.

Attenuation is linear between `MinDistance` and `MaxDistance`, with `Rolloff` as
an exponent (1 = natural). Pan comes from the source's offset along the
listener's right axis, so a source dead ahead *or directly behind* is centred.

Spatial maths lives in the engine rather than miniaudio, because that version of
the library exposes no listener accessor. The benefit is that it is a pure
function of positions, so the curve is unit-testable with no audio device:

```cpp
const auto mix = AudioEngine::ComputeSpatialMix(listener, sourcePos, spatialParams);
// mix.Gain in [0,1], mix.Pan in [-1,1]

AudioEngine::SetListener(listener);
AudioEngine::SetSoundPosition(handle, newPos);
AudioEngine::UpdateSpatialization();   // once per frame
```

From Lua:

```lua
local ok, handle = PlaySound("resources/audio/jump.ogg", 1.0, 1.0, false)
entity:PlaySound("resources/audio/jump.ogg", 0.8, 1.0, false)  -- AudioSourceComponent
entity:StopSound()
```

## Scripting System

### Lua Integration

- Global functions: `GetTime()`, `GetDeltaTime(ts)`, `SpawnPrefab(name)`
A script is a Lua file in `resources/scripts/` that returns a table with
optional `OnCreate(entity)`, `OnUpdate(entity, dt)`, `OnDestroy(entity)`,
`OnCollision(other, nx, ny, nz)` and `OnCollisionEnd(other, nx, ny, nz)`.
An entity's `ScriptComponent::ClassName` selects the file (one instance of the
chunk is created per entity, so scripts keep their own state).

```lua
-- resources/scripts/PlayerController.lua
local M = {}
M.ticks = 0

function M.OnCreate(entity)
    entity:SetPosition(0, 0, 0)
end

function M.OnUpdate(entity, dt)
    M.ticks = M.ticks + 1
    local x, y, z = entity:GetPosition()
    entity:SetPosition(x + dt, y, z)
end

return M
```

### Collision hooks in Lua

Every scripted entity is automatically wired to the physics collision bridge, so
no registration is needed. A script only defines the hooks it wants:

```lua
-- resources/scripts/Player.lua
local M = {}

M.Hits = 0
M.LastHitBy = ""

function M.OnCollision(entity, other, nx, ny, nz)
    M.Hits = M.Hits + 1
    -- `other` is nil if the partner body has no entity.
    M.LastHitBy = other and other:GetName() or ""
end

return M
```

Contacts are edge events: `OnCollision` fires on the step the contact starts,
`OnCollisionEnd` on the step it stops. A contact that persists across steps
fires neither.

### Inspecting script state

A script's own fields are readable and writable from C++, tools and AI agents,
which is how tests assert on script behaviour without exporting globals:

```cpp
double hits = 0.0;
ScriptEngine::GetScriptNumber(entity, "Hits", hits);

std::string name;
ScriptEngine::GetScriptString(entity, "LastHitBy", name);

ScriptEngine::SetScriptNumber(entity, "Hits", 0.0);   // reset
```

### Available API

- Globals: `GetTime()`, `Log(msg)`, `SpawnPrefab(name, x, y, z)`,
  `PlaySound(path, volume, pitch, loop)` -> `ok, handle`,
  `StopSound(handle)`, `Scene`
- Entity: `PlaySound(path, volume, pitch, loop)`, `StopSound`,
  `GetName/SetName`, `Get/SetPosition`, `Get/SetRotation`,
  `Get/SetScale`, `HasComponent`, `AddComponent`, `RemoveComponent`,
  `Get/SetFloat`, `Get/SetBool`, `Get/SetString`, `Get/SetVector3`,
  `Duplicate`, `Destroy`
- Scene: `CreateEntity(name)`, `DestroyEntity(entity)`,
  `GetEntityByName(name)`, `GetEntitiesWithComponent(type)`
  (both `Scene:Method(...)` and `Scene.Method(...)` work)

Component type names: `Camera`, `Script`, `Rigidbody`, `BoxCollider`,
`SphereCollider`, `Light`, `PBRMaterial`, `Mesh`, `Transform`, `Audio`.

### Native Scripting

- Derive from `ScriptableEntity`
- Override `OnCreate()`, `OnUpdate(Timestep)`, `OnDestroy()`
- Bind via `NativeScriptComponent::Bind<T>()`

## Physics

### Components

- `RigidbodyComponent`: Body type (Static/Dynamic/Kinematic), fixed rotation
- `BoxColliderComponent`: Offset, size, density, friction, restitution
- `SphereColliderComponent`: Offset, radius, density, friction, restitution

### Body types

| Type | Gravity | Collision response | Moved by |
|------|---------|--------------------|----------|
| `Static` | no | none (infinite mass) | nothing, unless teleported |
| `Kinematic` | no | acts as infinite mass | its own `Velocity` |
| `Dynamic` | yes | full impulse response | forces, gravity, contacts |

A kinematic body is the right choice for moving platforms, elevators and moving
hazards: it is driven by velocity rather than forces, yet dynamic bodies collide
with it as if it were immovable, and they inherit its motion when they land on
it. Kinematic bodies never sleep.

Positional correction is weighted by inverse mass, so when one partner is
immovable the movable body absorbs the entire correction rather than half of it.

### Continuous collision detection

Discrete detection only tests overlaps at step boundaries, so a fast body passes
clean through a thin wall. Enable CCD to subdivide each frame adaptively:

```cpp
Physics::SetContinuousCollision(true);
Physics::SetMaxSubsteps(8);              // worst-case cost bound
Physics::GetLastSubstepCount();          // diagnostics
```

The substep count is derived from the fastest body's motion and the thinnest
collider in the scene, so each substep never moves a body more than half the
thinnest collider. A stationary scene still costs exactly one substep, and the
loop exits early once every movable body is below the sleep velocity threshold.

Contacts are accumulated across all substeps of a frame, so an impact that is
detected and resolved on substep 2 of 17 still fires exactly one `Begin` event.

CCD is opt-in. Without it the previous hard velocity clamp applies instead.

### Integration

- Physics step runs in `OnUpdateRuntime` and `OnUpdateSimulation`
- Transform translations are copied into dynamic bodies before the step, so
  scripts and the editor can move an entity during play
- Transforms are updated from physics bodies after step
- Collision detection (AABB + sphere) with impulse response and friction
- Raycasting (ray-AABB / ray-sphere) via Physics::Raycast
- Sphere casting via Physics::SphereCast

### Collision events

Contacts are reported as edges, not as a per-step stream. A pair fires
`Begin` on the step it starts touching and `End` on the step it stops; a pair
that is still touching on the next step fires neither.

At the physics level:

```cpp
// Per-body callback. `normal` points away from the surface towards `self`.
Physics::SetCollisionCallback(body, &OnContact);
void OnContact(PhysicsBody& self, PhysicsBody& other,
               const glm::vec3& normal, Physics::ContactPhase phase);

// Contact pairs resolved during the step that just completed.
for (const auto& key : Physics::GetActiveContacts())
    FG_INFO("contact {0} <-> {1}", key.A, key.B);
```

At the scene level, entities get the same events without touching bodies:

```cpp
scene.SetCollisionHandler(entity, &OnEntityCollision);
void OnEntityCollision(Entity self, Entity other, const glm::vec3& normal,
                       Forge::Physics::ContactPhase phase);

// `other` is a null Entity when the partner body has no entity.
```

Handlers are keyed by entity UUID, so they survive `OnPhysicsStop` /
`OnPhysicsStart` and are re-attached to the bodies as they are built. They may
also be registered mid-simulation, or unregistered by passing `nullptr`.

### Sleeping

Bodies whose speed stays under the thresholds fall asleep and stop integrating:

```cpp
Physics::SetSleepVelocityThreshold(0.05f);  // metres/second
Physics::SetSleepTimeThreshold(0.5f);       // seconds
```

A collision wakes both bodies in the pair.
