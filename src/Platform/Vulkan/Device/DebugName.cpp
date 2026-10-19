#include "fgpch.h"
#include "Platform/Vulkan/Device/DebugName.h"

#include "Platform/Vulkan/Device/VkDevice.h"
#include "Forge/Core/Assert.h"

#include <algorithm>
#include <cstring>

size_t Forge::Vulkan::TruncateDebugName(std::string_view name, char* out, size_t capacity)
{
	FG_CORE_ASSERT_MSG(capacity >= 2, "debug name buffer must hold at least one byte plus a terminator");

	size_t length = std::min(name.size(), capacity - 1);
	// Do not cut a multi-byte character in half: back up over continuation
	// bytes (10xxxxxx) so the truncated prefix is still valid UTF-8.
	while (length > 0 && length < name.size() && (static_cast<unsigned char>(name[length]) & 0xC0u) == 0x80u)
		--length;

	std::memcpy(out, name.data(), length);
	out[length] = '\0';
	return length;
}

#if defined(FG_DEBUG)
void Forge::Vulkan::SetDebugName(const Device& device, VkObjectType objectType, uint64_t objectHandle, std::string_view name)
{
	if (!device.HasExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
		return;

	const DeviceFunctions& functions = device.GetFunctions();
	if (!functions.SetDebugUtilsObjectNameEXT)
		return;

	char terminated[MaxDebugNameLength];
	// The terminator is written inside the buffer; the byte count is the
	// caller's business, not this one's.
	(void)TruncateDebugName(name, terminated, MaxDebugNameLength);

	// The driver receives a raw char* and reads until NUL, so the terminator
	// is part of the string handed over.
	VkDebugUtilsObjectNameInfoEXT info{};
	info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	info.objectType = objectType;
	info.objectHandle = objectHandle;
	info.pObjectName = terminated;

	const VkResult result = functions.SetDebugUtilsObjectNameEXT(device.Handle(), &info);
	if (result != VK_SUCCESS)
		FG_CORE_WARN("SetDebugName failed for {0} ({1})", (uint32_t)objectType, (int)result);
}
#endif
