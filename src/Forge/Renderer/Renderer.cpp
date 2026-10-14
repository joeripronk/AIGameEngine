#include "fgpch.h"
#include "Forge/Renderer/Renderer.h"

#include "Forge/Renderer/RenderCommand.h"
#include "Forge/Renderer/Shader.h"
#include "Forge/Renderer/VertexArray.h"
#include "Forge/Renderer/Texture.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/common.h>
#include <nvrhi/swapchain.h>
#include <nvrhi/texture.h>
#include <nvrhi/buffer.h>
#include <nvrhi/pipeline.h>
#include <nvrhi/descriptor_set.h>

namespace Forge {

	Scope<Renderer::SceneData> Renderer::s_SceneData = CreateScope<Renderer::SceneData>();
	ShaderLibrary Renderer::s_ShaderLibrary;
	uint32_t Renderer::s_Width = 1920;
	uint32_t Renderer::s_Height = 1080;
	nvrhi::CommandListHandle Renderer::s_CommandList;

	void Renderer::Init()
	{
		RenderCommand::Init();

		// Create shaders
		auto pbrShader = Shader::Create("PBR", Shaders::PBRVertexShader, Shaders::PBRFragmentShader);
		s_ShaderLibrary.Add(pbrShader);

		auto ssaoShader = Shader::Create("SSAO", Shaders::SSAOVertexShader, Shaders::SSAOFragmentShader);
		s_ShaderLibrary.Add(ssaoShader);

		auto hdrShader = Shader::Create("HDR", Shaders::HDRVertexShader, Shaders::HDRFragmentShader);
		s_ShaderLibrary.Add(hdrShader);

		auto finalShader = Shader::Create("Final", Shaders::FinalVertexShader, Shaders::FinalFragmentShader);
		s_ShaderLibrary.Add(finalShader);

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
		s_CommandList->begin();
		s_CommandList->setViewport(0, 0, (float)s_Width, (float)s_Height, 0.0f, 1.0f);
		s_CommandList->setScissor(0, 0, s_Width, s_Height);
	}

	void Renderer::EndScene()
	{
		s_CommandList->finish();
	}

	void Renderer::Submit(const Ref<class Shader>& shader, const Ref<class VertexArray>& vertexArray, const glm::mat4& transform)
	{
		shader->Bind();
		shader->SetMat4("u_ViewProjection", s_SceneData->ViewProjectionMatrix);
		shader->SetMat4("u_Transform", transform);

		vertexArray->Bind();
		RenderCommand::DrawIndexed(vertexArray);
	}

	void Renderer::Submit(const Ref<class Shader>& shader, const Ref<class VertexArray>& vertexArray, const glm::mat4& transform, const Ref<class Texture>& albedoTexture)
	{
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
		s_CommandList->begin();
	}

	void Renderer::EndShadowPass()
	{
		s_CommandList->finish();
	}

	void Renderer::BeginHDRPass()
	{
		s_CommandList->begin();
	}

	void Renderer::EndHDRPass()
	{
		s_CommandList->finish();
	}

	void Renderer::BeginSSAOPass()
	{
		s_CommandList->begin();
	}

	void Renderer::EndSSAOPass()
	{
		s_CommandList->finish();
	}

	void Renderer::BeginFinalPass()
	{
		s_CommandList->begin();
	}

	void Renderer::EndFinalPass()
	{
		s_CommandList->finish();
	}

	void Renderer::SubmitFinal()
	{
		s_CommandList->begin();
		s_CommandList->setViewport(0, 0, (float)s_Width, (float)s_Height, 0.0f, 1.0f);
		s_CommandList->setScissor(0, 0, s_Width, s_Height);

		auto finalShader = s_ShaderLibrary.Get("Final");
		finalShader->Bind();
		finalShader->SetFloat("u_SSAOIntensity", 0.5f);

		// Draw fullscreen quad
		auto quadVA = s_ShaderLibrary.Get("Final")->GetVertexArray();
		quadVA->Bind();
		RenderCommand::DrawIndexed(quadVA);

		s_CommandList->finish();
	}

}
