#include "fgpch.h"
#include "Forge/Renderer/Renderer.h"

#if !defined(FG_NO_VULKAN_RUNTIME)

#include "Forge/Renderer/RenderCommand.h"
#include "Forge/Renderer/Shader.h"
#include "Forge/Renderer/ShaderLibrary.h"
#include "Forge/Renderer/VertexArray.h"
#include "Forge/Renderer/VertexBuffer.h"
#include "Forge/Renderer/IndexBuffer.h"
#include "Forge/Renderer/Texture.h"

#include "Platform/Vulkan/VulkanVertexArray.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanRendererAPI.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

namespace Forge {

	Scope<Renderer::SceneData> Renderer::s_SceneData = CreateScope<Renderer::SceneData>();
	ShaderLibrary Renderer::s_ShaderLibrary;
	uint32_t Renderer::s_Width = 1920;
	uint32_t Renderer::s_Height = 1080;
	nvrhi::CommandListHandle Renderer::s_CommandList;
	Ref<VertexArray> Renderer::s_FullscreenQuad;

	void Renderer::Init()
	{
		RenderCommand::Init();

		// Load pre-compiled SPIR-V shaders from resources/shaders.
		auto pbrShader = Shader::CreateFromSPIRV("PBR", "resources/shaders/pbr.vert.spv", "resources/shaders/pbr.frag.spv");
		if (pbrShader)
			s_ShaderLibrary.Add(pbrShader);

		auto ssaoShader = Shader::CreateFromSPIRV("SSAO", "resources/shaders/fullscreen.vert.spv", "resources/shaders/ssao.frag.spv");
		if (ssaoShader)
			s_ShaderLibrary.Add(ssaoShader);

		auto hdrShader = Shader::CreateFromSPIRV("HDR", "resources/shaders/fullscreen.vert.spv", "resources/shaders/hdr.frag.spv");
		if (hdrShader)
			s_ShaderLibrary.Add(hdrShader);

		auto finalShader = Shader::CreateFromSPIRV("Final", "resources/shaders/fullscreen.vert.spv", "resources/shaders/final.frag.spv");
		if (finalShader)
			s_ShaderLibrary.Add(finalShader);

		auto shadowShader = Shader::CreateFromSPIRV("Shadow", "resources/shaders/shadow.vert.spv", "resources/shaders/shadow.frag.spv");
		if (shadowShader)
			s_ShaderLibrary.Add(shadowShader);

		// Create fullscreen quad
		auto quadVA = VertexArray::Create();

		float vertices[] = {
			-1.0f, -1.0f, 0.0f, 0.0f,
			 1.0f, -1.0f, 1.0f, 0.0f,
			 1.0f,  1.0f, 1.0f, 1.0f,
			-1.0f,  1.0f, 0.0f, 1.0f
		};

		uint32_t indices[] = { 0, 1, 2, 2, 3, 0 };

		auto vb = VertexBuffer::Create(sizeof(vertices), vertices);
		vb->SetLayout({ 2, 2 });
		quadVA->AddVertexBuffer(vb);
		quadVA->SetIndexBuffer(IndexBuffer::Create(6, indices));

		// Resolve the fullscreen quad's input layout against the post-process
		// shaders (they share fullscreen.vert: position + uv).
		if (auto vulkanQuad = std::dynamic_pointer_cast<VulkanVertexArray>(quadVA))
		{
			const std::vector<std::string> kFullscreenAttributes = { "aPosition", "aUV" };

			bool anyLayout = false;
			for (const char* shaderName : { "SSAO", "HDR", "Final" })
			{
				auto shader = s_ShaderLibrary.Get(shaderName);
				auto vulkanShader = std::dynamic_pointer_cast<VulkanShader>(shader);
				if (!vulkanShader)
					continue;
				anyLayout = vulkanQuad->UpdateLayout(vulkanShader.get(), kFullscreenAttributes) || anyLayout;
			}

			if (anyLayout)
			{
				FG_CORE_INFO("Fullscreen quad layout resolved (stride {0} bytes)", vulkanQuad->GetStride());
			}
			else
			{
				FG_CORE_WARN("Fullscreen quad layout could not be resolved; post-processing disabled");
			}
		}

		// Retain the quad so its GPU buffers outlive Init() and can be uploaded.
		s_FullscreenQuad = quadVA;

		FG_CORE_INFO("Renderer initialized with {0} shaders", s_ShaderLibrary.GetShaderCount());
	}

	void Renderer::Shutdown()
	{
	}

	void Renderer::OnWindowResize(uint32_t width, uint32_t height)
	{
		RenderCommand::SetViewport(0, 0, width, height);
	}

	void Renderer::BeginScene()
	{
		if (!s_CommandList)
			return;

		// Open the frame's command list. nvrhi requires it to be open before any
		// resource writes (buffer uploads) or draw calls are recorded.
		s_CommandList->open();

		// Flush the static fullscreen quad's vertex/index data on the first
		// frame; subsequent frames have nothing pending.
		FlushPendingUploads(s_FullscreenQuad);
	}

	void Renderer::EndScene()
	{
		if (!s_CommandList)
			return;

		// Submit the recorded work to the graphics queue and close the list.
		s_CommandList->close();

		auto device = VulkanRendererAPI::GetStaticDevice();
		if (device)
			device->executeCommandList(s_CommandList.Get());
	}

	void Renderer::FlushPendingUploads(const Ref<class VertexArray>& vertexArray)
	{
		if (!s_CommandList || !vertexArray)
			return;

		auto vulkanVertexArray = std::dynamic_pointer_cast<VulkanVertexArray>(vertexArray);
		if (vulkanVertexArray)
			vulkanVertexArray->UploadTo(s_CommandList.Get());
	}

	void Renderer::Submit(const Ref<class Shader>& shader, const Ref<class VertexArray>& vertexArray, const glm::mat4& transform)
	{
		if (!shader || !vertexArray)
			return;

		shader->Bind();
		shader->SetMat4("u_ViewProjection", s_SceneData->ViewProjectionMatrix);
		shader->SetMat4("u_Transform", transform);

		vertexArray->Bind();
		RenderCommand::DrawIndexed(vertexArray);
	}

	void Renderer::Submit(const Ref<class Shader>& shader, const Ref<class VertexArray>& vertexArray, const glm::mat4& transform, const Ref<class Texture>& albedoTexture)
	{
		if (!shader || !vertexArray)
			return;

		(void)albedoTexture;
		shader->Bind();
		shader->SetMat4("u_ViewProjection", s_SceneData->ViewProjectionMatrix);
		shader->SetMat4("u_Transform", transform);
		shader->SetFloat3("u_Albedo", glm::vec3(1.0f));
		shader->SetFloat("u_Metallic", 0.0f);
		shader->SetFloat("u_Roughness", 0.5f);
		shader->SetFloat("u_AO", 1.0f);
		shader->SetFloat3("u_Emissive", glm::vec3(0.0f));
		shader->SetFloat("u_EmissiveIntensity", 0.0f);
		shader->SetFloat("u_LightIntensity", 1.0f);
		shader->SetFloat3("u_LightColor", glm::vec3(1.0f));
		shader->SetFloat3("u_LightDirection", glm::vec3(0.0f, -1.0f, 0.0f));
		shader->SetFloat3("u_AmbientLight", glm::vec3(0.02f, 0.02f, 0.02f));

		vertexArray->Bind();
		RenderCommand::DrawIndexed(vertexArray);
	}

	void Renderer::BeginShadowPass()
	{
		// TODO: Implement Vulkan shadow pass
	}

	void Renderer::EndShadowPass()
	{
		// TODO: Implement Vulkan shadow pass end
	}

	void Renderer::BeginHDRPass()
	{
		// TODO: Implement Vulkan HDR pass
	}

	void Renderer::EndHDRPass()
	{
		// TODO: Implement Vulkan HDR pass end
	}

	void Renderer::BeginSSAOPass()
	{
		// TODO: Implement Vulkan SSAO pass
	}

	void Renderer::EndSSAOPass()
	{
		// TODO: Implement Vulkan SSAO pass end
	}

	void Renderer::BeginFinalPass()
	{
		// TODO: Implement Vulkan final pass
	}

	void Renderer::EndFinalPass()
	{
		// TODO: Implement Vulkan final pass end
	}

	void Renderer::SubmitFinal()
	{
		// TODO: Implement Vulkan final submit
	}

}

#endif // FG_NO_VULKAN_RUNTIME
