# Forge Engine

A production-grade, AI-assisted 3D game engine for Windows, macOS, and Linux (Ubuntu 24+).

## Features

- **3D Rendering**: Vulkan-based renderer with PBR materials, IBL/HDRIs, soft shadow maps, SSAO, and HDR pipeline
- **ECS Architecture**: Entity-Component-System using enTT for flexible scene authoring
- **Physics**: Integrated 3D physics (Box2D/PhysX) controllable via scripting
- **Scripting**: Lua-based scripting API for entity behavior
- **Audio**: miniaudio-based audio system
- **Editor**: Full-featured editor with scene graph, gizmo manipulation, and AI agent control
- **Cross-Platform**: Windows, macOS, Linux

## Tech Stack

- **Core**: C++20, CMake
- **Rendering**: Vulkan + nvrhi
- **Math**: GLM
- **Audio**: miniaudio
- **Scripting**: Lua
- **Physics**: Box2D/PhysX
- **Testing**: Catch2

## Building

### Prerequisites

- CMake 3.22+
- C++20 compiler (GCC 11+, Clang 12+, MSVC 2019+)
- Vulkan SDK
- Git

### Setup

```bash
# Clone with submodules
git clone --recursive https://github.com/TheCherno/AIGameEngine.git
cd AIGameEngine

# Configure
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build . --config Debug
```

### Running

```bash
# Editor
./bin/ForgeEditor

# Runtime
./bin/ForgeRuntime

# Tests
ctest --output-on-failure
```

## Project Structure

```
src/Forge/          # Core engine library
src/Platform/       # Platform-specific implementations
editor/             # Editor application
runtime/            # Standalone runtime
tests/              # Unit tests
resources/          # Game assets
vendor/             # Third-party dependencies
```

## Development

See [AGENTS.md](AGENTS.md) for development guidelines and code style conventions.

## License

MIT
