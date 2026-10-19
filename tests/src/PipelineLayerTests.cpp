#include <catch2/catch_test_macros.hpp>

#include "Platform/Vulkan/Device/DebugName.h"
#include "Platform/Vulkan/Memory/UploadRegions.h"
#include "Platform/Vulkan/Pipeline/DescriptorAccounting.h"
#include "Platform/Vulkan/Pipeline/PipelineCache.h"
#include "Platform/Vulkan/Pipeline/PipelinePresets.h"
#include "Platform/Vulkan/Pipeline/VertexLayout.h"
#include "Platform/Vulkan/Resources/Subresources.h"

#include "Forge/Renderer/Mesh.h"

#include <array>

using namespace Forge::Vulkan;

// The engine's real vertex structs - the layouts must match these byte for
// byte, which is the whole point of generating them from the struct.
namespace {

	// The shadow pass vertex: position only.
	struct ShadowVertex
	{
		glm::vec3 Position;
	};

	// The fullscreen pass vertex.
	struct FullscreenVertex
	{
		glm::vec2 Position;
		glm::vec2 UV;
	};

}

TEST_CASE("Vertex layout: the engine's PBR vertex generates its GPU layout", "[pipeline]")
{
	const VertexFieldList fields = MakeFieldList(FieldFloat3{}, FieldFloat3{}, FieldFloat2{}, FieldFloat3{}, FieldFloat3{});
	REQUIRE(fields.Count == 5);

	REQUIRE(VertexLayout::Validate(fields, sizeof(Forge::Vertex)) == VertexLayoutError::Ok);

	const VertexLayout layout = VertexLayout::FromFields(fields, sizeof(Forge::Vertex));

	REQUIRE(layout.Stride == sizeof(Forge::Vertex));
	REQUIRE(sizeof(Forge::Vertex) == 56);
	REQUIRE(layout.AttributeCount == 5);

	REQUIRE(layout.Binding.stride == sizeof(Forge::Vertex));
	REQUIRE(layout.Binding.inputRate == VK_VERTEX_INPUT_RATE_VERTEX);

	const std::array<VkFormat, 5> expectedFormats{
		VK_FORMAT_R32G32B32_SFLOAT,  // Position
		VK_FORMAT_R32G32B32_SFLOAT,  // Normal
		VK_FORMAT_R32G32_SFLOAT,     // UV
		VK_FORMAT_R32G32B32_SFLOAT,  // Tangent
		VK_FORMAT_R32G32B32_SFLOAT,  // Bitangent
	};
	const std::array<uint32_t, 5> expectedOffsets{ 0, 12, 24, 32, 44 };

	for (uint32_t i = 0; i < 5; ++i)
	{
		REQUIRE(layout.Attributes[i].location == i);
		REQUIRE(layout.Attributes[i].binding == 0);
		REQUIRE(layout.Attributes[i].format == expectedFormats[i]);
		REQUIRE(layout.Attributes[i].offset == expectedOffsets[i]);
	}
}

TEST_CASE("Vertex layout: shadow and fullscreen vertices", "[pipeline]")
{
	const VertexFieldList shadowFields = MakeFieldList(FieldFloat3{});
	REQUIRE(VertexLayout::Validate(shadowFields, sizeof(ShadowVertex)) == VertexLayoutError::Ok);
	const VertexLayout shadow = VertexLayout::FromFields(shadowFields, sizeof(ShadowVertex));
	REQUIRE(shadow.Stride == 12);
	REQUIRE(shadow.AttributeCount == 1);
	REQUIRE(shadow.Attributes[0].format == VK_FORMAT_R32G32B32_SFLOAT);

	const VertexFieldList quadFields = MakeFieldList(FieldFloat2{}, FieldFloat2{});
	REQUIRE(VertexLayout::Validate(quadFields, sizeof(FullscreenVertex)) == VertexLayoutError::Ok);
	const VertexLayout quad = VertexLayout::FromFields(quadFields, sizeof(FullscreenVertex));
	REQUIRE(quad.Stride == 16);
	REQUIRE(quad.AttributeCount == 2);
	REQUIRE(quad.Attributes[1].offset == 8);
	REQUIRE(quad.Attributes[1].format == VK_FORMAT_R32G32_SFLOAT);
}

TEST_CASE("Vertex layout: validation rejects lists that do not tile the struct", "[pipeline]")
{
	// A field list shorter than the struct: a gap is a vertex attribute the
	// shader reads as garbage.
	const VertexFieldList shortList = MakeFieldList(FieldFloat3{}, FieldFloat3{});
	REQUIRE(VertexLayout::Validate(shortList, sizeof(Forge::Vertex)) == VertexLayoutError::FieldSizeMismatch);

	// A list longer than the struct: an attribute past the end.
	const VertexFieldList longList = MakeFieldList(FieldFloat4{}, FieldFloat4{}, FieldFloat4{}, FieldFloat4{}, FieldFloat4{});
	REQUIRE(VertexLayout::Validate(longList, sizeof(Forge::Vertex)) == VertexLayoutError::FieldSizeMismatch);

	// An empty list is not a vertex layout.
	const VertexFieldList empty{};
	REQUIRE(VertexLayout::Validate(empty, 0) == VertexLayoutError::Empty);

	// A validated list builds; an invalid one answers an empty layout and
	// logs, it does not half-build.
	const VertexLayout broken = VertexLayout::FromFields(shortList, sizeof(Forge::Vertex));
	REQUIRE(broken.AttributeCount == 0);
	REQUIRE(broken.Stride == 0);

	// Normalized byte4 maps to the normalized format, not a raw uint.
	const VertexFieldList color = MakeFieldList(FieldFloat3{}, FieldByte4Normalized{});
	const VertexLayout colorLayout = VertexLayout::FromFields(color, 12 + 4);
	REQUIRE(colorLayout.AttributeCount == 2);
	REQUIRE(colorLayout.Attributes[1].format == VK_FORMAT_R8G8B8A8_UNORM);
	REQUIRE(colorLayout.Attributes[1].offset == 12);
	REQUIRE(colorLayout.Stride == 16);
}

TEST_CASE("Pipeline presets: blend blocks say exactly what they blend with", "[pipeline]")
{
	REQUIRE(Presets::BlendAttachment::None.blendEnable == VK_FALSE);
	REQUIRE(Presets::BlendAttachment::Alpha.blendEnable == VK_TRUE);
	REQUIRE(Presets::BlendAttachment::Alpha.srcColorBlendFactor == VK_BLEND_FACTOR_SRC_ALPHA);
	REQUIRE(Presets::BlendAttachment::Alpha.dstColorBlendFactor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);

	// Additive: src + dst*srcA.
	REQUIRE(Presets::BlendAttachment::Additive.srcColorBlendFactor == VK_BLEND_FACTOR_ONE);
	REQUIRE(Presets::BlendAttachment::Additive.dstColorBlendFactor == VK_BLEND_FACTOR_DST_ALPHA);

	// Soft additive: src + dst*(1-src).
	REQUIRE(Presets::BlendAttachment::SoftAdditive.dstColorBlendFactor == VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR);

	// Multiplicative: src*dst + 0.
	REQUIRE(Presets::BlendAttachment::Multiplicative.srcColorBlendFactor == VK_BLEND_FACTOR_DST_COLOR);
	REQUIRE(Presets::BlendAttachment::Multiplicative.dstColorBlendFactor == VK_BLEND_FACTOR_ZERO);

	// Every preset writes all components; a pipeline that quietly drops the
	// alpha channel starts here.
	REQUIRE(Presets::BlendAttachment::None.colorWriteMask ==
		(VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT));

	// The state blocks point at their attachment block, exactly one.
	REQUIRE(Presets::Blend::Alpha.pAttachments == &Presets::BlendAttachment::Alpha);
	REQUIRE(Presets::Blend::Alpha.attachmentCount == 1);
	REQUIRE(Presets::Blend::Alpha.logicOpEnable == VK_FALSE);
}

TEST_CASE("Pipeline presets: depth, rasterizer, sampler blocks", "[pipeline]")
{
	// Shadow/G-buffer: nearest wins, depth written.
	REQUIRE(Presets::DepthStencil::Opaque.depthTestEnable == VK_TRUE);
	REQUIRE(Presets::DepthStencil::Opaque.depthWriteEnable == VK_TRUE);
	REQUIRE(Presets::DepthStencil::Opaque.depthCompareOp == VK_COMPARE_OP_LESS);

	// Fullscreen passes have no depth concept.
	REQUIRE(Presets::DepthStencil::Disabled.depthTestEnable == VK_FALSE);
	REQUIRE(Presets::DepthStencil::Disabled.depthWriteEnable == VK_FALSE);

	// Translucent: tested, never written.
	REQUIRE(Presets::DepthStencil::TestNoWrite.depthTestEnable == VK_TRUE);
	REQUIRE(Presets::DepthStencil::TestNoWrite.depthWriteEnable == VK_FALSE);

	// Rasterizers.
	REQUIRE(Presets::Rasterizer::FillNoCull.cullMode == VK_CULL_MODE_NONE);
	REQUIRE(Presets::Rasterizer::FillCullBack.cullMode == VK_CULL_MODE_BACK_BIT);
	REQUIRE(Presets::Rasterizer::LineNoCull.polygonMode == VK_POLYGON_MODE_LINE);
	REQUIRE(Presets::Rasterizer::LineNoCull.lineWidth == 1.0f);

	// Samplers.
	REQUIRE(Presets::Sampler::LinearClampToEdge.addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
	REQUIRE(Presets::Sampler::LinearRepeat.addressModeV == VK_SAMPLER_ADDRESS_MODE_REPEAT);
	REQUIRE(Presets::Sampler::Anisotropic16.anisotropyEnable == VK_TRUE);
	REQUIRE(Presets::Sampler::Anisotropic16.maxAnisotropy == 16.0f);
	REQUIRE(Presets::Sampler::PointClampToEdge.minFilter == VK_FILTER_NEAREST);

	// No-vertex-input: the fullscreen passes.
	REQUIRE(Presets::NoVertexInput.vertexBindingDescriptionCount == 0);
	REQUIRE(Presets::NoVertexInput.pVertexAttributeDescriptions == nullptr);
}

TEST_CASE("Descriptor accounting: over-allocation reports the exhausted resource with numbers", "[pipeline]")
{
	const std::array<VkDescriptorPoolSize, 2> poolSizes{
		VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8 },
		VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4 },
	};

	DescriptorAccounting accounting;
	accounting.Init(std::span<const VkDescriptorPoolSize>(poolSizes.data(), poolSizes.size()), 3);

	REQUIRE(accounting.RemainingSets() == 3);
	REQUIRE(accounting.Remaining(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) == 8);

	// A layout: 2 sampled images + 1 uniform buffer per set.
	const std::array<VkDescriptorSetLayoutBinding, 2> bindings{
		VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		VkDescriptorSetLayoutBinding{ 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
	};
	const DescriptorCensus census = CensusFromLayout(std::span<const VkDescriptorSetLayoutBinding>(bindings.data(), bindings.size()));
	REQUIRE(census.Sizes.size() == 2);
	REQUIRE(census.TotalDescriptors == 3);

	// 2 sets fit (4 sampled, 2 uniform).
	std::string reason;
	REQUIRE(accounting.CanAllocate(census, 2, reason));
	REQUIRE(accounting.TryAllocate(census, 2, reason));
	REQUIRE(accounting.RemainingSets() == 1);
	REQUIRE(accounting.Remaining(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) == 4);
	REQUIRE(accounting.Remaining(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) == 2);

	// One more set fits; the next one runs out of SAMPLED_IMAGE before it
	// runs out of sets - and the reason says which, with the numbers.
	REQUIRE(accounting.TryAllocate(census, 1, reason));
	REQUIRE(accounting.RemainingSets() == 0);
	REQUIRE_FALSE(accounting.TryAllocate(census, 1, reason));
	REQUIRE(reason.find("sets") != std::string::npos);

	// Descriptor exhaustion is reported before set exhaustion for a layout
	// that needs more descriptors than the pool has left.
	accounting.Init(std::span<const VkDescriptorPoolSize>(poolSizes.data(), poolSizes.size()), 10);
	REQUIRE(accounting.TryAllocate(census, 4, reason));  // 8 sampled, 4 uniform: exact fit
	REQUIRE(accounting.Remaining(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) == 0);
	REQUIRE(accounting.RemainingSets() == 6);
	REQUIRE_FALSE(accounting.TryAllocate(census, 1, reason));
	REQUIRE(reason.find("SAMPLED_IMAGE") != std::string::npos);
	REQUIRE(reason.find("need 2") != std::string::npos);
	REQUIRE(reason.find("remaining 0") != std::string::npos);

	// A failed allocation leaves the books untouched.
	REQUIRE(accounting.RemainingSets() == 6);
}

TEST_CASE("Descriptor accounting: free is credit-exact in both directions", "[pipeline]")
{
	const std::array<VkDescriptorPoolSize, 2> poolSizes{
		VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8 },
		VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4 },
	};
	DescriptorAccounting accounting;
	accounting.Init(std::span<const VkDescriptorPoolSize>(poolSizes.data(), poolSizes.size()), 3);

	const std::array<VkDescriptorSetLayoutBinding, 2> bindings{
		VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		VkDescriptorSetLayoutBinding{ 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
	};
	const DescriptorCensus census = CensusFromLayout(std::span<const VkDescriptorSetLayoutBinding>(bindings.data(), bindings.size()));

	std::string reason;
	REQUIRE(accounting.TryAllocate(census, 2, reason));

	// Freeing one set returns exactly its cost.
	REQUIRE(accounting.TryFree(census, 1, reason));
	REQUIRE(accounting.Remaining(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) == 6);
	REQUIRE(accounting.Remaining(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) == 3);
	REQUIRE(accounting.RemainingSets() == 2);

	// Freeing more than is in use reports false and changes nothing.
	REQUIRE_FALSE(accounting.TryFree(census, 2, reason));
	REQUIRE(accounting.Remaining(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) == 6);
	REQUIRE(accounting.RemainingSets() == 2);
	REQUIRE(reason.find("exceed") != std::string::npos);

	// The last in-use set frees cleanly, back to exactly the totals.
	REQUIRE(accounting.TryFree(census, 1, reason));
	REQUIRE(accounting.Remaining(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) == 8);
	REQUIRE(accounting.Remaining(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) == 4);
	REQUIRE(accounting.RemainingSets() == 3);
}

TEST_CASE("Subresources: range constructors cover what they name", "[pipeline]")
{
	const VkImageSubresourceRange full = Subresources::Full(VK_IMAGE_ASPECT_COLOR_BIT, 4, 6);
	REQUIRE(full.baseMipLevel == 0);
	REQUIRE(full.levelCount == 4);
	REQUIRE(full.baseArrayLayer == 0);
	REQUIRE(full.layerCount == 6);

	const VkImageSubresourceRange single = Subresources::Single(VK_IMAGE_ASPECT_DEPTH_BIT, 2, 3);
	REQUIRE(single.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT);
	REQUIRE(single.baseMipLevel == 2);
	REQUIRE(single.levelCount == 1);
	REQUIRE(single.baseArrayLayer == 3);
	REQUIRE(single.layerCount == 1);

	const VkImageSubresourceRange mips = Subresources::Mips(VK_IMAGE_ASPECT_COLOR_BIT, 1, 3, 6, 2);
	REQUIRE(mips.baseMipLevel == 1);
	REQUIRE(mips.levelCount == 3);
	REQUIRE(mips.baseArrayLayer == 2);
	REQUIRE(mips.layerCount == 6);

	const VkImageSubresourceRange layers = Subresources::Layers(VK_IMAGE_ASPECT_COLOR_BIT, 4, 2);
	REQUIRE(layers.baseArrayLayer == 4);
	REQUIRE(layers.layerCount == 2);
	REQUIRE(layers.levelCount == 1);

	const VkImageSubresourceLayers copyLayers = Subresources::FullLayers(VK_IMAGE_ASPECT_COLOR_BIT, 3, 0, 6);
	REQUIRE(copyLayers.mipLevel == 3);
	REQUIRE(copyLayers.layerCount == 6);
}

TEST_CASE("Format aspects: depth formats resolve to depth, not color", "[pipeline]")
{
	REQUIRE(FormatAspectMask(VK_FORMAT_R8G8B8A8_UNORM) == VK_IMAGE_ASPECT_COLOR_BIT);
	REQUIRE(FormatAspectMask(VK_FORMAT_D32_SFLOAT) == VK_IMAGE_ASPECT_DEPTH_BIT);
	REQUIRE(FormatAspectMask(VK_FORMAT_D16_UNORM) == VK_IMAGE_ASPECT_DEPTH_BIT);
	REQUIRE(FormatAspectMask(VK_FORMAT_D24_UNORM_S8_UINT) == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));
	REQUIRE(FormatAspectMask(VK_FORMAT_S8_UINT) == VK_IMAGE_ASPECT_STENCIL_BIT);

	REQUIRE(FormatTexelBlockBytes(VK_FORMAT_R8G8B8A8_UNORM) == 4);
	REQUIRE(FormatTexelBlockBytes(VK_FORMAT_R32_SFLOAT) == 4);
	REQUIRE(FormatTexelBlockBytes(VK_FORMAT_R32G32B32A32_SFLOAT) == 16);
	REQUIRE(FormatTexelBlockBytes(VK_FORMAT_D24_UNORM_S8_UINT) == 8);
	REQUIRE(FormatTexelBlockBytes(VK_FORMAT_R8_UNORM) == 1);
}

TEST_CASE("Pipeline cache file: build/parse round trip with the device UUID binding", "[pipeline]")
{
	const std::array<uint8_t, 16> uuid{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
	const std::vector<uint8_t> payload{ 'd', 'r', 'i', 'v', 'e', 'r', ' ', 'b', 'l', 'o', 'b' };

	const std::vector<uint8_t> file = PipelineCacheFile::Build(uuid, payload);
	REQUIRE(file.size() == 8 + 4 + 16 + payload.size());

	std::vector<uint8_t> out;
	std::string reason;
	REQUIRE(PipelineCacheFile::Parse(file, uuid, out, reason));
	REQUIRE(out == payload);

	// An empty payload is a legal cache (first run with nothing compiled).
	const std::vector<uint8_t> emptyFile = PipelineCacheFile::Build(uuid, {});
	REQUIRE(PipelineCacheFile::Parse(emptyFile, uuid, out, reason));
	REQUIRE(out.empty());
}

TEST_CASE("Pipeline cache file: foreign device, garbage and truncation are refused", "[pipeline]")
{
	const std::array<uint8_t, 16> uuid{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
	const std::array<uint8_t, 16> other{ 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1 };
	const std::vector<uint8_t> payload{ 0xDE, 0xAD, 0xBE, 0xEF };
	const std::vector<uint8_t> file = PipelineCacheFile::Build(uuid, payload);

	// A payload from one driver is meaningless to another: the UUID binds it.
	std::vector<uint8_t> out;
	std::string reason;
	REQUIRE_FALSE(PipelineCacheFile::Parse(file, other, out, reason));
	REQUIRE(out.empty());
	REQUIRE_FALSE(reason.empty());

	// Garbage is "not a cache", not a crash.
	const std::vector<uint8_t> garbage{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 };
	REQUIRE_FALSE(PipelineCacheFile::Parse(garbage, uuid, out, reason));

	// A header cut short reports false and yields nothing. (A payload cut
	// short is undetectable by the format - the driver validates the
	// payload itself - so the parser's job is the header.)
	const std::vector<uint8_t> headerOnly = PipelineCacheFile::Build(uuid, {});
	const std::vector<uint8_t> truncated(headerOnly.begin(), headerOnly.end() - 2);
	REQUIRE_FALSE(PipelineCacheFile::Parse(truncated, uuid, out, reason));
	REQUIRE(out.empty());

	// A header-only file parses to an empty payload.
	REQUIRE(PipelineCacheFile::Parse(headerOnly, uuid, out, reason));
	REQUIRE(out.empty());
}

TEST_CASE("Pipeline cache path: device names become safe, device-specific file names", "[pipeline]")
{
	REQUIRE(PipelineCacheFile::SanitizeDeviceName("Intel(R) Iris(R) Xe Graphics (ADL GT2)")
	        == "Intel_R__Iris_R__Xe_Graphics__ADL_GT2_");
	REQUIRE(PipelineCacheFile::SanitizeDeviceName("NVIDIA GeForce RTX 3080 Ti")
	        == "NVIDIA_GeForce_RTX_3080_Ti");
	REQUIRE(PipelineCacheFile::CachePathFor("NVIDIA GeForce RTX 3080 Ti").string()
	        == "pipelines-NVIDIA_GeForce_RTX_3080_Ti.vkcache");
}
