#include "fgpch.h"
#include "Platform/Vulkan/Pipeline/PipelineCache.h"

#include "Platform/Vulkan/Device/DebugName.h"
#include "Platform/Vulkan/Device/VkDevice.h"
#include "Forge/Core/Log.h"

#include <cctype>
#include <cstring>

namespace Forge { namespace Vulkan {

namespace {

	constexpr size_t kHeaderSize = 8 + 4 + 16;  // magic + version + uuid

	[[nodiscard]] uint32_t ReadU32(const uint8_t* bytes)
	{
		uint32_t value = 0;
		std::memcpy(&value, bytes, sizeof(value));
		return value;
	}

}  // namespace

namespace PipelineCacheFile {

	std::vector<uint8_t> Build(std::span<const uint8_t, 16> deviceUuid, std::span<const uint8_t> payload)
	{
		std::vector<uint8_t> file;
		file.reserve(kHeaderSize + payload.size());
		file.insert(file.end(), kMagic.begin(), kMagic.end());
		const uint32_t version = kVersion;
		const uint8_t* versionBytes = reinterpret_cast<const uint8_t*>(&version);
		file.insert(file.end(), versionBytes, versionBytes + sizeof(version));
		file.insert(file.end(), deviceUuid.begin(), deviceUuid.end());
		file.insert(file.end(), payload.begin(), payload.end());
		return file;
	}

	bool Parse(std::span<const uint8_t> file, std::span<const uint8_t, 16> deviceUuid,
		std::vector<uint8_t>& outPayload, std::string& outReason)
	{
		outPayload.clear();
		if (file.size() < kHeaderSize)
		{
			outReason = "file is shorter than the header";
			return false;
		}
		if (!std::equal(kMagic.begin(), kMagic.end(), file.begin()))
		{
			outReason = "not a Forge pipeline cache file";
			return false;
		}
		const uint32_t version = ReadU32(file.data() + kMagic.size());
		if (version != kVersion)
		{
			outReason = "cache version " + std::to_string(version) + " is not readable by version " + std::to_string(kVersion);
			return false;
		}
		const uint8_t* fileUuid = file.data() + kMagic.size() + 4;
		if (std::memcmp(fileUuid, deviceUuid.data(), deviceUuid.size()) != 0)
		{
			outReason = "cache was written by a different device (driver validates by pipelineCacheUuid)";
			return false;
		}
		outPayload.assign(file.begin() + static_cast<std::ptrdiff_t>(kHeaderSize), file.end());
		return true;
	}

	std::string SanitizeDeviceName(std::string_view deviceName)
	{
		std::string sanitized;
		for (const char c : deviceName.empty() ? std::string_view("device") : deviceName)
			sanitized += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
		return sanitized;
	}

	std::filesystem::path CachePathFor(std::string_view deviceName)
	{
		return std::filesystem::path("pipelines-" + SanitizeDeviceName(deviceName) + ".vkcache");
	}

	std::span<const uint8_t, 16> UuidOf(const VkPhysicalDeviceProperties& properties)
	{
		return std::span<const uint8_t, 16>(properties.pipelineCacheUUID);
	}

}  // namespace PipelineCacheFile

PipelineCache::~PipelineCache()
{
	Destroy();
}

bool PipelineCache::Create(const Device& device, std::span<const uint8_t> initialPayload)
{
	Destroy();

	VkPipelineCacheCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	info.initialDataSize = initialPayload.size();
	info.pInitialData = initialPayload.empty() ? nullptr : initialPayload.data();

	if (device.GetFunctions().CreatePipelineCache(device.Handle(), &info, nullptr, &m_Handle) != VK_SUCCESS)
	{
		FG_CORE_ERROR("PipelineCache: vkCreatePipelineCache failed");
		m_Handle = VK_NULL_HANDLE;
		return false;
	}

	m_Device = &device;

#if defined(FG_DEBUG)
	SetDebugName(device, VK_OBJECT_TYPE_PIPELINE_CACHE, reinterpret_cast<uint64_t>(m_Handle), "PipelineCache");
#endif
	return true;
}

std::vector<uint8_t> PipelineCache::GetData() const
{
	if (m_Handle == VK_NULL_HANDLE || !m_Device)
		return {};

	const DeviceFunctions& functions = m_Device->GetFunctions();
	size_t size = 0;
	if (functions.GetPipelineCacheData(m_Device->Handle(), m_Handle, &size, nullptr) != VK_SUCCESS)
	{
		FG_CORE_WARN("PipelineCache: querying the cache size failed");
		return {};
	}
	if (size == 0)
		return {};

	std::vector<uint8_t> data(size);
	if (functions.GetPipelineCacheData(m_Device->Handle(), m_Handle, &size, data.data()) != VK_SUCCESS)
	{
		FG_CORE_WARN("PipelineCache: querying the cache data failed");
		return {};
	}
	data.resize(size);
	return data;
}

void PipelineCache::Destroy()
{
	if (m_Handle != VK_NULL_HANDLE && m_Device)
		m_Device->GetFunctions().DestroyPipelineCache(m_Device->Handle(), m_Handle, nullptr);
	m_Handle = VK_NULL_HANDLE;
	m_Device = nullptr;
}

}}
