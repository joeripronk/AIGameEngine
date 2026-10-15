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

- Globals: `GetTime()`, `Log(msg)`, `SpawnPrefab(name, x, y, z)`, `Scene`
- Entity: `GetName/SetName`, `Get/SetPosition`, `Get/SetRotation`,
  `Get/SetScale`, `HasComponent`, `AddComponent`, `RemoveComponent`,
  `Get/SetFloat`, `Get/SetBool`, `Get/SetString`, `Get/SetVector3`,
  `Duplicate`, `Destroy`
- Scene: `CreateEntity(name)`, `DestroyEntity(entity)`,
  `GetEntityByName(name)`, `GetEntitiesWithComponent(type)`
  (both `Scene:Method(...)` and `Scene.Method(...)` work)

Component type names: `Camera`, `Script`, `Rigidbody`, `BoxCollider`,
`SphereCollider`, `Light`, `PBRMaterial`, `Mesh`, `Transform`.

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
