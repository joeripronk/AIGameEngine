#include "fgpch.h"
#include "Platform/Vulkan/Pipeline/DescriptorPool.h"

#include "Platform/Vulkan/Device/DebugName.h"
#include "Platform/Vulkan/Device/VkDevice.h"
#include "Forge/Core/Log.h"

namespace Forge { namespace Vulkan {

DescriptorSetLayout::~DescriptorSetLayout()
{
	Destroy();
}

bool DescriptorSetLayout::Create(const Device& device, std::span<const VkDescriptorSetLayoutBinding> bindings, const char* name)
{
	if (m_Handle != VK_NULL_HANDLE)
		Destroy();

	if (bindings.empty())
	{
		FG_CORE_ERROR("DescriptorSetLayout: empty layout ('{0}')", name ? name : "(unnamed)");
		return false;
	}

	VkDescriptorSetLayoutCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	info.bindingCount = static_cast<uint32_t>(bindings.size());
	info.pBindings = bindings.data();

	if (device.GetFunctions().CreateDescriptorSetLayout(device.Handle(), &info, nullptr, &m_Handle) != VK_SUCCESS)
	{
		FG_CORE_ERROR("DescriptorSetLayout: vkCreateDescriptorSetLayout failed ('{0}')", name ? name : "(unnamed)");
		return false;
	}

	m_Device = &device;
	m_Census = CensusFromLayout(bindings);
	m_Bindings.assign(bindings.begin(), bindings.end());
	std::sort(m_Bindings.begin(), m_Bindings.end(),
		[](const VkDescriptorSetLayoutBinding& a, const VkDescriptorSetLayoutBinding& b)
		{ return a.binding < b.binding; });

#if defined(FG_DEBUG)
	if (name && *name)
		SetDebugName(device, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, reinterpret_cast<uint64_t>(m_Handle), name);
#endif

	return true;
}

void DescriptorSetLayout::Destroy()
{
	if (m_Handle != VK_NULL_HANDLE && m_Device)
		m_Device->GetFunctions().DestroyDescriptorSetLayout(m_Device->Handle(), m_Handle, nullptr);
	m_Handle = VK_NULL_HANDLE;
	m_Device = nullptr;
	m_Census = DescriptorCensus{};
}

DescriptorPool::~DescriptorPool()
{
	Destroy();
}

bool DescriptorPool::Create(const Device& device, std::span<const VkDescriptorPoolSize> poolSizes, uint32_t maxSets, VkDescriptorPoolCreateFlags flags)
{
	if (m_Handle != VK_NULL_HANDLE)
		Destroy();

	if (maxSets == 0 || poolSizes.empty())
	{
		FG_CORE_ERROR("DescriptorPool: Create with maxSets {0} and {1} pool size(s)", maxSets, poolSizes.size());
		return false;
	}

	VkDescriptorPoolCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	info.flags = flags;
	info.maxSets = maxSets;
	info.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	info.pPoolSizes = poolSizes.data();

	if (device.GetFunctions().CreateDescriptorPool(device.Handle(), &info, nullptr, &m_Handle) != VK_SUCCESS)
	{
		FG_CORE_ERROR("DescriptorPool: vkCreateDescriptorPool failed (maxSets {0})", maxSets);
		return false;
	}

	m_Device = &device;
	m_Accounting.Init(poolSizes, maxSets);
	m_CreationAccounting = m_Accounting;  // Reset() restores these totals
	m_SetLayouts.clear();
	return true;
}

void DescriptorPool::Destroy()
{
	if (m_Handle != VK_NULL_HANDLE && m_Device)
		m_Device->GetFunctions().DestroyDescriptorPool(m_Device->Handle(), m_Handle, nullptr);
	m_Handle = VK_NULL_HANDLE;
	m_Device = nullptr;
	m_Accounting = DescriptorAccounting{};
	m_SetLayouts.clear();
}

bool DescriptorPool::Reset()
{
	if (m_Handle == VK_NULL_HANDLE || !m_Device)
		return false;

	if (m_Device->GetFunctions().ResetDescriptorPool(m_Device->Handle(), m_Handle, 0) != VK_SUCCESS)
	{
		FG_CORE_ERROR("DescriptorPool: vkResetDescriptorPool failed");
		return false;
	}

	// Every set the driver dropped is back as credit; the bookkeeping
	// restarts at the creation totals, and the set->layout map is empty.
	m_SetLayouts.clear();
	m_Accounting = m_CreationAccounting;
	return true;
}

bool DescriptorPool::AllocateSets(const DescriptorSetLayout& layout, uint32_t setCount, std::vector<::VkDescriptorSet>& outSets, std::string& outReason)
{
	outSets.clear();
	if (m_Handle == VK_NULL_HANDLE || !m_Device)
	{
		outReason = "descriptor pool does not exist";
		return false;
	}
	if (layout.IsNull())
	{
		outReason = "descriptor set layout does not exist";
		return false;
	}

	// Validate against the accounting first; the driver's failure carries
	// no numbers, and "OUT_OF_POOL_MEMORY" is not a diagnosis.
	if (!m_Accounting.TryAllocate(layout.GetCensus(), setCount, outReason))
	{
		FG_CORE_ERROR("DescriptorPool: allocation refused - {0}", outReason);
		return false;
	}

	std::vector<::VkDescriptorSetLayout> layouts(setCount, layout.Handle());

	VkDescriptorSetAllocateInfo allocateInfo{};
	allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocateInfo.descriptorPool = m_Handle;
	allocateInfo.descriptorSetCount = setCount;
	allocateInfo.pSetLayouts = layouts.data();

	outSets.resize(setCount);
	const VkResult result = m_Device->GetFunctions().AllocateDescriptorSets(m_Device->Handle(), &allocateInfo, outSets.data());
	if (result != VK_SUCCESS)
	{
		// The driver refused what the accounting said fit; hand the credit
		// back so the books stay true to the pool.
		std::string ignored;
		(void)m_Accounting.TryFree(layout.GetCensus(), setCount, ignored);
		outSets.clear();
		outReason = "vkAllocateDescriptorSets failed (" + std::to_string((int)result) + ")";
		FG_CORE_ERROR("DescriptorPool: {0}", outReason);
		return false;
	}

	for (::VkDescriptorSet set : outSets)
		m_SetLayouts[set] = &layout;

	return true;
}

bool DescriptorPool::FreeSets(std::span<const ::VkDescriptorSet> sets, std::string& outReason)
{
	if (m_Handle == VK_NULL_HANDLE || !m_Device)
	{
		outReason = "descriptor pool does not exist";
		return false;
	}
	if (sets.empty())
	{
		outReason = "no descriptor sets to free";
		return false;
	}

	// Every set must be one this pool handed out; an untracked set means a
	// caller is freeing something they did not allocate here.
	for (::VkDescriptorSet set : sets)
	{
		if (m_SetLayouts.find(set) == m_SetLayouts.end())
		{
			outReason = "descriptor set was not allocated from this pool";
			FG_CORE_ERROR("DescriptorPool: {0}", outReason);
			return false;
		}
	}

	// Group by layout so each group's credit is exactly what it was charged.
	std::unordered_map<const DescriptorSetLayout*, std::vector<::VkDescriptorSet>> groups;
	for (::VkDescriptorSet set : sets)
		groups[m_SetLayouts[set]].push_back(set);

	for (auto& [layout, group] : groups)
	{
		if (!m_Accounting.TryFree(layout->GetCensus(), static_cast<uint32_t>(group.size()), outReason))
		{
			FG_CORE_ERROR("DescriptorPool: free refused - {0}", outReason);
			return false;
		}
	}

	if (m_Device->GetFunctions().FreeDescriptorSets(m_Device->Handle(), m_Handle, static_cast<uint32_t>(sets.size()), sets.data()) != VK_SUCCESS)
	{
		// The driver kept the sets; undo the credit we just returned so the
		// books match the pool.
		for (auto& [layout, group] : groups)
		{
			std::string ignored;
			(void)m_Accounting.TryAllocate(layout->GetCensus(), static_cast<uint32_t>(group.size()), ignored);
		}
		outReason = "vkFreeDescriptorSets failed";
		FG_CORE_ERROR("DescriptorPool: {0}", outReason);
		return false;
	}

	for (::VkDescriptorSet set : sets)
		m_SetLayouts.erase(set);

	return true;
}

}}
