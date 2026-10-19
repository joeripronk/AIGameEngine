#include "fgpch.h"
#include "Platform/Vulkan/Pipeline/ShaderModule.h"

#include "Platform/Vulkan/Device/DebugName.h"
#include "Platform/Vulkan/Device/VkDevice.h"
#include "Forge/Core/Log.h"

namespace Forge { namespace Vulkan {

ShaderModule::~ShaderModule()
{
	Destroy();
}

bool ShaderModule::FromSpirv(const Device& device, std::span<const uint32_t> spirv, const char* name)
{
	if (m_Handle != VK_NULL_HANDLE)
		Destroy();

	if (spirv.empty())
	{
		FG_CORE_ERROR("ShaderModule: empty SPIR-V module ('{0}')", name ? name : "(unnamed)");
		return false;
	}
	if ((spirv.size() % 4) != 0)
	{
		// SPIR-V words are 4 bytes; the entry point takes a word count. A
		// length that is not a multiple of 4 is a truncated file, not a
		// shader.
		FG_CORE_ERROR("ShaderModule: SPIR-V length {0} is not a multiple of 4 ('{1}')", spirv.size(), name ? name : "(unnamed)");
		return false;
	}

	VkShaderModuleCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	info.codeSize = spirv.size() * sizeof(uint32_t);
	info.pCode = spirv.data();

	if (device.GetFunctions().CreateShaderModule(device.Handle(), &info, nullptr, &m_Handle) != VK_SUCCESS)
	{
		FG_CORE_ERROR("ShaderModule: vkCreateShaderModule failed ('{0}')", name ? name : "(unnamed)");
		return false;
	}
	m_Device = &device;

#if defined(FG_DEBUG)
	if (name && *name)
		SetDebugName(device, VK_OBJECT_TYPE_SHADER_MODULE, reinterpret_cast<uint64_t>(m_Handle), name);
#endif

	return true;
}

void ShaderModule::Destroy()
{
	if (m_Handle != VK_NULL_HANDLE && m_Device)
		m_Device->GetFunctions().DestroyShaderModule(m_Device->Handle(), m_Handle, nullptr);
	m_Handle = VK_NULL_HANDLE;
	m_Device = nullptr;
}

}}
