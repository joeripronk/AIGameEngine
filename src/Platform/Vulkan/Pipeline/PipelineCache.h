#pragma once

#include "Forge/Core/Base.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Forge { namespace Vulkan {

	// The cross-session pipeline cache file: magic, version, the 16-byte
	// pipelineCacheUuid of the device that produced the payload, then the
	// driver's opaque payload. The UUID is not decoration - a payload from
	// one driver is meaningless to another, and handing it one is what the
	// file format exists to make impossible.
	namespace PipelineCacheFile {

		inline constexpr std::array<uint8_t, 8> kMagic{ 'F', 'G', 'V', 'K', 'P', 'C', '1', '\n' };
		inline constexpr uint32_t kVersion = 1;

		// Pure builders/parsers so the format is unit-tested with no device.
		[[nodiscard]] std::vector<uint8_t> Build(std::span<const uint8_t, 16> deviceUuid,
			std::span<const uint8_t> payload);

		// A file that is not a cache, is a version this build cannot read,
		// carries the payload of a foreign device, or is truncated reports
		// false with a reason and writes nothing.
		[[nodiscard]] bool Parse(std::span<const uint8_t> file, std::span<const uint8_t, 16> deviceUuid,
			std::vector<uint8_t>& outPayload, std::string& outReason);

		// The device name as a file name: every character that is not
		// alphanumeric, '-' or '_' becomes '_', so "Intel(R) Iris(R) Xe
		// Graphics (ADL GT2)" lands on one predictable path.
		[[nodiscard]] std::string SanitizeDeviceName(std::string_view deviceName);

		// The cache file for a device: pipelines-<name>.vkcache. The payload
		// is the driver's own format and the driver validates it by
		// pipelineCacheUuid, so one name per device keeps a dual-GPU machine
		// from handing each driver the other's data.
		[[nodiscard]] std::filesystem::path CachePathFor(std::string_view deviceName);

		// The 16-byte driver identity a payload is bound to.
		[[nodiscard]] std::span<const uint8_t, 16> UuidOf(const VkPhysicalDeviceProperties& properties);
	}

	// RAII VkPipelineCache (BRUTAL Presets/PipelineCache pattern): created
	// with the parsed payload, queried by every pipeline creation, dumped
	// back to the file at shutdown.
	class Device;
	class DeviceFunctions;

	class PipelineCache
	{
	public:
		PipelineCache() = default;
		~PipelineCache();

		PipelineCache(const PipelineCache&) = delete;
		PipelineCache& operator=(const PipelineCache&) = delete;

		// initialPayload may be empty (first run, corrupt file, foreign
		// device): an empty cache is a legal cache.
		bool Create(const Device& device, std::span<const uint8_t> initialPayload);

		// The driver's current cache contents. Empty when the cache does not
		// exist - "no data", not "the cache is broken".
		[[nodiscard]] std::vector<uint8_t> GetData() const;

		void Destroy();

		[[nodiscard]] ::VkPipelineCache Handle() const { return m_Handle; }
		[[nodiscard]] bool IsNull() const { return m_Handle == VK_NULL_HANDLE; }

	private:
		const Device* m_Device = nullptr;
		::VkPipelineCache m_Handle = VK_NULL_HANDLE;
	};

}}
