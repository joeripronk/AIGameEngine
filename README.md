# Forge Engine

A production-grade, simple and straightforward 3D Game Engine for Windows, macOS and Linux (Ubuntu 24+). Built with C++, CMake, and Vulkan.

## Features

- **3D Rendering**: PBR materials, IBL with HDRIs, soft shadow maps, SSAO, HDR pipeline with tonemapping
- **ECS Architecture**: Entity-Component-System using enTT
- **Lua Scripting**: Full Lua integration for game logic
- **Physics**: Box2D/PhysX integration with rigidbody and collider components
- **Editor**: Full-featured ImGui-based editor with scene graph, inspector, and viewport
- **Scene Serialization**: YAML-based scene format (.forge)
- **Multi-platform**: Windows, macOS, Linux (Ubuntu 24+)

## Architecture

```
ForgeEngine/
├── src/Forge/          # Core engine library (static lib)
├── src/Platform/Vulkan/ # Vulkan platform implementations
├── editor/             # Editor application
├── runtime/            # Standalone runtime
├── tests/              # Unit tests
├── resources/          # Game assets
└── vendor/             # Third-party dependencies
```

## Building

### Prerequisites

- CMake 3.22+
- C++20 compiler (GCC 11+, Clang 14+, MSVC 2022+)
- Vulkan SDK
- GLFW, GLM, enTT, spdlog, fmt, Lua, miniaudio, stb, assimp, Box2D/PhysX, Catch2, yaml-cpp, ImGui

### Build Steps

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

### Output

- `bin/ForgeEditor` - Editor application
- `bin/ForgeRuntime` - Runtime application
- `bin/ForgeTests` - Test executable

## Testing

```bash
cd build
ctest --output-on-failure
```

## Development

See [AGENTS.md](AGENTS.md) for detailed development guidelines and coding standards.

## License

MIT
