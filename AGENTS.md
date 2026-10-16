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
├── games/tetris/       # A game built on the engine (ForgeTetris)
├── tests/              # Unit tests (Catch2)
└── resources/          # Engine assets (scenes, models, textures, HDRIs)
```

### Build System

- **CMake** (3.22+) for all builds
- Static library: `Forge` (core engine)
- Executables: `ForgeEditor`, `ForgeRuntime`, `ForgeTetris`
- Tests: `ForgeTests`, `ForgeTetrisTests`
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

### Sanitizers find what tests cannot

A use-after-free reads a plausible value, so Debug and Release both pass while
the engine is corrupting memory. CI runs the suite under ASan + UBSan:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan && ASAN_OPTIONS=detect_leaks=0 ./build-asan/bin/ForgeTests
```

It has found two real defects that no assertion could see: a freed `PhysicsBody`
read for its ID after removal, and an upstream miniaudio use-after-free on the
clip-not-found path.

### Test assets must actually decode

A committed asset that is *nearly* right is worse than none, because it decodes
to plausible garbage. The HDRI fixture checks decoded **values**, not just that
a buffer came back: stb reads flat RGBE only when the width is under 8, so a
wider file must be adaptively RLE encoded with four component planes per
scanline — interleaving pixels instead decodes silently to noise.

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

## Build Configurations

Three configurations, all of which must build and pass the full suite:

| Config | Asserts | Debugbreak | Log level | Optimisation | Binary |
|--------|---------|------------|-----------|--------------|--------|
| `Debug` | on | SIGTRAP / `__debugbreak` | trace | none | ~159 MB |
| `Release` | off | — | trace | `-O2` | ~17 MB |
| `Dist` | off | — | warn | `-O2 -DNDEBUG` | ~16 MB |

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Dist   # shipping
tools/deploy.sh Dist
```

`FG_DEBUG` is defined **only** for `Debug`. This is the whole point: defining it
unconditionally put `FG_ENABLE_ASSERTS` and a `raise(SIGTRAP)` debugbreak into
every build, so a shipped game took SIGTRAP on an assertion failure — no log
line, no diagnostic, just a crash. `FG_ASSERT` / `FG_CORE_ASSERT` are gated on
`FG_ENABLE_ASSERTS` and genuinely compiled out, not merely made non-trapping, so
the reported state matches the behaviour.

Because the condition is not evaluated when asserts are off, a variable used
only inside an assert becomes unused in a shipping build. Mark it
`[[maybe_unused]]`; with `-Werror` that is a build break rather than a silent
problem.

`Dist` is declared explicitly with optimisation flags. CMake has no built-in
`Dist`, so its flags default to empty — which made the *shipping* binary larger
than an ordinary Release build.

The configuration is a queryable fact rather than a macro you have to trust:

```cpp
Forge::GetBuildConfigurationName();   // "Debug" / "Release" / "Dist"
Forge::AreAssertsEnabled();
Forge::DoesAssertTrap();
Forge::IsDistributionBuild();
Forge::Log::GetLogLevel();
```

All of it is covered by `BuildConfigTests`, which runs identically in every
configuration and asserts whichever branch applies.

### One runtime target, not two

The shipping executable is `ForgeRuntime` built with `-DCMAKE_BUILD_TYPE=Dist`.
There is no separate `ForgeRuntimeDist` target: the Dist behaviour lives in
`ForgeCore`, and a static library keeps the configuration it was compiled with,
so an executable target cannot switch it on. The old `ForgeRuntimeDist` had
identical sources and libraries and the same `OUTPUT_NAME`, so both wrote to
`bin/ForgeRuntime` — whichever linked last silently won.

## Games

A game is **not** part of the engine. `games/tetris/` builds `ForgeTetris`, which
links exactly what `ForgeRuntime` links — `ForgeApp`, `nvrhi`,
`forge_vk_dispatch` — and supplies the game itself.

The dependency arrows only point one way:

```
TetrisManager      → GLM only. The rules.
TetrisBoardView    → Forge::Scene. Mirrors the rules into entities.
TetrisInputLayer   → Forge events. Maps key codes to actions.
TetrisApp          → all of the above, plus the window and renderer.
src/Forge/**       → none of them.
```

Concretely, that means:

- **The engine does not branch on what is running.** No component of the game
  is in `SceneSerializer`'s known-component list, and nothing in `ForgeCore`
  references a game type. A gate like "start Tetris if the scene has a
  `TetrisBoardComponent`" is coupling wearing a disguise: the component is
  silently dropped on load, so the game never actually starts, and it looks like
  it works.
- **A game builds its own scene in code** rather than shipping a `.forge` that
  depends on the engine serialising its components. `EditorContext` is the tool
  for authoring scenes when an engine component *is* wanted.
- **Input arrives as an action, not a key code.** `Tetris::Action` is what the
  rules take, so they are pure arithmetic and the test target links `ForgeCore`
  only — no window, no Vulkan.
- **A game's tests are its own target** (`ForgeTetrisTests`), not part of
  `ForgeTests`. Engine tests that reach into `games/` would fail to build the
  moment the game was deleted: coupling in the other direction.

```bash
./run-tetris.sh
./build-tetris/bin/ForgeTetris
```

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

### Volume has one source of truth

Spatialisation recomputes gain every frame as
`authored volume x bus gain x spatial gain`. That makes the **authored volume
the single source of truth**: `SetSoundVolume` has to write it, not just the
mixer.

A setter that writes only the mixer looks correct until the next frame reverts
it, and silently discards the bus and spatial gain along the way — a change
that appears to work and then does not. So both paths compose gain through one
function:

```cpp
AudioEngine::ComputeEffectiveGain(baseVolume, bus);   // pure, unit-tested
AudioEngine::GetSoundVolume(handle);                   // reads back the mix
```

`GetSoundVolume` exists so the claim is verifiable rather than assumed: a
volume change can be asserted to survive a frame boundary, and to compose with
attenuation rather than replace it.

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

### Baking an effect to a file

Processing is a separate step from playback, and the result is a real asset
rather than something stranded in memory:

```cpp
auto processed = Forge::ProcessAudioFile("ambience.wav", settings);
processed->WriteWav("resources/audio/ambience_cave.wav");   // now streamable
```

`AudioBuffer::FromSamples` wraps in-memory PCM and `AudioBuffer::WriteWav`
emits 16-bit PCM, so baked audio is playable by the ordinary streaming path.
Samples outside [-1, 1] are clamped rather than cast: a wrapping cast turns an
out-of-range value into full scale of the *opposite* sign, which is a click
rather than a loud sample.

Note that `LoadAudioBuffer` decodes to a fixed 48 kHz **stereo** target
regardless of the file, so a mono source is upmixed and resampled on load, and
every decoded buffer carries that rate. Both the reverb's stereo requirement
and `WriteWav`'s header depend on this.

That suits music and ambience, where the tail does not need to react to
anything.

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

Authoring is available both ways. `EditorContext` drives it from an agent, and
the Inspector renders every authored field, with generic **Add Component** /
**Remove Component** menus plus a Scene > Create Audio Source item. ID, Tag and
Transform are never offered for removal: the engine reads them unconditionally
(`GetName`, `GetUUID`, the gizmo), so removing one would leave an entity in a
state nothing else handles.

### Removing a component releases what it owns

Removal is not the absence of an addition. `AddComponent` runs
`Scene::OnComponentAdded`, which builds a physics body when physics is active;
`RemoveComponent` runs `Scene::OnComponentRemoved`, which releases it. Skipping
the hook left the `PhysicsBody` alive, holding the old collider's dimensions and
still colliding with them — and left `RigidbodyComponent::RuntimeBody` dangling,
so the next `EnsurePhysicsBody` believed the entity already had a body.

The hook fires *before* the erase, while the Scene can still see the component.
Physics-bearing components declare an explicit specialisation in `Scene.h`; the
no-op primary lives in `Entity.h` because defining it needs a complete `Entity`
(`Scene.h` only forward-declares it).

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

- Global functions: `GetTime()`, `GetDeltaTime()`, `SpawnPrefab(name)`
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

bool looping = false;
ScriptEngine::GetScriptBool(entity, "Looping", looping);

ScriptEngine::SetScriptNumber(entity, "Hits", 0.0);   // reset
ScriptEngine::SetScriptBool(entity, "Looping", false);
ScriptEngine::SetScriptString(entity, "LastHitBy", "");
```

`GetScriptBool` checks `lua_isboolean` rather than calling `lua_toboolean`.
That distinction is the whole point: `lua_toboolean` maps both `nil` and `0` to
false, so a typo'd field name would read back as a legitimate `false` and the
caller could not tell the difference.

### GetDeltaTime

```lua
function M.OnUpdate(entity, dt)
    local x = x + GetDeltaTime() * speed   -- seconds this frame
end
```

Per **entity**, not a single global: two entities ticked in one frame share a
value, but an entity that is not being ticked must not report a stale one as if
it were current. `GetDeltaTime(entity)` is that entity's last frame;
`GetDeltaTime()` is the current frame's, for a script with no entity in hand.

Recorded before `OnUpdate` is called, so it is correct *inside* the hook rather
than one frame behind. Cleared on `OnRuntimeStart` — left over from a previous
run it would give a script's first `OnCreate` the last session's dt, and an
entity whose handle happened to be reused would inherit another's value. The
editor's play/stop/play cycle hits that every time.

`0.0` is returned when there is no frame to describe (before any update, or for
an entity that has never been ticked) rather than a substituted value.

### Mutating a component reports whether it worked

Every binding that **changes** something returns a boolean and logs the failure.
A setter used to discard the result, so

```lua
entity:SetFloat("Rigidbody", "Masa", 5)   -- typo: no "Masa"
```

did nothing at all and looked exactly like it had worked. A silent no-op is the
failure mode that costs the longest to find, so `SetFloat`, `SetBool`,
`SetString`, `SetVector3`, `RemoveComponent` and `Destroy` all return `true`
or `false`, and name the component field they could not find:

```
entity:SetFloat: no component field 'BoxCollider.Frikshun'
```

Scripts written before this ignore the answer and keep working unchanged.
Getters return `nil` on an unknown field, which Lua already handles naturally.

### Available API

- Globals: `GetTime()`, `GetDeltaTime([entity])`, `Log(msg)`,
  `SpawnPrefab(name, x, y, z)`,
  `PlaySound(path, volume, pitch, loop)` -> `ok, handle`,
  `StopSound(handle)`, `SetBusVolume(name, v)`, `GetBusVolume(name)`, `Scene`
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

`Get/SetFloat`, `Get/SetBool`, `Get/SetString` and `Get/SetVector3` take a
component name and a field name, so `entity:SetFloat("BoxCollider", "Friction",
0.5)`. The getters return `nil` for an unknown field; the setters return
`false` and log what they could not find.

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
