# Renderer Layer Layout (raw Vulkan)

`VkRenderer` is the engine's renderer: raw Vulkan + VMA, no abstraction layer.
It lives in `src/Platform/Vulkan/` on top of the parallel layers Phases 1-4 of
the BRUTAL rewrite put underneath it.

```
src/Platform/Vulkan/
├── Device/        VkInstance, VkPhysicalDevice, VkDevice, VkQueue, debug naming
├── Memory/        AllocationInfo, IAllocator/VmaDeviceAllocator, MappedMemory,
│                  StagingPool, UploadRegions
├── Resources/     Buffer / Image / ImageView RAII wrappers, Subresources
├── Frame/         SyncObjects, FrameResources, Swapchain, FrameLoop, Barriers
├── Pipeline/      PipelinePresets, VertexLayout (CPU struct = GPU layout),
│                  ShaderModule, DescriptorSetLayout + accounting DescriptorPool
├── VkRenderer*    the renderer itself (device, resources, pipelines, scene)
├── VkMesh / VkTexture  GPU mesh upload / texture upload
├── VulkanWindow   GLFW window + surface
└── ImGuiLayer     vendor imgui_impl_glfw + imgui_impl_vulkan
```

## Function resolution

Instance-scope entry points (`vkCreateInstance`, enumeration, extension
queries) use the system loader (`Vulkan::Vulkan`). Everything device-scope
goes through the per-device `DeviceFunctions` table resolved by
`vkGetDeviceProcAddr` in `Device/VkDevice.cpp`. There is no dynamic-dispatch
loader and no Vulkan-Hpp in the render path.

## Frame flow

`FrameLoop` drives acquire → fence wait → `ResetCommandPool` →
`Begin(OneTimeSubmit)` → record → `End` → submit → present. The acquire
semaphore ring has `ImageCount + 1` entries and rotates independently of the
frame index; per-image fences are created signaled. `OutOfDate` rebuilds the
swapchain next frame; a one-shot command buffer initializes every new
backbuffer `Undefined → PresentSrcKHR`.

`Renderer` (the engine-facing API in `src/Forge/Renderer/`) maps onto
`VkRenderer` pass calls: `BeginScene` opens the HDR pass, the seven-pass chain
is G-Buffer → Shadow → HDR (PBR + bloom composite) → SSAO → Bloom downsample +
3×16-tap blur → Tonemap (ACES) → Final (SSAO composite → swapchain), with
classic render passes and per-frame descriptor sets from the accounting pool.

## Ownership rules

- Every resource carries the allocator that made it; `Dispose` returns it
  there. `FrameResources::OwnsColorImage` keeps swapchain images alive when
  the framebuffer around them is rebuilt.
- Descriptor sets come from `DescriptorPool`, which keeps a per-type census
  from the layouts it has seen: an over-allocation is refused with human-
  readable counts before the driver is asked, and `FreeDescriptorSets`
  credits exactly what a set's layout charged.
- Every `VkWriteDescriptorSet` is built with `dstArrayElement = 0` for the
  arraySize-1 bindings the engine uses - the driver indexes by the field
  triple `(dstBinding, dstArrayElement, descriptorCount)`, and a `1` there
  walks past the allocated descriptor.
- Uploads go through the `StagingPool`: host-coherent `TRANSFER_SRC` buffers
  pooled by size, batch-submitted under one recycled fence, reclaimed on
  `WaitFor`; the texture copy plan is computed on the CPU by `UploadRegions`.

## Failure policy

Typed failure per the engine convention: every fallible API is
`[[nodiscard]]` and reports false with a log line - `VkResultError`-style
exceptions are a BRUTAL idiom, not a Forge one. Debug naming and RAII
command-buffer debug scopes are compiled in for `FG_DEBUG` only.
