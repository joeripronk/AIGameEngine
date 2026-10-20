# Forge Engine

A production-grade, simple and straightforward 3D Game Engine for Windows, macOS and Linux (Ubuntu 24+). Built with C++20, CMake, and raw Vulkan (VMA for allocation).

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
├── src/Platform/Vulkan/ # Raw-Vulkan backend (VkRenderer + VMA)
├── editor/             # ImGui editor application (ForgeEditor)
├── runtime/            # Standalone runtime (ForgeRuntime)
├── tests/              # Catch2 unit tests (ForgeTests)
├── resources/          # Scenes, scripts, prefabs, models, textures, audio
└── vendor/             # Vendored third-party dependencies
```

**Targets:**
- `ForgeCore` — headless engine (ECS, physics, scripting, serialization, CPU mesh/texture, assets, editor model). Built with `FG_NO_VULKAN_RUNTIME` so unit tests run without a graphics device.
- `ForgeApp` — platform layer (window + renderer + application shell) linking the Vulkan backend.
- `vulkan` — raw-Vulkan backend (`VkRenderer`, Device/Memory/Resources/Frame/Pipeline layers) + Forge platform classes.
- `ForgeTests` — links only `ForgeCore`.
- `ForgeEditor` / `ForgeRuntime` — link `ForgeApp`.

## Building

### Prerequisites

| Requirement | Notes |
|-------------|-------|
| CMake 3.22+ | |
| A C++20 compiler | GCC 13+, Clang 15+, MSVC 2022+ |
| **Vulkan SDK** | `find_package(Vulkan REQUIRED)` runs at *configure* time, so configure fails without it — even to build the headless test binary |
| X11 + OpenGL development headers | Needed by **GLFW and ImGui**, not by Forge |
| yaml-cpp | Only Linux is affected: `vendor/yaml-cpp` does not exist, so the link resolves against the system library |

Everything else (GLFW, VulkanMemoryAllocator, GLM, enTT, spdlog, fmt, Lua, miniaudio, stb, assimp, Catch2, ImGui) is **vendored**; Lua is compiled from `vendor/lua/*.c` as part of the build.

Vulkan is required at *configure* time regardless of what you build, because `find_package(Vulkan REQUIRED)` in `src/CMakeLists.txt` is unconditional. There is no configure-only-tests path: a Vulkan SDK is needed to configure, not just to run.

### Linux system packages

Vendoring GLFW does not vendor its X11 dependencies, and two of them are only discovered when they are missing — GLFW fails at configure with `RandR headers not found`, and ImGui's GLFW backend fails much later with `fatal error: GL/gl.h: No such file or directory`. Install them up front:

```bash
sudo apt install libvulkan-dev libyaml-cpp-dev \
                 libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxxf86vm-dev \
                 libgl-dev
```

macOS needs only `brew install vulkan-headers yaml-cpp` (X11 and OpenGL frameworks are part of the SDK). Windows needs the Vulkan SDK installed; GLFW, GLM, enTT, fmt and spdlog build from source with no extra packages.

### Vendored dependencies are git submodules

`vendor/*` are gitlinks, so a fresh clone has empty directories and configure fails with:

```
The source directory .../vendor/assimp does not contain a CMakeLists.txt file.
```

Populate them before configuring:

```bash
git submodule update --init --recursive --depth 1
```

This fetches ~12 repositories (assimp dominates). `--depth 1` works because GitHub serves the pinned commits directly; drop it if you need submodule history.

`vendor/imgui` is vendored as plain files (v1.91.9), not a submodule. Pinned commits, if you ever need to clone one by hand:

| Path | Upstream | Pinned commit |
|------|----------|---------------|
| `vendor/assimp` | https://github.com/assimp/assimp | `3e9c4e1d38a8cf4cbc63513e0f23a58f70833e49` |
| `vendor/catch2` | https://github.com/catchorg/Catch2 | `08092139210881f01a34e60c5f2fd6afa0ea4024` |
| `vendor/entt` | https://github.com/skypjack/EnTT | `1dcd7aab6548786f7672ff578639188bd2f0d837` |
| `vendor/fmt` | https://github.com/fmtlib/fmt | `b4ffa39b9994c961418406f47358791d89a5aa48` |
| `vendor/glfw` | https://github.com/glfw/glfw | `92dcf4ce74f2e2554a98fea09be7c705c17daa5a` |
| `vendor/glm` | https://github.com/g-truc/glm | `f451d0ad6226ba4381c13d8ec497b98548671fc5` |
| `vendor/lua` | https://github.com/lua/lua | `1ab3208a1fceb12fca8f24ba57d6e13c5bff15e3` |
| `vendor/miniaudio` | https://github.com/mackron/miniaudio | `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d` |
| `vendor/VulkanMemoryAllocator` | https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator | `3aa921224c154a0d2c43912bc88e1c42ce1f7607` (v3.4.0) |
| `vendor/spdlog` | https://github.com/gabime/spdlog | `08155a51c532126597db917c42aabdac1b20f011` |
| `vendor/stb` | https://github.com/nothings/stb | `2c980bb59875b0d32144a71867fbdebb2f77cd20` |

### Build configurations

Three configurations, all of which build and run the same suite (the failures noted under [Testing](#testing) are configuration-independent):

| Config | Asserts | Log level | Optimisation | Use for |
|--------|---------|-----------|--------------|---------|
| `Debug` | on, trap on failure | trace | none | development (the default) |
| `Release` | compiled out | trace | `-O2` | profiling |
| `Dist` | compiled out | warn | `-O2 -DNDEBUG` | **shipping** |

`CMAKE_BUILD_TYPE` defaults to `Debug` when unset, so a bare `cmake -S . -B build` does not silently compile every assert out.

```bash
# Development
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)

# Shipping
cmake -S . -B build-dist -DCMAKE_BUILD_TYPE=Dist
cmake --build build-dist -j$(nproc)
```

`Dist` is not a separate target. The shipping executable is `ForgeRuntime` built with `-DCMAKE_BUILD_TYPE=Dist`; a previous `ForgeRuntimeDist` target had identical sources and the same `OUTPUT_NAME`, so both wrote to `bin/ForgeRuntime` and whichever linked last silently won.

### Build types with multi-config generators

Visual Studio and Xcode append the configuration to the output path, so binaries land in `build/bin/Debug/`, `build/bin/Release/` or `build/bin/Dist/` rather than `build/bin/`. `tools/deploy.sh` and `tools/deploy.ps1` look for `build/bin/ForgeRuntime[.exe]`, so deploy with a single-config generator (Ninja/Makefiles) or point the scripts at the config subdirectory.

### Where the binaries end up

CMake sets the output directories relative to the **build** directory, not the source tree (`CMakeLists.txt:29-31`), so with the commands above:

```
build/
├── bin/
│   ├── ForgeEditor            # editor application
│   ├── ForgeRuntime           # standalone runtime (the shipping binary)
│   ├── ForgeTests             # Catch2 unit tests
│   ├── Forge.log              # log written next to the binary
│   └── resources/             # copied from the source tree at *configure* time
└── lib/                       # ForgeCore/ForgeApp plus every vendored static lib
```

| Artefact | Debug path | Installed to |
|----------|-----------|--------------|
| Editor | `build/bin/ForgeEditor` (`.exe`) | — |
| Runtime | `build/bin/ForgeRuntime` (`.exe`) | `bin/` via `cmake --install` |
| Tests | `build/bin/ForgeTests` (`.exe`) | — |
| Static libs | `build/lib/` | `lib/` |
| Headers | — | `include/Forge/` |

In a Debug build the libraries carry a `d` suffix (`libForgeAppd.a`) — **except** `libForgeCore.a`. assimp sets `CMAKE_DEBUG_POSTFIX` as a *cache* variable, so it leaks globally and applies to every target created after `add_subdirectory(assimp)`. `ForgeCore` is declared at the top of `src/CMakeLists.txt`, before any dependency, so it keeps the bare name while `ForgeApp`, `vulkan`, `Catch2` and the rest get the postfix. Don't hard-code either spelling.

Three consequences worth knowing:

- **`resources/` is a copy, not a link.** `file(COPY ...)` in `editor/CMakeLists.txt` and `runtime/CMakeLists.txt` runs during *configure*, so editing a resource is not reflected in `build/bin/resources/` until you re-run `cmake -S . -B build`. Run the binaries from `build/bin/` so their relative `resources/` paths resolve.
- **`Forge.log` lands next to the binary**, not in the build root — worth knowing when you run from an unexpected directory and wonder where the log went.
- **`build/bin/` is disposable.** Everything in it is regenerated; `build/deploy/` (see below) is the only output meant to be kept.


### Shaders

The engine consumes SPIR-V directly (raw Vulkan shader modules). The generated `.spv` files next to the GLSL in `resources/shaders/` are committed, so a normal engine build does not need `glslang`. After editing a shader:

```bash
tools/compile_shaders.sh          # requires glslangValidator (apt install glslang-tools)
```

### Packaging a distributable

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Dist
cmake --build build -j$(nproc)
tools/deploy.sh Dist              # tools/deploy.ps1 on Windows
```

This packages `ForgeRuntime` + `resources/` into `build/deploy/<platform>-<BuildType>/` and writes a `ForgeRuntime-<platform>-<BuildType>-<date>.tar.gz` beside it. Note the script reads from `build/`, so build `Dist` into `build/` (or copy the binary across) before deploying.

## Running

```bash
cd build/bin
./ForgeEditor
./ForgeRuntime                            # loads resources/scenes/EngineTest.forge
./ForgeRuntime path/to/your.forge         # or an explicit scene
```

- `FG_ENV_MAP=/path/to/hdri.exr` — override the environment map used for IBL.
- `FG_CAPTURE_PATH=/tmp/frame.ppm` — have the runtime write out a frame instead of opening a window.
- `FG_RESIZE_TO=1900x1040` — ask the window to change shape a few frames in (the editor and the game). There is no window manager to drag the window with on a headless machine, and the resize path is the thing that has to work.

The Tetris game ships a project (`Tetris.forgeproject`) and the scene it
opens (`resources/scenes/Tetris.forge`), so the editor can open the game and
edit it:

```bash
./build/bin/ForgeEditor --project Tetris.forgeproject
```

The scene is authored with the editor's own model
(`games/tetris/tools/AuthorScene.cpp`), so what the tool writes is what the
editor would write.

If the runtime cannot load the requested scene it logs an error and falls back to a built-in scene rather than exiting.

## Testing

The engine is exercised by 310 Catch2 test cases (~2200 assertions) covering the ECS, serialization, physics, scripting, asset pipeline, editor model and a canonical end-to-end test scene.

```bash
ctest --test-dir build --output-on-failure
```

`ForgeTests` links only `ForgeCore`, which is built with `FG_NO_VULKAN_RUNTIME`, so it needs no GPU and no display — only a Vulkan SDK for the configure step described above. Tests use `FORGE_SOURCE_DIR` to find committed fixtures, and write scratch files (`test_*.forge`, `test_prefabs/`, `test_export/`, …) into the current working directory, so run them somewhere writable.

> **6 tests currently fail on a clean checkout**, all because fixtures are missing from the repository. `resources/models/`, `resources/textures/` and `resources/prefabs/` do not exist at all, so `CpuMesh imports a model file via assimp`, `CpuTexture loads a PNG from disk`, `Prefab library loads committed prefab` and `Every asset the test scene names exists` load nothing. The two `EditorContext` save/open round-trips write into `test_assets/`, which `SaveScene` does not create. Tracked in [TODO.md](TODO.md).

Under sanitizers, because a use-after-free reads a plausible value and passes in Debug *and* Release:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan && ASAN_OPTIONS=detect_leaks=0 ./build-asan/bin/ForgeTests
```

Run the suite in all three configurations: `BuildConfigTests` runs identically in each and asserts whichever branch applies, so it is the canary for a misconfigured build.

## Development

See [AGENTS.md](AGENTS.md) for detailed development guidelines, coding standards and the engine APIs (editor, scripting, build layout).

## License

MIT