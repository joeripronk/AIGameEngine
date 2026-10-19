#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Forge { namespace Vulkan {

// The accounting behind the descriptor pool (BRUTAL DescriptorPoolEx): a
// per-type census from the layouts, allocation validated against the
// remaining budget before vkAllocateDescriptorSets is asked for sets it
// cannot give, out-of-sets / out-of-descriptors reported with the actual
// numbers, and credit-exact free. The accounting is pure arithmetic - the
// diagnostics that used to be "VK_ERROR_OUT_OF_POOL_MEMORY" are testable
// text here.

// What one set of a given layout costs: descriptor counts per type.
struct DescriptorCensus
{
	std::vector<VkDescriptorPoolSize> Sizes;  // one entry per type, counts summed
	uint32_t TotalDescriptors = 0;
};

[[nodiscard]] DescriptorCensus CensusFromLayout(std::span<const VkDescriptorSetLayoutBinding> bindings);

class DescriptorAccounting final
{
public:
	DescriptorAccounting() = default;

	void Init(std::span<const VkDescriptorPoolSize> poolSizes, uint32_t maxSets);

	[[nodiscard]] uint32_t RemainingSets() const { return m_RemainingSets; }
	[[nodiscard]] uint32_t Remaining(VkDescriptorType type) const;
	[[nodiscard]] bool HasType(VkDescriptorType type) const;

	// Validation without mutation: the reason names the exhausted resource
	// with the numbers ("SAMPLED_IMAGE: need 4, remaining 2"), so the
	// caller's log line is the diagnosis, not a code.
	[[nodiscard]] bool CanAllocate(const DescriptorCensus& census, uint32_t setCount, std::string& outReason) const;

	// Reserve the credit; false leaves the accounting untouched.
	[[nodiscard]] bool TryAllocate(const DescriptorCensus& census, uint32_t setCount, std::string& outReason);

	// Credit-exact return: freeing more than was allocated reports false
	// rather than inflating the pool beyond what it was created with.
	[[nodiscard]] bool TryFree(const DescriptorCensus& census, uint32_t setCount, std::string& outReason);

private:
	struct Entry
	{
		VkDescriptorType Type;
		uint32_t Total;
		uint32_t Remaining;
	};

	std::vector<Entry> m_Entries;
	uint32_t m_MaxSets = 0;
	uint32_t m_RemainingSets = 0;
};

}}
