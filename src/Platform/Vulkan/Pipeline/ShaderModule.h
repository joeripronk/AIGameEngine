#pragma once

#include "Forge/Core/Base.h"

#include <vulkan/vulkan.h>

#include <span>
#include <string>
#include <vector>

namespace Forge { namespace Vulkan {

class Device;

// RAII shader module from SPIR-V bytes (replaces VulkanShader's nvrhi
// path). The resources/shaders/*.spv contract - and the StartupIntegrityTests
// that police it - is unchanged; this is the loader, not the pipeline.
class ShaderModule final
{
public:
	ShaderModule() = default;
	~ShaderModule();

	ShaderModule(const ShaderModule&) = delete;
	ShaderModule& operator=(const ShaderModule&) = delete;

	[[nodiscard]] bool FromSpirv(const Device& device, std::span<const uint32_t> spirv, const char* name);
	void Destroy();

	[[nodiscard]] ::VkShaderModule Handle() const { return m_Handle; }
	[[nodiscard]] bool IsNull() const { return m_Handle == VK_NULL_HANDLE; }

private:
	const Device* m_Device = nullptr;
	::VkShaderModule m_Handle = VK_NULL_HANDLE;
};

}}
