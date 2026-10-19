#include "fgpch.h"
#include "Platform/Vulkan/Resources/ImageView.h"

#include "Platform/Vulkan/Device/VkDevice.h"
#include "Platform/Vulkan/Resources/Image.h"

namespace Forge { namespace Vulkan {

ImageView::~ImageView()
{
	Destroy();
}

bool ImageView::Create(const Device& device, const Image& image, ::VkImageView& outHandle)
{
	if (m_Handle != VK_NULL_HANDLE)
		Destroy();

	if (!image.CreateView(device, outHandle))
		return false;

	m_Device = &device;
	m_Handle = outHandle;
	return true;
}

void ImageView::Destroy()
{
	if (m_Handle != VK_NULL_HANDLE && m_Device)
		m_Device->GetFunctions().DestroyImageView(m_Device->Handle(), m_Handle, nullptr);
	m_Handle = VK_NULL_HANDLE;
	m_Device = nullptr;
}

}}
