# Forge Engine

A production-grade, simple and straightforward 3D Game Engine for Windows, macOS and Linux (Ubuntu 24+). Built with C++20, CMake, and Vulkan (via nvrhi).

## Features

- **ECS Architecture**: Entity-Component-System using enTT, with deep copy, duplication, prefabs and fast name/UUID lookup
- **Lua Scripting**: full scripting API — entities, components, scene queries, entity creation/destruction and prefab spawning, driven by `OnCreate`/`OnUpdate`/`OnDestroy`
- **3D Physics**: built-in rigid-body engine (AABB/sphere shapes, impulse response with friction, gravity, ray/sphere casting) driven by `RigidbodyComponent` + collider components
- **PBR Authoring**: PBR material components (albedo/metallic/roughness/AO/emissive), lights (directional/point/spot), CPU model import (glTF/GLB/OBJ/FBX via assimp) and procedural cube/plane/sphere primitives
- **Asset & Texture Pipeline**: `AssetRegistry` + `CpuTexture` (stb_image, LDR and Radiance HDR for IBL)
- **AI-controllable Editor**: `EditorContext` — a window-independent editor model (scene, selection, play/pause/step, gizmo, save/load) that both the ImGui editor and AI agents drive, so a game can be authored headlessly
- **Scene Serialization**: YAML `.forge` format plus a compact versioned binary runtime format
- **Audio**: miniaudio `ma_engine` mixer with device-independent decoding and playback
- **Game Export**: package the runtime + scene + resources into a distributable
- **Multi-platform**: Windows, macOS, Linux (Ubuntu 24+)

## Architecture

```
ForgeEngine/
├── src/Forge/          # Core engine (ForgeCore: headless) + platform layer (ForgeApp)
│   ├── Core/           # Application, window, logging, UUID, layers
│   ├── Renderer/       # Renderer, cameras, CPU mesh/texture, viewport
│   ├── Scene/          # ECS (Scene, Entity, Components), serializer, prefabs
│   ├── Editor/         # EditorContext (AI/headless editor model), gizmo math
│   ├── Physics/        # Rigid-body simulation + raycasting
│   ├── Scripting/      # Lua engine
│   ├── Audio/          # miniaudio engine
│   ├── Assets/         # AssetRegistry
│   └── Project/        # GameExporter
├── src/Platform/Vulkan/ # Vulkan (nvrhi) backend
├── editor/             # ImGui editor application (ForgeEditor)
├── runtime/            # Standalone runtime (ForgeRuntime)
├── tests/              # Catch2 unit tests (ForgeTests)
├── resources/          # Scenes, scripts, prefabs, models, textures, audio
└── vendor/             # Vendored third-party dependencies
```

**Targets:**
- `ForgeCore` — headless engine (ECS, physics, scripting, serialization, CPU mesh/texture, assets, editor model). Built with `FG_NO_VULKAN_RUNTIME` so unit tests run without a graphics device.
- `ForgeApp` — platform layer (window + renderer + application shell) linking the Vulkan backend.
- `vulkan` — nvrhi Vulkan backend + Forge platform classes.
- `ForgeTests` — links only `ForgeCore`.
- `ForgeEditor` / `ForgeRuntime` — link `ForgeApp`.

## Building

### Prerequisites

- CMake 3.22+
- A C++20 compiler (GCC 13+, Clang 15+, MSVC 2022+)

All third-party dependencies (GLFW, nvrhi, GLM, enTT, spdlog, fmt, Lua, miniaudio, stb, assimp, Catch2, ImGui, yaml-cpp) are **vendored**, so no system packages are required. nvrhi fetches a matching Vulkan-Headers at configure time.

### Build Steps

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

### Output

- `bin/ForgeEditor` — editor application
- `bin/ForgeRuntime` — standalone runtime
- `bin/ForgeTests` — unit tests

## Testing

The whole engine is exercised by 100+ Catch2 test cases (1000+ assertions) that run **headlessly** (no GPU required), covering the ECS, serialization, physics, scripting, asset pipeline, editor model and a canonical end-to-end test scene.

```bash
ctest --test-dir build --output-on-failure
```

## Development

See [AGENTS.md](AGENTS.md) for detailed development guidelines, coding standards and the engine APIs (editor, scripting, build layout).

## License

MIT