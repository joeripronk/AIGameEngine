#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Forge { namespace Vulkan {

class Device;

// Debug-name budget: 255 UTF-8 bytes plus the terminator the driver reads to
// (BRUTAL DebugNameExtensions).
constexpr size_t MaxDebugNameLength = 256;

// UTF-8-safe truncation: writes at most capacity-1 bytes of `name` plus a
// terminator, never cutting a multi-byte character in half, so the truncated
// prefix stays valid UTF-8. Returns the byte count written, excluding the
// terminator. `capacity` must be at least 2.
[[nodiscard]] size_t TruncateDebugName(std::string_view name, char* out, size_t capacity);

// Names a Vulkan handle for debuggers and RenderDoc. Compiled out of shipping
// builds (FG_DEBUG), and a no-op when the instance does not carry
// VK_EXT_debug_utils.
#if defined(FG_DEBUG)
void SetDebugName(const Device& device, VkObjectType objectType, uint64_t objectHandle, std::string_view name);
#else
inline void SetDebugName(const Device&, VkObjectType, uint64_t, std::string_view) {}
#endif

}}
