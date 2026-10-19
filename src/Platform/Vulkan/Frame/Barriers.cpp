#include "fgpch.h"
#include "Platform/Vulkan/Frame/Barriers.h"

#include "Platform/Vulkan/Device/DebugName.h"
#include "Platform/Vulkan/Device/VkDevice.h"

namespace Forge { namespace Vulkan {

VkImageMemoryBarrier ImageBarrierBuilder::Build(VkImage image) const
{
	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcAccessMask = SrcAccess;
	barrier.dstAccessMask = DstAccess;
	barrier.oldLayout = OldLayout;
	barrier.newLayout = NewLayout;
	barrier.srcQueueFamilyIndex = SrcQueueFamily;
	barrier.dstQueueFamilyIndex = DstQueueFamily;
	barrier.image = image;
	barrier.subresourceRange = SubresourceRange;
	return barrier;
}

VkBufferMemoryBarrier BufferBarrierBuilder::Build(VkBuffer buffer) const
{
	VkBufferMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	barrier.srcAccessMask = SrcAccess;
	barrier.dstAccessMask = DstAccess;
	barrier.srcQueueFamilyIndex = SrcQueueFamily;
	barrier.dstQueueFamilyIndex = DstQueueFamily;
	barrier.buffer = buffer;
	barrier.offset = 0;
	barrier.size = Size;
	return barrier;
}

void RecordImageBarrier(const Device& device, VkCommandBuffer commandBuffer, const ImageBarrierBuilder& builder, VkImage image)
{
	const VkImageMemoryBarrier barrier = builder.Build(image);
	device.GetFunctions().CmdPipelineBarrier(commandBuffer,
		builder.SrcStages, builder.DstStages, 0,
		0, nullptr, 0, nullptr, 1, &barrier);
}

void RecordBufferBarrier(const Device& device, VkCommandBuffer commandBuffer, const BufferBarrierBuilder& barrier, VkBuffer buffer)
{
	const VkBufferMemoryBarrier b = barrier.Build(buffer);
	device.GetFunctions().CmdPipelineBarrier(commandBuffer,
		barrier.SrcStages, barrier.DstStages, 0,
		0, nullptr, 1, &b, 0, nullptr);
}

void RecordUploadPreBarriers(const Device& device, VkCommandBuffer commandBuffer, VkBuffer stagingBuffer, VkImage image, const ImageBarrierBuilder& imageBarrier)
{
	// Host writes land in the staging buffer; the copy reads them.
	BufferBarrierBuilder bufferBarrier{};
	bufferBarrier.SrcAccess = VK_ACCESS_HOST_WRITE_BIT;
	bufferBarrier.DstAccess = VK_ACCESS_TRANSFER_READ_BIT;
	bufferBarrier.SrcStages = VK_PIPELINE_STAGE_HOST_BIT;
	bufferBarrier.DstStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
	VkBufferMemoryBarrier b = bufferBarrier.Build(stagingBuffer);
	device.GetFunctions().CmdPipelineBarrier(commandBuffer,
		bufferBarrier.SrcStages, bufferBarrier.DstStages, 0,
		0, nullptr, 1, &b, 0, nullptr);

	// A fresh image starts undefined; the copy is its first definition.
	ImageBarrierBuilder preBarrier = imageBarrier;
	preBarrier.OldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	preBarrier.NewLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	preBarrier.SrcAccess = 0;
	preBarrier.DstAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
	preBarrier.SrcStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	preBarrier.DstStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
	VkImageMemoryBarrier i = preBarrier.Build(image);
	device.GetFunctions().CmdPipelineBarrier(commandBuffer,
		preBarrier.SrcStages, preBarrier.DstStages, 0,
		0, nullptr, 0, nullptr, 1, &i);
}

void RecordUploadPostBarrier(const Device& device, VkCommandBuffer commandBuffer, VkImage image, const ImageBarrierBuilder& imageBarrier)
{
	ImageBarrierBuilder postBarrier = imageBarrier;
	postBarrier.OldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	postBarrier.NewLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	postBarrier.SrcAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
	postBarrier.DstAccess = 0;
	postBarrier.SrcStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
	// ALL_COMMANDS, not FRAGMENT_SHADER: this barrier is recorded into the
	// staging pool's command buffer, and that pool may run on a dedicated
	// transfer queue family - a family that supports no graphics stage at
	// all. A barrier's stage masks must name stages the command buffer's
	// queue family supports, and ALL_COMMANDS is the one every family
	// supports.
	//
	// The access mask is empty for the same reason: a transfer-only family
	// has no stage that could carry a shader-read access, so "make the
	// upload visible to the shader" cannot be expressed here at all. It
	// does not have to be: every upload path submits and then waits for
	// the staging fence before the frame that reads the image is
	// submitted, and the host's wait is what makes the data visible.
	postBarrier.DstStages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	VkImageMemoryBarrier i = postBarrier.Build(image);
	device.GetFunctions().CmdPipelineBarrier(commandBuffer,
		postBarrier.SrcStages, postBarrier.DstStages, 0,
		0, nullptr, 0, nullptr, 1, &i);
}

#if defined(FG_DEBUG)

CommandDebugScope::CommandDebugScope(const Device& device, VkCommandBuffer commandBuffer, std::string_view label)
	: m_Device(&device), m_CommandBuffer(commandBuffer)
{
	if (!device.HasExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
		return;

	char terminated[MaxDebugNameLength];
	(void)TruncateDebugName(label, terminated, MaxDebugNameLength);

	VkDebugUtilsLabelEXT labelInfo{};
	labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
	labelInfo.pLabelName = terminated;
	device.GetFunctions().CmdBeginDebugUtilsLabelEXT(commandBuffer, &labelInfo);
}

CommandDebugScope::~CommandDebugScope()
{
	if (m_Device && m_CommandBuffer != VK_NULL_HANDLE &&
		m_Device->HasExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
		m_Device->GetFunctions().CmdEndDebugUtilsLabelEXT(m_CommandBuffer);
}

#endif

}}
