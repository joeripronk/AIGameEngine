#include "fgpch.h"
#include "Platform/Vulkan/Device/DeviceSelection.h"

namespace Forge { namespace Vulkan {

namespace {

	uint32_t CountBits(uint64_t value)
	{
		uint32_t count = 0;
		while (value != 0)
		{
			count += static_cast<uint32_t>(value & 1u);
			value >>= 1u;
		}
		return count;
	}

}

int FindMemoryType(
	uint32_t memoryTypeBits,
	VkMemoryPropertyFlags required,
	std::span<const MemoryTypeDesc> types)
{
	for (uint32_t i = 0; i < types.size(); ++i)
	{
		if ((memoryTypeBits & (1u << i)) == 0)
			continue;
		if ((types[i].Properties & required) == required)
			return static_cast<int>(i);
	}
	return -1;
}

int FindMemoryType(
	uint32_t memoryTypeBits,
	VkMemoryPropertyFlags required,
	VkMemoryPropertyFlags preferred,
	std::span<const MemoryTypeDesc> types)
{
	int bestIndex = -1;
	uint32_t bestHits = 0;

	for (uint32_t i = 0; i < types.size(); ++i)
	{
		if ((memoryTypeBits & (1u << i)) == 0)
			continue;
		if ((types[i].Properties & required) != required)
			continue;

		const VkMemoryPropertyFlags prefMatch = types[i].Properties & preferred;
		if (prefMatch == preferred)
			return static_cast<int>(i);

		const uint32_t hits = CountBits(static_cast<uint64_t>(prefMatch));
		if (hits > bestHits)
		{
			bestIndex = static_cast<int>(i);
			bestHits = hits;
		}
	}

	return bestIndex;
}

int FindQueueFamily(
	VkQueueFlags required,
	std::span<const QueueFamilyDesc> families,
	VkQueueFlags excluded)
{
	int bestFamily = -1;
	uint32_t bestExtra = UINT32_MAX;

	for (uint32_t i = 0; i < families.size(); ++i)
	{
		const VkQueueFlags flags = families[i].QueueFlags;

		if ((flags & required) != required)
			continue;
		if ((flags & excluded) != 0)
			continue;

		const uint32_t extra = CountBits(static_cast<uint64_t>(flags & ~required));
		if (extra >= bestExtra)
			continue;

		bestFamily = static_cast<int>(i);
		bestExtra = extra;
	}

	return bestFamily;
}

VkFormat FindSupportedFormat(
	std::span<const VkFormat> candidates,
	VkImageTiling tiling,
	VkFormatFeatureFlags features,
	const std::function<FormatFeatures(VkFormat)>& properties)
{
	for (const VkFormat format : candidates)
	{
		const FormatFeatures supported = properties(format);
		const VkFormatFeatureFlags available = (tiling == VK_IMAGE_TILING_LINEAR)
			? supported.LinearTiling
			: (tiling == VK_IMAGE_TILING_OPTIMAL ? supported.OptimalTiling : 0);

		if ((available & features) == features)
			return format;
	}

	return VK_FORMAT_UNDEFINED;
}
}}
