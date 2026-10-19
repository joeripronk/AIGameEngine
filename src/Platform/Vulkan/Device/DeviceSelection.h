#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

namespace Forge { namespace Vulkan {

// Plain-data mirrors of the physical-device queries every selection rule
// answers from. Keeping them plain data is the whole point: these rules
// decide where every allocation lands, which queue submits the work and
// which format the framebuffer uses, and they must be testable with no
// graphics device attached (BRUTAL PhysicalDevice / PhysicalDeviceExtensions).

struct MemoryTypeDesc
{
	VkMemoryPropertyFlags Properties = 0;
	uint32_t HeapIndex = 0;
};

static_assert(sizeof(MemoryTypeDesc) == sizeof(VkMemoryType), "MemoryTypeDesc must mirror VkMemoryType layout");

// Mirrors VkQueueFamilyProperties *in full*, not just its first two fields.
// The selection rules read this array straight out of the driver's
// vkGetPhysicalDeviceQueueFamilyProperties buffer, so the mirror has to
// match the driver's element size exactly - a smaller mirror would make
// the array stride wrong and every family after the first would be read
// from the wrong offset. The stride is what matters, not the field count.
struct QueueFamilyDesc
{
	VkQueueFlags QueueFlags = 0;
	uint32_t QueueCount = 0;
	uint32_t TimestampValidBits = 0;
	VkExtent3D MinImageTransferGranularity{};
};

static_assert(sizeof(QueueFamilyDesc) == sizeof(VkQueueFamilyProperties), "QueueFamilyDesc must mirror VkQueueFamilyProperties layout");

struct FormatFeatures
{
	VkFormatFeatureFlags LinearTiling = 0;
	VkFormatFeatureFlags OptimalTiling = 0;
};

// First memory type inside the filter that carries every required flag.
// -1 when no candidate qualifies.
[[nodiscard]] int FindMemoryType(
	uint32_t memoryTypeBits,
	VkMemoryPropertyFlags required,
	std::span<const MemoryTypeDesc> types);

// Preference-ranked variant: a type carrying every preferred flag wins
// outright; among imperfect candidates the one with the most preferred-flag
// hits wins, ties going to the earliest index. -1 when no candidate carries
// every required flag.
[[nodiscard]] int FindMemoryType(
	uint32_t memoryTypeBits,
	VkMemoryPropertyFlags required,
	VkMemoryPropertyFlags preferred,
	std::span<const MemoryTypeDesc> types);

// Family carrying all of required, none of excluded, and the fewest extra
// capabilities - so a dedicated transfer family wins over the do-everything
// family when one exists. -1 when no family qualifies.
[[nodiscard]] int FindQueueFamily(
	VkQueueFlags required,
	std::span<const QueueFamilyDesc> families,
	VkQueueFlags excluded = 0);

// First candidate whose tiling features cover `features`, VK_FORMAT_UNDEFINED
// when none qualifies. The properties query is injected so the rule is
// testable against a table and runs against a real device unchanged.
[[nodiscard]] VkFormat FindSupportedFormat(
	std::span<const VkFormat> candidates,
	VkImageTiling tiling,
	VkFormatFeatureFlags features,
	const std::function<FormatFeatures(VkFormat)>& properties);

}}
