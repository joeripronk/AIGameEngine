#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <span>

namespace Forge { namespace Vulkan {

// Pipeline state as data (BRUTAL Presets.cs): named, fixed blocks composed
// by pointer at PSO creation. A pipeline that quietly enables blending it
// did not ask for starts here, as a name you can point at.
namespace Presets {

	namespace BlendAttachment {

		inline constexpr VkPipelineColorBlendAttachmentState None{
			/*blendEnable*/ VK_FALSE,
			VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
			VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };

		// src*srcA + dst*(1-srcA): the alpha blend every translucent pass wants.
		inline constexpr VkPipelineColorBlendAttachmentState Alpha{
			VK_TRUE,
			VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD,
			VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };

		// src + dst*srcA: glow, bloom composites, particles.
		inline constexpr VkPipelineColorBlendAttachmentState Additive{
			VK_TRUE,
			VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_OP_ADD,
			VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };

		// src + dst*(1-src): the brighter the destination, the softer the source;
		// components stay <= 1.
		inline constexpr VkPipelineColorBlendAttachmentState SoftAdditive{
			VK_TRUE,
			VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, VK_BLEND_OP_ADD,
			VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };

		// src*dst: darkening, projected gradients.
		inline constexpr VkPipelineColorBlendAttachmentState Multiplicative{
			VK_TRUE,
			VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
			VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };

	}

	namespace Blend {

		inline constexpr VkPipelineColorBlendStateCreateInfo None{
			VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0,
			VK_FALSE, VK_LOGIC_OP_CLEAR, 1, &BlendAttachment::None, { 0.0f, 0.0f, 0.0f, 0.0f } };

		inline constexpr VkPipelineColorBlendStateCreateInfo Alpha{
			VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0,
			VK_FALSE, VK_LOGIC_OP_CLEAR, 1, &BlendAttachment::Alpha, { 0.0f, 0.0f, 0.0f, 0.0f } };

		inline constexpr VkPipelineColorBlendStateCreateInfo Additive{
			VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0,
			VK_FALSE, VK_LOGIC_OP_CLEAR, 1, &BlendAttachment::Additive, { 0.0f, 0.0f, 0.0f, 0.0f } };

		inline constexpr VkPipelineColorBlendStateCreateInfo SoftAdditive{
			VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0,
			VK_FALSE, VK_LOGIC_OP_CLEAR, 1, &BlendAttachment::SoftAdditive, { 0.0f, 0.0f, 0.0f, 0.0f } };

		inline constexpr VkPipelineColorBlendStateCreateInfo Multiplicative{
			VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0,
			VK_FALSE, VK_LOGIC_OP_CLEAR, 1, &BlendAttachment::Multiplicative, { 0.0f, 0.0f, 0.0f, 0.0f } };

	}

	namespace DepthStencil {

		inline constexpr VkPipelineDepthStencilStateCreateInfo Opaque{
			VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr, 0,
			/*depthTestEnable*/ VK_TRUE, /*depthWriteEnable*/ VK_TRUE,
			/*depthCompareOp*/ VK_COMPARE_OP_LESS,
			/*depthBoundsTestEnable*/ VK_FALSE, /*stencilTestEnable*/ VK_FALSE,
			VkStencilOpState{}, VkStencilOpState{},
			0.0f, 1.0f };

		inline constexpr VkPipelineDepthStencilStateCreateInfo Disabled{
			VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr, 0,
			VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS, VK_FALSE, VK_FALSE,
			VkStencilOpState{}, VkStencilOpState{}, 0.0f, 1.0f };

		inline constexpr VkPipelineDepthStencilStateCreateInfo TestNoWrite{
			VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr, 0,
			VK_TRUE, VK_FALSE, VK_COMPARE_OP_LESS, VK_FALSE, VK_FALSE,
			VkStencilOpState{}, VkStencilOpState{}, 0.0f, 1.0f };

	}

	namespace Rasterizer {

		inline constexpr VkPipelineRasterizationStateCreateInfo FillNoCull{
			VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr, 0,
			VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE,
			VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f };

		inline constexpr VkPipelineRasterizationStateCreateInfo FillCullFront{
			VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr, 0,
			VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_FRONT_BIT,
			VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f };

		inline constexpr VkPipelineRasterizationStateCreateInfo FillCullBack{
			VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr, 0,
			VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT,
			VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f };

		// Wireframe for gizmos and debug draws.
		inline constexpr VkPipelineRasterizationStateCreateInfo LineNoCull{
			VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr, 0,
			VK_FALSE, VK_FALSE, VK_POLYGON_MODE_LINE, VK_CULL_MODE_NONE,
			VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_FALSE, 0.0f, 0.0f, 1.0f, 1.0f };

	}

	namespace Topology {

		inline constexpr VkPrimitiveTopology TriangleList = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inline constexpr VkPrimitiveTopology TriangleStrip = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		inline constexpr VkPrimitiveTopology LineList = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		inline constexpr VkPrimitiveTopology PointList = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

	}

	namespace Sampler {

		inline constexpr VkSamplerCreateInfo LinearClampToEdge{
			VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, nullptr, 0,
			VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			/*mipLodBias*/ 0.0f, /*anisotropyEnable*/ VK_FALSE, /*maxAnisotropy*/ 1.0f,
			/*compareEnable*/ VK_FALSE, /*compareOp*/ VK_COMPARE_OP_NEVER,
			/*minLod*/ 0.0f, /*maxLod*/ 1000.0f,
			VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK, /*unnormalizedCoordinates*/ VK_FALSE };

		inline constexpr VkSamplerCreateInfo LinearRepeat{
			VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, nullptr, 0,
			VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
			VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT,
			0.0f, VK_FALSE, 1.0f, VK_FALSE, VK_COMPARE_OP_NEVER, 0.0f, 1000.0f,
			VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK, VK_FALSE };

		inline constexpr VkSamplerCreateInfo Anisotropic16{
			VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, nullptr, 0,
			VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
			VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT,
			0.0f, VK_TRUE, 16.0f, VK_FALSE, VK_COMPARE_OP_NEVER, 0.0f, 0.0f,
			VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK, VK_FALSE };

		inline constexpr VkSamplerCreateInfo PointClampToEdge{
			VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, nullptr, 0,
			VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			0.0f, VK_FALSE, 1.0f, VK_FALSE, VK_COMPARE_OP_NEVER, 0.0f, 1000.0f,
			VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK, VK_FALSE };

	}

	// No vertex input: the fullscreen passes generate their geometry.
	inline constexpr VkPipelineVertexInputStateCreateInfo NoVertexInput{
		VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr, 0,
		0, nullptr, 0, nullptr };

}

}}
