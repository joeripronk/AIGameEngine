# Setting up the build environment on Windows

This document describes how to build Forge Engine on Windows (MSVC).

## Prerequisites

1. **Visual Studio Build Tools 2022** (or newer)
   - Workloads/components:
     - "Desktop development with C++"
     - MSVC v143 (or v145) C++ x64/x86 build tools
     - Windows 10/11 SDK
   - Install location used in CI: `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`

2. **CMake** 3.22 or newer
   - Download from https://cmake.org/download/ or install via your package manager.
   - Verify: `cmake --version`

3. **Git** with submodule support
   - Clone with submodules:
     ```
     git clone --recursive https://github.com/<org>/AIGameEngine.git
     ```
   - Or initialize after clone:
     ```
     git submodule update --init --recursive
     ```

4. **Vulkan SDK** 1.4.x
   - Download the Windows installer from https://vulkan.lunarg.com/sdk/home
     (e.g. `vulkansdk-windows-X64-1.4.363.0.exe`).
   - Install to the default location `C:\VulkanSDK\<version>` (e.g. `C:\VulkanSDK\1.4.363.0`).
   - The SDK provides `vulkan-1.lib`, headers, `glslc`/`glslangValidator`, and the loader.

## Environment variables

Set `VULKAN_SDK` to the installed SDK root and add its `Bin` directory to `PATH`:

PowerShell (current session):
```
$env:VULKAN_SDK = 'C:\VulkanSDK\1.4.363.0'
$env:PATH = "$env:VULKAN_SDK\Bin;$env:PATH"
```

To make it permanent, set `VULKAN_SDK` in System Environment Variables and add
`%VULKAN_SDK%\Bin` to `PATH`. The LunarG installer usually does this; if a
fresh step cannot see it (as in CI), re-export it from the registry:
```
$sdk = (Get-ItemProperty -Path 'HKLM:\SOFTWARE\Khronos\Vulkan\SDK').VulkanSDK
```

## Configure

Open a "Developer PowerShell for VS 2022" (or run `vcvars64.bat`), then:

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_CONFIGURATION_TYPES="Debug;Release;Dist"
```

Visual Studio is a multi-config generator: the configuration is chosen at
build time with `--config`, and `Dist` must be declared up front (it is not a
built-in CMake configuration).

## Build

```
cmake --build build --config Debug
```

Replace `Debug` with `Release` or `Dist` as needed. Outputs go to
`build\bin\<Config>\` (e.g. `build\bin\Debug\ForgeTests.exe`).

## Run tests

```
ctest --test-dir build -C Debug --output-on-failure
```

`ForgeTests` and `ForgeTetrisTests` link only `ForgeCore` (built with
`FG_NO_VULKAN_RUNTIME`), so they run without a display or GPU.

## Run the editor / runtime

Run from the repository root so the engine can find `resources/` (paths are relative
to the working directory), or copy `resources/` next to the executable:

```
robocopy resources build\bin\Debug\resources /E
```

```
cd C:\Users\micro\src\AIGameEngine
build\bin\Debug\ForgeEditor.exe
build\bin\Debug\ForgeRuntime.exe
build\bin\Debug\ForgeTetris.exe
```

A Vulkan-capable GPU/driver is required to run the editor and runtime. For
software fallback, set:
```
$env:VK_ICD_FILENAMES = 'C:\VulkanSDK\1.4.363.0\Bin\vk_swiftshader_icd.json'
```
(or install Mesa lavapipe) and run under `xvfb`-equivalent on Windows.

## Shaders

The engine loads `resources\shaders\*.spv`. If they are missing or stale, run:
```
tools\compile_shaders.sh   # requires glslc on PATH (provided by the Vulkan SDK)
```

## Notes for MSVC

- `/W4 /utf-8` are used for engine targets; `/utf-8` is required by spdlog/fmt.
- `NOMINMAX` and `WIN32_LEAN_AND_MEAN` are defined in `src/fgpch.h` to avoid
  macro clashes (`min`/`max`/`small`/`PlaySound`) from the Windows headers.
- Submodules must be initialized; `vendor/` is not vendored as plain files.
