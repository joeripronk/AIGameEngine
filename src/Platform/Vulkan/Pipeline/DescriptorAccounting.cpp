#include "fgpch.h"
#include "Platform/Vulkan/Pipeline/DescriptorAccounting.h"

#include <algorithm>

namespace Forge { namespace Vulkan {

namespace {

	const char* DescriptorTypeName(VkDescriptorType type)
	{
		switch (type)
		{
		case VK_DESCRIPTOR_TYPE_SAMPLER: return "SAMPLER";
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: return "COMBINED_IMAGE_SAMPLER";
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: return "SAMPLED_IMAGE";
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: return "STORAGE_IMAGE";
		case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: return "UNIFORM_TEXEL_BUFFER";
		case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: return "STORAGE_TEXEL_BUFFER";
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: return "UNIFORM_BUFFER";
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: return "STORAGE_BUFFER";
		case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: return "INPUT_ATTACHMENT";
		default: return "UNKNOWN";
		}
	}

}

DescriptorCensus CensusFromLayout(std::span<const VkDescriptorSetLayoutBinding> bindings)
{
	DescriptorCensus census{};

	for (const VkDescriptorSetLayoutBinding& binding : bindings)
	{
		auto existing = std::find_if(census.Sizes.begin(), census.Sizes.end(), [binding](const VkDescriptorPoolSize& s)
			{ return s.type == binding.descriptorType; });
		if (existing != census.Sizes.end())
			existing->descriptorCount += binding.descriptorCount;
		else
			census.Sizes.push_back(VkDescriptorPoolSize{ binding.descriptorType, binding.descriptorCount });
		census.TotalDescriptors += binding.descriptorCount;
	}

	return census;
}

void DescriptorAccounting::Init(std::span<const VkDescriptorPoolSize> poolSizes, uint32_t maxSets)
{
	m_Entries.clear();
	for (const VkDescriptorPoolSize& size : poolSizes)
		m_Entries.push_back(Entry{ size.type, size.descriptorCount, size.descriptorCount });
	m_MaxSets = maxSets;
	m_RemainingSets = maxSets;
}

uint32_t DescriptorAccounting::Remaining(VkDescriptorType type) const
{
	const auto entry = std::find_if(m_Entries.begin(), m_Entries.end(), [type](const Entry& e) { return e.Type == type; });
	return entry == m_Entries.end() ? 0 : entry->Remaining;
}

bool DescriptorAccounting::HasType(VkDescriptorType type) const
{
	return std::find_if(m_Entries.begin(), m_Entries.end(), [type](const Entry& e) { return e.Type == type; }) != m_Entries.end();
}

bool DescriptorAccounting::CanAllocate(const DescriptorCensus& census, uint32_t setCount, std::string& outReason) const
{
	if (setCount == 0)
	{
		outReason = "cannot allocate zero descriptor sets";
		return false;
	}

	if (setCount > m_RemainingSets)
	{
		outReason = std::string("sets: need ") + std::to_string(setCount) +
			", remaining " + std::to_string(m_RemainingSets) +
			" (pool max " + std::to_string(m_MaxSets) + ")";
		return false;
	}

	for (const VkDescriptorPoolSize& size : census.Sizes)
	{
		const uint32_t need = size.descriptorCount * setCount;
		const uint32_t remaining = Remaining(size.type);
		if (need > remaining)
		{
			outReason = std::string(DescriptorTypeName(size.type)) + ": need " + std::to_string(need) +
				", remaining " + std::to_string(remaining);
			return false;
		}
	}

	return true;
}

bool DescriptorAccounting::TryAllocate(const DescriptorCensus& census, uint32_t setCount, std::string& outReason)
{
	if (!CanAllocate(census, setCount, outReason))
		return false;

	m_RemainingSets -= setCount;
	for (const VkDescriptorPoolSize& size : census.Sizes)
	{
		for (Entry& entry : m_Entries)
		{
			if (entry.Type == size.type)
			{
				entry.Remaining -= size.descriptorCount * setCount;
				break;
			}
		}
	}

	return true;
}

bool DescriptorAccounting::TryFree(const DescriptorCensus& census, uint32_t setCount, std::string& outReason)
{
	if (setCount == 0)
	{
		outReason = "cannot free zero descriptor sets";
		return false;
	}

	// Credit-exact: never return more than the pool was created with.
	for (const VkDescriptorPoolSize& size : census.Sizes)
	{
		const uint32_t count = size.descriptorCount * setCount;
		const uint32_t remaining = Remaining(size.type);
		const uint32_t total = [&]() -> uint32_t
		{
			for (const Entry& entry : m_Entries)
				if (entry.Type == size.type)
					return entry.Total;
			return 0;
		}();

		if (remaining + count > total)
		{
			outReason = std::string(DescriptorTypeName(size.type)) + ": freeing " + std::to_string(count) +
				" would exceed the pool's " + std::to_string(total) + " (in use " + std::to_string(total - remaining) + ")";
			return false;
		}
	}

	if (m_RemainingSets + setCount > m_MaxSets)
	{
		outReason = "freeing " + std::to_string(setCount) + " sets would exceed the pool's max " + std::to_string(m_MaxSets);
		return false;
	}

	m_RemainingSets += setCount;
	for (const VkDescriptorPoolSize& size : census.Sizes)
	{
		for (Entry& entry : m_Entries)
		{
			if (entry.Type == size.type)
			{
				entry.Remaining += size.descriptorCount * setCount;
				break;
			}
		}
	}

	return true;
}

}}
