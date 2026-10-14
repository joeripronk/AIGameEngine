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
// Core assertions (always active in Debug)
FG_CORE_ASSERT(condition, "message");

// Client assertions (active when FG_ENABLE_ASSERTS is defined)
FG_ASSERT(condition, "message");
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

```cpp
// AI can access the editor through the Application singleton
auto& app = Forge::Application::Get();
auto* editor = static_cast<EditorApp*>(&app);

// Create entities
auto entity = editor->GetScene()->CreateEntity("NewEntity");
auto& transform = entity.AddComponent<Forge::TransformComponent>();
transform.Translation = { 0.0f, 1.0f, 0.0f };

// Modify components
auto& pbr = entity.AddComponent<Forge::PBRMaterialComponent>();
pbr.Albedo = { 1.0f, 0.0f, 0.0f };

// Delete entities
editor->GetScene()->DestroyEntity(entity);

// Save scene
Forge::SceneSerializer::Serialize(editor->GetScene(), "scene.forge");
```

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
- Entity metatable: `entity:GetName()`, `entity:SetPosition(x,y,z)`, etc.
- Scene metatable: `Scene.CreateEntity(name)`, `Scene.DestroyEntity(entity)`

### Native Scripting

- Derive from `ScriptableEntity`
- Override `OnCreate()`, `OnUpdate(Timestep)`, `OnDestroy()`
- Bind via `NativeScriptComponent::Bind<T>()`

## Physics

### Components

- `RigidbodyComponent`: Body type (Static/Dynamic/Kinematic), fixed rotation
- `BoxColliderComponent`: Offset, size, density, friction, restitution
- `SphereColliderComponent`: Offset, radius, density, friction, restitution

### Integration

- Physics step runs in `OnUpdateRuntime` and `OnUpdateSimulation`
- Transforms are updated from physics bodies after step
- Collision detection via Box2D (2D) or PhysX (3D)
