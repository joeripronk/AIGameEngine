#pragma once

#include <vulkan/vulkan.h>

namespace Forge { namespace Vulkan {

class Device;
class Image;

// RAII image view (BRUTAL ImageViewEx): the view created from an image,
// destroyed through the device that created it.
class ImageView final
{
public:
	ImageView() = default;
	~ImageView();

	ImageView(const ImageView&) = delete;
	ImageView& operator=(const ImageView&) = delete;

	[[nodiscard]] bool Create(const Device& device, const Image& image, ::VkImageView& outHandle);
	void Destroy();

	[[nodiscard]] ::VkImageView Handle() const { return m_Handle; }
	[[nodiscard]] bool IsNull() const { return m_Handle == VK_NULL_HANDLE; }

private:
	const Device* m_Device = nullptr;
	::VkImageView m_Handle = VK_NULL_HANDLE;
};

}}
