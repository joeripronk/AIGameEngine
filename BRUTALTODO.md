# BRUTAL Rewrite Plan

Rewrite Forge's Vulkan renderer with inspiration from [BRUTAL](https://github.com/ahwootech/BRUTAL)
(checkout at `~/src/BRUTAL`). BRUTAL is a raw-Vulkan **framework**, not a renderer - it supplies
the layers a renderer is built on and exactly one reference frame loop (the ImGui Vulkan backend).
Forge keeps its renderer architecture (G-Buffer → Shadow → HDR → SSAO → Bloom → Tonemap → Final);
what changes is the layer underneath it.

## Ground rules

- **Raw Vulkan + VMA.** The nvrhi abstraction is removed from the render path. nvrhi stays as a
  submodule pinned to upstream `https://github.com/NVIDIA-RTX/NVRHI.git` (done - `6b96fb0`,
  `.gitmodules` updated, fork branch `forge-commandlist-lifetime` dropped) only until the new
  renderer reaches parity, then it is deleted from the build.
- BRUTAL idioms are ported as **C++/RAII**, not translated C#. Naming stays Hazel convention
  (`m_` members, `On`/`Get` prefixes, `FG_` macros) - see AGENTS.md.
- Every fallible API is `[[nodiscard]]`; typed failure at every layer (BRUTAL throws
  `VkResultError` per result code - Forge logs + reports false, per the engine's error convention).
- Pure maths stays pure and unit-tested: `Bloom.h`, `ShadowCamera.h`, `IBL.h` are reused as-is.
- Shaders stay GLSL/SPIR-V in `resources/shaders/` (compile_shaders.sh, StartupIntegrityTests).
  Slang (BRUTAL's shader path) is a **later evaluation**, not part of this rewrite.
- No real Vulkan calls in unit tests; the pure layers (allocation policy, barrier plans,
  descriptor accounting, vertex layout, frame sync state machine) are what gets tested.

## Key BRUTAL references (consult while implementing)

| Idiom | BRUTAL file |
|---|---|
| Reference frame loop (swapchain/acquire/present/semaphore ring) | `Brutal.ImGui.Backends/RendererBackends/VulkanBackend.cs:1205-1353`, `VulkanBackend.Helpers.cs` |
| One-shot command buffers + whole-pool reset | `VulkanBackend.cs:1263-1311` |
| Staging pool (multi-record, batch submit, fence recycle, rewind) | `Brutal.Vulkan.Abstractions/StagingPool.cs` |
| Barrier triple for uploads (HostWrite→TransferRead / Undefined→TransferDst / TransferDst→ShaderReadOnly) | `VulkanBackend.cs:545-636` |
| Allocator interfaces + VMA-shaped AllocationInfo | `Brutal.Vulkan.Abstractions/Allocator/*.cs`, `Brutal.Vulkan.Vma/Manual/VmaAllocator.cs` |
| Resource wrappers (`BufferEx`/`ImageEx`/`ImageViewEx`, allocator-carried, Dispose returns to owner) | `Brutal.Vulkan.Abstractions/{BufferEx,ImageEx,ImageViewEx}.cs` |
| RAII mapped memory, subset views, refcounted nested maps | `MappedMemory.cs`, `IMappable.cs`, `DeviceEx.cs:222-276` |
| SyncObjects / FrameResources as plain data with ownership flags (`OwnsColorImage`) | `Brutal.Vulkan.Abstractions/{SyncObjects,FrameResources}.cs` |
| Accounting descriptor pool (per-type census, fail-fast OOM diagnostics) | `DescriptorPoolEx.cs`, `DescriptorSetLayoutEx.cs` |
| Pipeline presets as data (blend/depth/raster/sampler/topology blocks) | `Presets.cs` |
| Reflection-driven vertex input (CPU struct *is* the GPU layout) | `VertexInput.cs` |
| Subresource/format helpers (`Subresources.Full/Mips/Layers`, aspect masks) | `Extensions/{SubresourceExtensions,FormatExtensions}.cs` |
| Queue submit lock; per-device function table | `Manual/Classes/Queue.cs:73-98`, `Device.cs:84-85` |
| Debug names on every allocation + RAII debug scopes | `DebugNameExtensions.cs`, `Extensions/CommandBufferExtensions.cs` |
| Staging→image copy plan computed on CPU (row padding, `lcm(4,block)` alignment, region batching) | `Brutal.Texture.Abstractions/Upload/UploadRegions.cs` |
| Queue family selection preferring the family with the *fewest* extra capabilities | `Extensions/PhysicalDeviceExtensions.cs:48` |
| Memory type selection with *preferred* flags ranked by popcount | `Manual/Classes/PhysicalDevice.cs:130-169` |

## Phase 0 - Setup (DONE)

- [x] Branch `brutalrewrite` created.
- [x] nvrhi submodule reset to upstream NVIDIA-RTX/NVRHI (`6b96fb0`); `.gitmodules` url updated,
      fork branch removed. Full build and `ForgeTests` + `ForgeTetrisTests` pass on upstream.
- [x] Commit the setup (branch, `.gitmodules`, submodule pointer, this plan).
- [x] Add `vendor/VulkanMemoryAllocator` submodule (`https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git`)
      and wire it into `src/CMakeLists.txt` (replace nvrhi's vendored VMA usage).
      Pinned `v3.4.0`; the `vma` target is the copy the new renderer allocates through -
      nvrhi compiles its own vendored copy until Phase 6 deletes it.

## Phase 1 - Device layer (`src/Platform/Vulkan/Device/`)

Hand-written handle wrappers, thin, explicit ownership (BRUTAL `Manual/Classes` pattern):

- [x] `VkInstance` - instance, extension/layer lists from `VulkanHelpers`-style bootstrap
      (validation "mandatory if supported"), debug messenger with severity callbacks,
      eager physical device enumeration.
- [x] `VkPhysicalDevice` - extension name sets (allocation-free span lookup),
      `FindMemoryType(bits, required, preferred)` with popcount ranking,
      `FindSupportedFormat(candidates, tiling, features)`, queue family selection that
      prefers the family with the fewest extra capabilities (dedicated transfer wins).
- [x] `VkDevice` - per-device function table resolved via `vkGetDeviceProcAddr`
      (kills the `forge_vk_dispatch` / Vulkan-Hpp dynamic-dispatch workaround block in
      `src/CMakeLists.txt:243-252,395-403`), cached `VkQueue` objects, VMA allocator
      creation, device-address feature enable.
- [x] `VkQueue` - every submit/present/wait-idle serialized by one mutex; record on any
      thread, submit through the queue object.
- [x] Debug naming helper: `SetDebugName(handle, name)` on every resource, DEBUG-only,
      UTF-8-safe 255-byte truncation.
- Tests: memory-type preference ranking, queue family choice, format selection (pure parts).
      (`tests/src/DeviceLayerTests.cpp` - 6 cases, 25 assertions)

## Phase 2 - Memory + resources (`src/Platform/Vulkan/Memory/`, `Resources/`)

- [ ] `AllocationInfo` (VMA-shaped: allocator, memory, offset, size, memoryType, handle),
      `MemoryPreference {None, PreferGpu, PreferCpu}`, device-address flag chaining.
- [ ] `IAllocator` / `IBufferAllocator` / `IImageAllocator` interfaces; `VmaAllocator`
      implementation (Preference → `VmaMemoryUsage` mapping). Only implementation for now -
      the interface exists so a ring/buddy allocator can be added later.
- [ ] `Buffer` / `Image` / `ImageView` RAII wrappers carrying their allocator; `Dispose`
      returns the resource to whichever allocator made it. `Image::CreateView`, cached full
      subresource range, `MaxMipLevels`.
- [ ] `MappedMemory` RAII: map → `AsSpan<T>()` → `Flush`/`Invalidate` exact range → unmap;
      subset views share the parent map; free refuses while mapped.
- [ ] `StagingPool`: host-coherent `TRANSFER_SRC` buffers pooled by size, growable command
      buffer list with cursor, batch submit under one recycled fence, `WaitFor` reclaims
      signalled submissions, full `ResetCommandPool` only when nothing is left recorded.
- [ ] `UploadRegions`: pure CPU copy-plan builder for textures (per-mip layout, row pitch,
      `lcm(4, texelBlock)` alignment, region batching) → list of `bufferToImageCopy` regions.
      This is pure arithmetic → Catch2 tests against hand-computed layouts.
- [ ] `Subresources::Full/Single/Mips/Layers`, format aspect-mask helpers.
- Tests: upload region plans (mip pyramid math, KTX-style row padding), staging pool state
  machine (record/submit/reclaim/rewind), mapped-memory subset validation.

## Phase 3 - Frame, swapchain, synchronization (`src/Platform/Vulkan/Frame/`)

- [ ] `SyncObjects` struct: acquire semaphore ring (`ImageCount + 1`, rotates independently of
      frame index), render-finished semaphore, per-image fence (created signaled), one-shot
      command buffer, explicit pool-ownership flag.
- [ ] `FrameResources` struct: framebuffer + color/depth/multisample attachments with
      `OwnsColorImage` flag so swapchain images are never destroyed.
- [ ] `VkSwapchain`: surface format / present mode selection with fallbacks, min image count
      from present mode, `PreTransform`, `OldSwapchain` recreation chain, `OutOfDate` →
      rebuild-next-frame, `Suboptimal` → flag; one-shot CB initializing every backbuffer
      `Undefined → PresentSrcKHR`.
- [ ] Frame loop: acquire → fence wait → `ResetCommandPool` → `Begin(OneTimeSubmit)` →
      record → `End` → submit → present. Resize-safe, no leaks on repeated recreation
      (verified with ASan build).
- [ ] Barrier helpers: `ImageBarrier` / `BufferBarrier` builders with queue-family-ignore
      defaults; the upload triple from the reference; RAII `DebugScope("Shadow pass")`
      command labels.
- Tests: swapchain parameter selection (pure), frame-index/semaphore-ring rotation logic,
  resize state machine (headless mock).

## Phase 4 - Pipelines, shaders, descriptors (`src/Platform/Vulkan/Pipeline/`)

- [ ] `ShaderModule::FromSpirv` (replace `VulkanShader`), shader library keeps its
      `resources/shaders/*.spv` contract and the StartupIntegrityTests checks.
- [ ] `PipelinePresets`: `constexpr`/static state blocks - blend (None/Alpha/Additive/
      SoftAdditive/Multiplicative), depth-stencil variants, rasterizer (fill/line × cull),
      topologies, samplers (linear clamped/repeat, aniso 16×, point). Composed by pointer
      at PSO creation, exactly the BRUTAL `Presets.cs` pattern.
- [ ] Vertex layout from the CPU struct: `VertexLayout::For<TRoot, TVertex, TInstance>()`
      template with a `FG_VERTEX_FIELDS(...)` metadata macro mapping member types to
      `VkFormat` (float2→R32G32_SFLOAT, normalized byte4, etc.), cached per type,
      `Validate()` returning an error enum. Replaces hand-written `VkVertexInputAttributeDescription`
      arrays in `VulkanVertexArray`/`PbrRenderer`.
- [ ] `DescriptorSetLayout` + **accounting `DescriptorPool`**: per-type census from layouts,
      allocation validated against a scratch copy of pool sizes, out-of-sets / out-of-descriptors
      reported with human-readable counts, credit-exact free (set→layout map).
- [ ] Pipeline cache (explicit `VkPipelineCache`, dumped/loaded across sessions - out of scope
      for parity, tracked as TODO here).
- [ ] `RenderPass` equivalents: keep classic render passes first; **dynamic rendering
      (`vkCmdBeginRendering`) is the target** for the pass rewrite (Phase 5) since it removes
      most render-pass objects; decide at Phase 5 start, record the decision below.
- Tests: vertex layout generation for the engine's real vertex structs (PBR vertex, shadow
  vertex, fullscreen vertex) against expected binding/offset/format tables; descriptor pool
  accounting (over-allocation reports, credit-exact free, layout census).

## Phase 5 - Renderer rewrite (`src/Forge/Renderer/` + `src/Platform/Vulkan/`)

Port the seven passes from `Renderer.cpp` (currently ~7.5k lines with `PbrRenderer` on nvrhi)
onto Phases 1-4:

- [ ] `VkRenderer` (new): device init, framebuffer set for the offscreen chain
      (G-Buffer position/normal/material, HDR, SSAO, bloom half-res ping-pong, shadow map),
      sampler set, per-pass pipeline selection, per-frame descriptor updates.
- [ ] Passes in engine order: G-Buffer → Shadow → HDR (PBR + bloom composite) → SSAO →
      Bloom downsample + 3×16-tap blur → Tonemap (ACES) → Final (SSAO composite → swapchain).
      Same formulas, same uniforms, same `Bloom.h`/`ShadowCamera.h` inputs - the existing
      renderer-stats counters (`RendererStats.h`) must survive the port.
- [ ] Frustum culling, picking, gizmos: untouched (pure layers) - they must keep passing.
- [ ] ImGui: replace `ImGuiLayer`'s nvrhi path with the vendor `imgui_impl_vulkan` (+ the
      BRUTAL backend as the behavioural reference for barriers/descriptor/upload).
- [ ] ViewportRenderer / editor panels render through `VkRenderer`.
- [ ] Parity gate: `startup_check.sh` green under xvfb+lavapipe, editor/runtime/tetris visually
      identical, frame stats match, ASan suite green, valgrind clean on the editor.

## Phase 6 - Remove nvrhi

- [ ] Delete `src/Platform/Vulkan/{VulkanRendererAPI,VulkanVertexArray,VulkanVertexBuffer,
      VulkanIndexBuffer,VulkanMesh,VulkanShader,VulkanTexture,VulkanSwapchain,VulkanWindow
      (nvrhi parts),PbrRenderer,ImGuiLayer nvrhi path}`, `forge_vk_dispatch`, the nvrhi
      `add_subdirectory` + dispatch-definition blocks in `src/CMakeLists.txt`, and the
      `nvrhi`/`nvrhi_vk` link lines in editor/runtime/tetris CMake.
- [ ] Drop the `vendor/nvrhi` submodule; move `VulkanDispatch.cpp`'s dispatch storage to the
      plain `vkCreateInstance`-first pattern (no dynamic loader needed if we link `vulkan`
      directly - decide static vs dynamic loader, record it).
- [ ] Update AGENTS.md (dependency table, Renderer Architecture, build architecture sections),
      README, `tools/deploy.sh`, `tests/integration/startup_check.sh` hints.
- [ ] Docs: `docs/` renderer notes describing the new layer layout.

## Decisions to record as they are made

| Date | Decision | Why |
|---|---|---|
| 2026-10-19 | nvrhi reset to upstream, fork branch dropped | fork patches (commandlist lifetime, CHECK_VK_FAIL, Vulkan-Hpp init) are made obsolete by the raw-Vulkan rewrite; upstream builds and passes tests unchanged |
| 2026-10-19 | Raw Vulkan + VMA, no Vulkan-Hpp in render path | BRUTAL's layering is the inspiration; per-device function tables + handle wrappers map 1:1 to C++; kills the dispatch-loader workarounds |
| 2026-10-19 | Classic render passes first, dynamic rendering evaluated at Phase 5 | parity first, simplification second |
| 2026-10-19 | Vulkan loader: link the system loader (`Vulkan::Vulkan`), no dynamic-dispatch loader | raw `vk*` calls need the loader only at instance scope; everything device-scope goes through the `DeviceFunctions` table resolved by `vkGetDeviceProcAddr` (Phase 1), which is what kills `forge_vk_dispatch` in Phase 6 |
| | Dynamic rendering: yes/no | |
| | Slang evaluation | |

## Working order note

Phases 1-4 land as a **parallel layer** (`src/Platform/Vulkan/{Device,Memory,Resources,Frame,Pipeline}`)
with unit tests, while the nvrhi renderer keeps running - the engine stays green at every commit.
Phase 5 is the switch-over; Phase 6 is the cleanup. No half-migrated renderer files: a pass is
ported completely or not at all.
