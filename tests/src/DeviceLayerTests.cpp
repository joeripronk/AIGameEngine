#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>

#include "Platform/Vulkan/Device/DeviceSelection.h"
#include "Platform/Vulkan/Device/DebugName.h"

#include <cstring>
#include <functional>
#include <string>

using namespace Forge::Vulkan;

namespace {

	std::span<const MemoryTypeDesc> Types(const MemoryTypeDesc* descs, size_t count)
	{
		return std::span<const MemoryTypeDesc>(descs, count);
	}

	std::span<const QueueFamilyDesc> Families(const QueueFamilyDesc* descs, size_t count)
	{
		return std::span<const QueueFamilyDesc>(descs, count);
	}

}

TEST_CASE("Memory type: required flags pick the first qualifying candidate", "[device]")
{
	const MemoryTypeDesc types[] = {
		{ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0 },
		{ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1 },
		{ VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1 },
	};

	REQUIRE(Forge::Vulkan::FindMemoryType(0xFFu, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, Types(types, 3)) == 2);
	REQUIRE(Forge::Vulkan::FindMemoryType(0xFFu, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Types(types, 3)) == 0);

	// The filter restricts the candidates: type 2 qualifies for the flags but
	// is filtered out, so no candidate is left.
	REQUIRE(Forge::Vulkan::FindMemoryType(0x03u, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, Types(types, 3)) == -1);

	// Required flags that no type carries answer -1, not a near miss.
	REQUIRE(Forge::Vulkan::FindMemoryType(0xFFu, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, Types(types, 3)) == -1);
}

TEST_CASE("Memory type: preference ranking - perfect match wins outright", "[device]")
{
	const MemoryTypeDesc types[] = {
		{ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 0 },
		{ VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1 },
		{ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1 },
	};

	// Type 0 carries every preferred flag and is found before the superset at
	// index 2 - a perfect candidate returns immediately.
	REQUIRE(Forge::Vulkan::FindMemoryType(0xFFu, 0u, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, Types(types, 3)) == 0);
}

TEST_CASE("Memory type: imperfect candidates rank by preferred-flag hits", "[device]")
{
	const MemoryTypeDesc types[] = {
		{ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0 },
		{ VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1 },
		{ VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1 },
	};

	// Preferred: host visible + coherent. Type 2 has both hits, so it wins
	// over the single-hit types even though it comes last.
	REQUIRE(Forge::Vulkan::FindMemoryType(0xFFu, 0u, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, Types(types, 3)) == 2);

	// Equal hits go to the earliest index.
	const MemoryTypeDesc ties[] = {
		{ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0 },
		{ VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1 },
	};
	REQUIRE(Forge::Vulkan::FindMemoryType(0xFFu, 0u, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, Types(ties, 2)) == 0);

	// Required is never traded away for preference: a type with all the
	// preferred flags but missing the required one must not win.
	const MemoryTypeDesc required[] = {
		{ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 0 },
		{ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1 },
		{ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1 },
	};
	REQUIRE(Forge::Vulkan::FindMemoryType(0xFFu, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, Types(required, 3)) == 1);
}

TEST_CASE("Queue family: the family with the fewest extra capabilities wins", "[device]")
{
	const QueueFamilyDesc families[] = {
		{ VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT, 1 },
		{ VK_QUEUE_GRAPHICS_BIT, 8 },
		{ VK_QUEUE_TRANSFER_BIT, 4 },
	};

	// Dedicated transfer family beats the do-everything family.
	REQUIRE(Forge::Vulkan::FindQueueFamily(VK_QUEUE_TRANSFER_BIT, Families(families, 3)) == 2);

	// Graphics: family 1 has zero extras, family 0 has one.
	REQUIRE(Forge::Vulkan::FindQueueFamily(VK_QUEUE_GRAPHICS_BIT, Families(families, 3)) == 1);

	// Exclusion removes the family that has the excluded capability.
	REQUIRE(Forge::Vulkan::FindQueueFamily(VK_QUEUE_GRAPHICS_BIT, Families(families, 3), VK_QUEUE_TRANSFER_BIT) == 1);

	// A family must carry ALL required flags.
	const QueueFamilyDesc split[] = {
		{ VK_QUEUE_COMPUTE_BIT, 1 },
		{ VK_QUEUE_TRANSFER_BIT, 1 },
	};
	REQUIRE(Forge::Vulkan::FindQueueFamily(VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT, Families(split, 2)) == -1);

	// Excluding everything leaves nothing.
	REQUIRE(Forge::Vulkan::FindQueueFamily(VK_QUEUE_GRAPHICS_BIT, Families(families, 3), VK_QUEUE_GRAPHICS_BIT) == -1);
}

TEST_CASE("Format selection: first candidate covering the features for the tiling", "[device]")
{
	const std::function<FormatFeatures(VkFormat)> table = [](VkFormat format) -> FormatFeatures
	{
		switch (format)
		{
		case VK_FORMAT_R32_SFLOAT:
			return { VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT };
		case VK_FORMAT_D24_UNORM_S8_UINT:
			// Linear-tiltable only: no optimal depth features.
			return { VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, 0 };
		case VK_FORMAT_D32_SFLOAT:
			return { 0, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT };
		default:
			return {};
		}
	};

	const VkFormat candidates[] = { VK_FORMAT_R32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT };

	REQUIRE(Forge::Vulkan::FindSupportedFormat(std::span<const VkFormat>(candidates, 3), VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT, table) == VK_FORMAT_D32_SFLOAT);

	// R32_SFLOAT is first and covers sampled; the depth formats come after.
	REQUIRE(Forge::Vulkan::FindSupportedFormat(std::span<const VkFormat>(candidates, 3), VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT, table) == VK_FORMAT_R32_SFLOAT);

	// Linear tiling: D24 qualifies, D32 does not.
	REQUIRE(Forge::Vulkan::FindSupportedFormat(std::span<const VkFormat>(candidates, 3), VK_IMAGE_TILING_LINEAR,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, table) == VK_FORMAT_D24_UNORM_S8_UINT);

	// Nothing qualifies answers UNDEFINED - not the last candidate tried.
	REQUIRE(Forge::Vulkan::FindSupportedFormat(std::span<const VkFormat>(candidates, 3), VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT, table) == VK_FORMAT_UNDEFINED);
}

TEST_CASE("Debug names: UTF-8-safe truncation inside the 255-byte budget", "[device]")
{
	char out[MaxDebugNameLength];

	// Short ASCII names pass through untouched.
	const size_t shortWrite = TruncateDebugName("HDR pass", out, MaxDebugNameLength);
	REQUIRE(shortWrite == 8);
	REQUIRE(std::strcmp(out, "HDR pass") == 0);

	// A long ASCII name is cut at capacity-1 with the terminator intact.
	const std::string longName(400, 'x');
	const size_t longWrite = TruncateDebugName(longName, out, MaxDebugNameLength);
	REQUIRE(longWrite == MaxDebugNameLength - 1);
	REQUIRE(std::strlen(out) == MaxDebugNameLength - 1);
	REQUIRE(out[MaxDebugNameLength - 1] == '\0');

	// A multi-byte character straddling the cut is dropped whole, so the
	// prefix stays valid UTF-8: 254 ASCII bytes, then the 3-byte '€' starts
	// at index 254 and the budget stops at 255 - inside the character.
	std::string euro = std::string(254, 'a');
	euro += "\xE2\x82\xAC";  // U+20AC EURO SIGN
	const size_t euroWrite = TruncateDebugName(euro, out, MaxDebugNameLength);
	REQUIRE(euroWrite == 254);
	REQUIRE(std::strlen(out) == 254);

	// A character that fits exactly at the end is kept.
	std::string fits = std::string(252, 'a');
	fits += "\xE2\x82\xAC";  // ends at index 254, inside the 255-byte budget
	const size_t fitsWrite = TruncateDebugName(fits, out, MaxDebugNameLength);
	REQUIRE(fitsWrite == 255);
}
