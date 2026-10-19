#pragma once

#include "Platform/Vulkan/Pipeline/DescriptorAccounting.h"

#include "Forge/Core/Base.h"

#include <vulkan/vulkan.h>

#include <unordered_map>
#include <vector>

namespace Forge { namespace Vulkan {

class Device;

// RAII descriptor set layout: the bindings, and the census they imply for
// the pool.
class DescriptorSetLayout final
{
public:
	DescriptorSetLayout() = default;
	~DescriptorSetLayout();

	DescriptorSetLayout(const DescriptorSetLayout&) = delete;
	DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;

	[[nodiscard]] bool Create(const Device& device, std::span<const VkDescriptorSetLayoutBinding> bindings, const char* name = nullptr);
	void Destroy();

	[[nodiscard]] ::VkDescriptorSetLayout Handle() const { return m_Handle; }
	[[nodiscard]] const DescriptorCensus& GetCensus() const { return m_Census; }
	// The bindings as created, sorted by binding index: the census counts
	// types, and this says which index each type lives at.
	[[nodiscard]] std::span<const VkDescriptorSetLayoutBinding> GetBindings() const { return m_Bindings; }
	[[nodiscard]] bool IsNull() const { return m_Handle == VK_NULL_HANDLE; }

private:
	const Device* m_Device = nullptr;
	::VkDescriptorSetLayout m_Handle = VK_NULL_HANDLE;
	DescriptorCensus m_Census{};
	std::vector<VkDescriptorSetLayoutBinding> m_Bindings;
};

// Accounting descriptor pool (BRUTAL DescriptorPoolEx): per-type census from
// the layouts, allocation validated against the remaining budget *before*
// vkAllocateDescriptorSets is asked, failures reported with human-readable
// counts, and a set->layout map so every free returns the exact credit the
// set was charged for.
class DescriptorPool final
{
public:
	DescriptorPool() = default;
	~DescriptorPool();

	DescriptorPool(const DescriptorPool&) = delete;
	DescriptorPool& operator=(const DescriptorPool&) = delete;

	[[nodiscard]] bool Create(const Device& device, std::span<const VkDescriptorPoolSize> poolSizes, uint32_t maxSets, VkDescriptorPoolCreateFlags flags = 0);
	void Destroy();

	[[nodiscard]] ::VkDescriptorPool Handle() const { return m_Handle; }
	[[nodiscard]] const DescriptorAccounting& GetAccounting() const { return m_Accounting; }

	// Reset the whole pool (the per-frame pattern: every set of the frame is
	// allocated fresh, the pool resets when the frame's fence returns). The
	// accounting is restored to its creation totals.
	[[nodiscard]] bool Reset();

	// Allocate `setCount` sets for a layout. On failure, outReason explains
	// what ran out with the numbers, and nothing is allocated.
	[[nodiscard]] bool AllocateSets(const DescriptorSetLayout& layout, uint32_t setCount, std::vector<::VkDescriptorSet>& outSets, std::string& outReason);

	// Credit-exact free: each set is charged back at its own layout's cost.
	[[nodiscard]] bool FreeSets(std::span<const ::VkDescriptorSet> sets, std::string& outReason);

private:
	const Device* m_Device = nullptr;
	::VkDescriptorPool m_Handle = VK_NULL_HANDLE;
	DescriptorAccounting m_Accounting{};
	DescriptorAccounting m_CreationAccounting{};   // totals, restored by Reset
	std::unordered_map<void*, const DescriptorSetLayout*> m_SetLayouts;
};

}}
