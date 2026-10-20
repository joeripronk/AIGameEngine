#include "fgpch.h"
#include "Platform/Vulkan/VkRenderer.h"
#include "Platform/Vulkan/Pipeline/PipelineCache.h"
#include "Platform/Vulkan/Pipeline/PipelinePresets.h"

#include "Forge/Core/FileSystem.h"
#include "Forge/Core/Log.h"

#include <array>
#include <cctype>
#include <filesystem>
#include <span>
#include <string>

namespace Forge { namespace Vulkan {

namespace {

	// The cache file is device-keyed (PipelineCacheFile::CachePathFor); the
	// payload is the driver's own format validated by pipelineCacheUuid.

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
		const VertexLayout& quadLayout, ::VkPipelineCache pipelineCache)
	{
		const std::array<VkPipelineShaderStageCreateInfo, 2> stages{
			VkPipelineShaderStageCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr },
			VkPipelineShaderStageCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr },
		};

		// fullscreen.vert declares aPosition/aUV as vertex attributes, so the
		// pipeline bakes the quad layout in. NoVertexInput leaves the shader
		// reading attributes nothing supplies: gl_Position is undefined and
		// every fullscreen draw lands off-screen.
		VkPipelineVertexInputStateCreateInfo vertexInput{};
		vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		VkVertexInputBindingDescription binding = quadLayout.Binding;
		std::array<VkVertexInputAttributeDescription, 16> attributes{};
		for (uint32_t i = 0; i < quadLayout.AttributeCount; ++i)
			attributes[i] = quadLayout.Attributes[i];
		vertexInput.vertexBindingDescriptionCount = 1;
		vertexInput.pVertexBindingDescriptions = &binding;
		vertexInput.vertexAttributeDescriptionCount = quadLayout.AttributeCount;
		vertexInput.pVertexAttributeDescriptions = attributes.data();

		VkPipelineInputAssemblyStateCreateInfo assembly{};
		assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

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
		info.pVertexInputState = &vertexInput;
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
		if (functions.CreateGraphicsPipelines(device.Handle(), pipelineCache, 1, &info, nullptr, &pipeline) != VK_SUCCESS)
			FG_CORE_ERROR("VkRenderer: fullscreen pipeline creation failed");
		return pipeline;
	}

}  // namespace

bool VkRenderer::CreatePipelines()
{
	const Device& device = m_Device;
	const DeviceFunctions& functions = device.GetFunctions();

	// The cross-session pipeline cache: load the device's file (if a real
	// one is there), start the driver's cache from its payload, and let
	// every pipeline creation below feed it. A missing, corrupt, foreign
	// or unreadable file is a cold start, not a failure.
	std::vector<uint8_t> initialPayload;
	{
		const std::string_view deviceName =
			m_PhysicalDevicePtr ? std::string_view(m_PhysicalDevicePtr->GetProperties().deviceName) : std::string_view();
		const std::filesystem::path path = PipelineCacheFile::CachePathFor(deviceName);
		std::vector<uint8_t> file;
		std::string reason;
		if (FileSystem::ReadFile(path, file))
		{
			const std::array<uint8_t, 16> kNoDevice{};
			const std::span<const uint8_t, 16> uuid =
				m_PhysicalDevicePtr ? PipelineCacheFile::UuidOf(m_PhysicalDevicePtr->GetProperties())
				                    : std::span<const uint8_t, 16>(kNoDevice);
			if (PipelineCacheFile::Parse(file, uuid, initialPayload, reason))
				FG_CORE_INFO("VkRenderer: pipeline cache loaded ({0} bytes from {1})", file.size(), path.string());
			else
				FG_CORE_WARN("VkRenderer: pipeline cache '{0}' ignored - {1}", path.string(), reason);
		}
	}
	if (!m_PipelineCache.Create(device, initialPayload))
		FG_CORE_WARN("VkRenderer: pipeline cache unavailable; pipelines compile fresh every session");

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
			Presets::DepthStencil::Opaque, Presets::Blend::None, m_PipelineCache.Handle());
	}

	// The instanced pipeline: the scene's pipeline with the instance
	// layout beside the mesh's, so the model matrix arrives as a vertex
	// attribute and one draw can put the mesh in many places.
	if (!m_VertexShaderPbrInstanced.IsNull() && !m_FragmentShaderPbr.IsNull())
	{
		m_PipelinePbrInstanced = VkRenderer::CreateGraphicsPipeline(device,
			m_VertexShaderPbrInstanced.Handle(), m_FragmentShaderPbr.Handle(), &m_VertexLayoutMesh,
			m_RenderPassHdr, 0, m_LayoutPbr,
			[]() {
				VkPipelineRasterizationStateCreateInfo r = Presets::Rasterizer::FillNoCull;
				r.frontFace = VK_FRONT_FACE_CLOCKWISE;
				return r;
			}(),
			Presets::DepthStencil::Opaque, Presets::Blend::None, m_PipelineCache.Handle(),
			&m_VertexLayoutInstance);
	}

	// The text pipeline: the scene's pipeline with the blending a label
	// needs, and the depth write off so the text does not hide what is
	// behind it. The atlas and the sampler come from the scene's descriptor
	// set, so the two share one layout.
	if (!m_VertexShaderText.IsNull() && !m_FragmentShaderText.IsNull())
	{
		m_PipelineText = VkRenderer::CreateGraphicsPipeline(device,
			m_VertexShaderText.Handle(), m_FragmentShaderText.Handle(), &m_VertexLayoutMesh,
			m_RenderPassHdr, 0, m_LayoutPbr,
			[]() {
				VkPipelineRasterizationStateCreateInfo r = Presets::Rasterizer::FillNoCull;
				r.frontFace = VK_FRONT_FACE_CLOCKWISE;
				return r;
			}(),
			Presets::DepthStencil::TestNoWrite, Presets::Blend::Alpha, m_PipelineCache.Handle());
	}

	// The shadow pipeline: depth-only, back-face culled, counter-clockwise
	// front face (nvrhi's frontCounterClockwise for the shadow draw).
	if (!m_VertexShaderShadow.IsNull() && !m_FragmentShaderShadow.IsNull())
	{
		m_PipelineShadow = VkRenderer::CreateGraphicsPipeline(device,
			m_VertexShaderShadow.Handle(), m_FragmentShaderShadow.Handle(), &m_VertexLayoutMesh,
			m_RenderPassShadow, 0, m_LayoutShadow,
			Presets::Rasterizer::FillCullBack,
			Presets::DepthStencil::Opaque, Presets::Blend::None, m_PipelineCache.Handle());
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
			m_VertexLayoutQuad, m_PipelineCache.Handle());
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
	destroyPipeline(m_PipelinePbrInstanced);
	destroyPipeline(m_PipelineText);
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
