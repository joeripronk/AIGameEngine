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

#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

namespace Forge {

	Scope<Renderer::SceneData> Renderer::s_SceneData = CreateScope<Renderer::SceneData>();
	ShaderLibrary Renderer::s_ShaderLibrary;
	uint32_t Renderer::s_Width = 1920;
	uint32_t Renderer::s_Height = 1080;
	nvrhi::CommandListHandle Renderer::s_CommandList;

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
		// TODO: Implement Vulkan command list begin
		// s_CommandList->begin();
		// s_CommandList->setViewport(0, 0, (float)s_Width, (float)s_Height, 0.0f, 1.0f);
		// s_CommandList->setScissor(0, 0, s_Width, s_Height);
	}

	void Renderer::EndScene()
	{
		// TODO: Implement Vulkan command list finish
		// s_CommandList->finish();
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
