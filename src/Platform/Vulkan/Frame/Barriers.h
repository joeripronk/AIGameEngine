#pragma once

#include <vulkan/vulkan.h>

#include <string_view>

namespace Forge { namespace Vulkan {

class Device;

// Barrier builders with queue-family-ignore defaults (BRUTAL
// VulkanBackend's upload triple and CommandBufferExtensions' RAII debug
// scopes). The upload triple - HostWrite→TransferRead on the staging
// buffer, Undefined→TransferDst on the image, TransferDst→ShaderReadOnly
// after the copy - is the exact sequence a staged upload needs; getting
// the source state of a fresh image "close enough" (say, to
// Undefined→TransferDst twice) is the classic undefined-content upload.

struct ImageBarrierBuilder
{
	VkImageSubresourceRange SubresourceRange{};
	VkImageLayout OldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImageLayout NewLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	VkAccessFlags SrcAccess = 0;
	VkAccessFlags DstAccess = 0;
	VkPipelineStageFlags SrcStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	VkPipelineStageFlags DstStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
	uint32_t SrcQueueFamily = VK_QUEUE_FAMILY_IGNORED;
	uint32_t DstQueueFamily = VK_QUEUE_FAMILY_IGNORED;

	[[nodiscard]] VkImageMemoryBarrier Build(VkImage image) const;
};

struct BufferBarrierBuilder
{
	size_t Size = VK_WHOLE_SIZE;
	VkAccessFlags SrcAccess = 0;
	VkAccessFlags DstAccess = 0;
	VkPipelineStageFlags SrcStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	VkPipelineStageFlags DstStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
	uint32_t SrcQueueFamily = VK_QUEUE_FAMILY_IGNORED;
	uint32_t DstQueueFamily = VK_QUEUE_FAMILY_IGNORED;

	[[nodiscard]] VkBufferMemoryBarrier Build(VkBuffer buffer) const;
};

void RecordImageBarrier(const Device& device, VkCommandBuffer commandBuffer, const ImageBarrierBuilder& barrier, VkImage image);
void RecordBufferBarrier(const Device& device, VkCommandBuffer commandBuffer, const BufferBarrierBuilder& barrier, VkBuffer buffer);

// The staged-upload triple, split around the copy: call before
// CmdCopyBufferToImage and after it.
void RecordUploadPreBarriers(const Device& device, VkCommandBuffer commandBuffer, VkBuffer stagingBuffer, VkImage image, const ImageBarrierBuilder& imageBarrier);
void RecordUploadPostBarrier(const Device& device, VkCommandBuffer commandBuffer, VkImage image, const ImageBarrierBuilder& imageBarrier);

// RAII command-buffer debug scope (BRUTAL DebugScope("Shadow pass")):
// compiles out of shipping builds.
class CommandDebugScope final
{
public:
#if defined(FG_DEBUG)
	CommandDebugScope(const Device& device, VkCommandBuffer commandBuffer, std::string_view label);
	~CommandDebugScope();
	CommandDebugScope(const CommandDebugScope&) = delete;
	CommandDebugScope& operator=(const CommandDebugScope&) = delete;
private:
	const Device* m_Device = nullptr;
	VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
#endif
};

}}
