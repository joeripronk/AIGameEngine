#include "fgpch.h"
#include "Platform/Vulkan/VkRenderer.h"
#include "Platform/Vulkan/Pipeline/PipelinePresets.h"

#include "Forge/Core/Log.h"

namespace Forge { namespace Vulkan {

namespace {

	// Pipeline-state parity with the nvrhi renderer it replaces:
	//  - PBR: cull none, clockwise front face (nvrhi's default), depth test,
	//    no blending.
	//  - Shadow: cull back, counter-clockwise front face (nvrhi's
	//    frontCounterClockwise=true for the shadow pipeline), depth test.
	//  - Fullscreen passes: cull none, depth off, no blending.

	[[nodiscard]] ::VkPipelineLayout MakeLayout(const Device& device, const DescriptorSetLayout& setLayout)
	{
		const ::VkDescriptorSetLayout layouts[] = { setLayout.Handle() };
		VkPipelineLayoutCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		info.setLayoutCount = 1;
		info.pSetLayouts = layouts;
		::VkPipelineLayout layout = VK_NULL_HANDLE;
		if (device.GetFunctions().CreatePipelineLayout(device.Handle(), &info, nullptr, &layout) != VK_SUCCESS)
			FG_CORE_ERROR("VkRenderer: pipeline layout creation failed");
		return layout;
	}

	[[nodiscard]] ::VkPipeline MakeFullscreenPipeline(const Device& device, const DeviceFunctions& functions,
		::VkShaderModule vert, ::VkShaderModule frag, ::VkRenderPass renderPass, ::VkPipelineLayout layout,
		VkPrimitiveTopology topology)
	{
		const std::array<VkPipelineShaderStageCreateInfo, 2> stages{
			VkPipelineShaderStageCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr },
			VkPipelineShaderStageCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr },
		};

		VkPipelineInputAssemblyStateCreateInfo assembly{};
		assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		assembly.topology = topology;

		VkPipelineViewportStateCreateInfo viewportState{};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		VkPipelineMultisampleStateCreateInfo multisample{};
		multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		const std::array<VkDynamicState, 2> dynamicStates{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamic{};
		dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamic.pDynamicStates = dynamicStates.data();

		VkGraphicsPipelineCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		info.stageCount = static_cast<uint32_t>(stages.size());
		info.pStages = stages.data();
		info.pVertexInputState = &Presets::NoVertexInput;
		info.pInputAssemblyState = &assembly;
		info.pViewportState = &viewportState;
		info.pRasterizationState = &Presets::Rasterizer::FillNoCull;
		info.pMultisampleState = &multisample;
		info.pDepthStencilState = &Presets::DepthStencil::Disabled;
		info.pColorBlendState = &Presets::Blend::None;
		info.pDynamicState = &dynamic;
		info.layout = layout;
		info.renderPass = renderPass;
		info.subpass = 0;

		::VkPipeline pipeline = VK_NULL_HANDLE;
		if (functions.CreateGraphicsPipelines(device.Handle(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS)
			FG_CORE_ERROR("VkRenderer: fullscreen pipeline creation failed");
		return pipeline;
	}

}  // namespace

bool VkRenderer::CreatePipelines()
{
	const Device& device = m_Device;
	const DeviceFunctions& functions = device.GetFunctions();

	m_LayoutPbr = MakeLayout(device, m_SetLayoutPbr);
	m_LayoutShadow = MakeLayout(device, m_SetLayoutShadow);
	m_LayoutSsao = MakeLayout(device, m_SetLayoutSsao);
	m_LayoutBloom = MakeLayout(device, m_SetLayoutBloom);
	m_LayoutTonemap = MakeLayout(device, m_SetLayoutTonemap);
	m_LayoutFinal = MakeLayout(device, m_SetLayoutFinal);
	m_LayoutIrradiance = MakeLayout(device, m_SetLayoutIrradiance);
	m_LayoutPrefilter = MakeLayout(device, m_SetLayoutPrefilter);
	if (!m_LayoutPbr || !m_LayoutShadow || !m_LayoutSsao || !m_LayoutBloom ||
		!m_LayoutTonemap || !m_LayoutFinal || !m_LayoutIrradiance || !m_LayoutPrefilter)
	{
		FG_CORE_ERROR("VkRenderer: pipeline layout creation failed");
		return false;
	}

	// The scene pipeline: the mesh vertex layout baked in, opaque depth,
	// clockwise front face with no culling - nvrhi's state for this pipeline.
	if (!m_VertexShaderPbr.IsNull() && !m_FragmentShaderPbr.IsNull())
	{
		m_PipelinePbr = VkRenderer::CreateGraphicsPipeline(device,
			m_VertexShaderPbr.Handle(), m_FragmentShaderPbr.Handle(), &m_VertexLayoutMesh,
			m_RenderPassHdr, 0, m_LayoutPbr,
			[]() {
				VkPipelineRasterizationStateCreateInfo r = Presets::Rasterizer::FillNoCull;
				r.frontFace = VK_FRONT_FACE_CLOCKWISE;
				return r;
			}(),
			Presets::DepthStencil::Opaque, Presets::Blend::None);
	}

	// The shadow pipeline: depth-only, back-face culled, counter-clockwise
	// front face (nvrhi's frontCounterClockwise for the shadow draw).
	if (!m_VertexShaderShadow.IsNull() && !m_FragmentShaderShadow.IsNull())
	{
		m_PipelineShadow = VkRenderer::CreateGraphicsPipeline(device,
			m_VertexShaderShadow.Handle(), m_FragmentShaderShadow.Handle(), &m_VertexLayoutMesh,
			m_RenderPassShadow, 0, m_LayoutShadow,
			Presets::Rasterizer::FillCullBack,
			Presets::DepthStencil::Opaque, Presets::Blend::None);
	}

	// Fullscreen passes share the fullscreen vertex shader; the strip
	// topology lets every one of them draw the 4-vertex quad unindexed.
	auto makePost = [this, &device, &functions](ShaderModule& frag, ::VkRenderPass renderPass,
		::VkPipelineLayout layout) -> ::VkPipeline
	{
		if (m_VertexShaderFullscreen.IsNull() || frag.IsNull() || renderPass == VK_NULL_HANDLE)
			return VK_NULL_HANDLE;
		return MakeFullscreenPipeline(device, functions,
			m_VertexShaderFullscreen.Handle(), frag.Handle(), renderPass, layout,
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
	};

	m_PipelineSsao = makePost(m_FragmentShaderSsao, m_RenderPassR8, m_LayoutSsao);
	m_PipelineBloom = makePost(m_FragmentShaderBloom, m_RenderPassColor16, m_LayoutBloom);
	m_PipelineBlur = makePost(m_FragmentShaderBlur, m_RenderPassColor16, m_LayoutBloom);
	m_PipelineTonemap = makePost(m_FragmentShaderHdr, m_RenderPassColor16, m_LayoutTonemap);
	m_PipelineFinal = makePost(m_FragmentShaderFinal, m_RenderPassPresent, m_LayoutFinal);
	m_PipelineIrradiance = makePost(m_FragmentShaderIrradiance, m_RenderPassCubeFace, m_LayoutIrradiance);
	m_PipelinePrefilter = makePost(m_FragmentShaderPrefilter, m_RenderPassCubeFace, m_LayoutPrefilter);

	if (!m_PipelinePbr)
	{
		FG_CORE_ERROR("VkRenderer: PBR pipeline unavailable; the scene will not be drawn");
		return false;
	}
	if (!m_PipelineSsao || !m_PipelineBloom || !m_PipelineBlur || !m_PipelineTonemap || !m_PipelineFinal)
		FG_CORE_WARN("VkRenderer: post chain incomplete; frames render without post-processing");

	return true;
}

void VkRenderer::DestroyPipelines()
{
	const auto& functions = m_Device.GetFunctions();
	if (m_Device.IsNull())
		return;

	auto destroyPipeline = [&functions, this](::VkPipeline& pipeline)
	{
		if (pipeline != VK_NULL_HANDLE)
			functions.DestroyPipeline(m_Device.Handle(), pipeline, nullptr);
		pipeline = VK_NULL_HANDLE;
	};
	destroyPipeline(m_PipelinePbr);
	destroyPipeline(m_PipelineShadow);
	destroyPipeline(m_PipelineSsao);
	destroyPipeline(m_PipelineBloom);
	destroyPipeline(m_PipelineBlur);
	destroyPipeline(m_PipelineTonemap);
	destroyPipeline(m_PipelineFinal);
	destroyPipeline(m_PipelineIrradiance);
	destroyPipeline(m_PipelinePrefilter);
	for (::VkPipeline& pipeline : m_PipelineProbeFaces)
		destroyPipeline(pipeline);
	m_PipelineProbeFaces.clear();

	auto destroyLayout = [&functions, this](::VkPipelineLayout& layout)
	{
		if (layout != VK_NULL_HANDLE)
			functions.DestroyPipelineLayout(m_Device.Handle(), layout, nullptr);
		layout = VK_NULL_HANDLE;
	};
	destroyLayout(m_LayoutPbr);
	destroyLayout(m_LayoutShadow);
	destroyLayout(m_LayoutSsao);
	destroyLayout(m_LayoutBloom);
	destroyLayout(m_LayoutTonemap);
	destroyLayout(m_LayoutFinal);
	destroyLayout(m_LayoutIrradiance);
	destroyLayout(m_LayoutPrefilter);
}

}}
