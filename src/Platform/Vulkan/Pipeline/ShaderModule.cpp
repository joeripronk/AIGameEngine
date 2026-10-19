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
	// The span is a word stream (4 bytes per element), so the byte size the
	// driver receives is always a multiple of 4. What identifies SPIR-V is
	// the magic word - the same check the startup integrity tests make on
	// the files on disk.
	if (spirv[0] != 0x07230203u)
	{
		FG_CORE_ERROR("ShaderModule: '{0}' is not a SPIR-V module (magic {1:#08x})",
			name ? name : "(unnamed)", spirv[0]);
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
