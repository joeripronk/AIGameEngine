#include "fgpch.h"
#include "Forge/Renderer/RendererAPI.h"
#include "Forge/Renderer/Renderer.h"
#include "Forge/Renderer/EditorCamera.h"

#if !defined(FG_NO_VULKAN_RUNTIME)
#include "Platform/Vulkan/VulkanRendererAPI.h"
#endif

namespace Forge {

	RendererAPI::API RendererAPI::s_API = RendererAPI::API::Vulkan;

	// Null stub implementations (used when FG_NO_VULKAN_RUNTIME is defined)
#if defined(FG_NO_VULKAN_RUNTIME)

	class NullRendererAPI : public RendererAPI
	{
	public:
		void Init() override {}
		void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override { (void)x; (void)y; (void)width; (void)height; }
		void SetClearColor(const glm::vec4& color) override { (void)color; }
		void Clear() override {}
		void DrawIndexed(const Ref<class VertexArray>& vertexArray, uint32_t indexCount = 0) override { (void)vertexArray; (void)indexCount; }
		void SetLineWidth(float width) override { (void)width; }
	};

	class NullVertexArray
	{
	public:
		void Bind() const {}
		void Unbind() const {}
		uint32_t GetIndexCount() const { return 0; }
		const Ref<class VertexArray>& GetVertexArray() const { static Ref<class VertexArray> nullVA; return nullVA; }
	};

	class NullVertexBuffer
	{
	public:
		NullVertexBuffer() {}
		void Bind() const {}
		void Unbind() const {}
	};

	class NullIndexBuffer
	{
	public:
		NullIndexBuffer() {}
		void Bind() const {}
		void Unbind() const {}
		uint32_t GetCount() const { return 0; }
	};

	class NullMesh
	{
	public:
		NullMesh() {}
	};

	class NullTexture
	{
	public:
		NullTexture() {}
	};

	class NullShader
	{
	public:
		NullShader(const std::string& name) : m_Name(name) {}
		const std::string& GetName() const { return m_Name; }
	private:
		std::string m_Name;
	};

	class NullShaderLibrary
	{
	public:
		void Add(const std::string& name, const Ref<Shader>& shader) { (void)name; (void)shader; }
		void Add(const Ref<Shader>& shader) { (void)shader; }
		Ref<Shader> Load(const std::string& filepath) { (void)filepath; return nullptr; }
		Ref<Shader> Load(const std::string& name, const std::string& filepath) { (void)name; (void)filepath; return nullptr; }
		Ref<Shader> Get(const std::string& name) { (void)name; return nullptr; }
		bool Exists(const std::string& name) const { (void)name; return false; }
	};

	class NullCamera
	{
	public:
		NullCamera() {}
	};

	class NullEditorCamera
	{
	public:
		NullEditorCamera() {}
	};

	class NullRenderer
	{
	public:
		static void Init() {}
		static void Shutdown() {}
		static void OnWindowResize(uint32_t width, uint32_t height) { (void)width; (void)height; }
		static void BeginScene() {}
		static void EndScene() {}
		static void Submit(const Ref<class Shader>& shader, const Ref<class VertexArray>& vertexArray, const glm::mat4& transform = glm::mat4(1.0f)) { (void)shader; (void)vertexArray; (void)transform; }
		static void Submit(const Ref<class Shader>& shader, const Ref<class VertexArray>& vertexArray, const glm::mat4& transform, const Ref<class Texture>& albedoTexture) { (void)shader; (void)vertexArray; (void)transform; (void)albedoTexture; }
		static void BeginShadowPass() {}
		static void EndShadowPass() {}
		static void BeginHDRPass() {}
		static void EndHDRPass() {}
		static void BeginSSAOPass() {}
		static void EndSSAOPass() {}
		static void BeginFinalPass() {}
		static void EndFinalPass() {}
		static void SubmitFinal() {}
	};

#endif // FG_NO_VULKAN_RUNTIME

	Scope<RendererAPI> RendererAPI::Create()
	{
#if defined(FG_NO_VULKAN_RUNTIME)
		return CreateScope<NullRendererAPI>();
#else
		switch (s_API)
		{
			case RendererAPI::API::None:    FG_CORE_ASSERT_MSG(false, "RendererAPI::None is currently not supported!"); return nullptr;
			case RendererAPI::API::Vulkan:  return CreateScope<VulkanRendererAPI>();
		}

		FG_CORE_ASSERT_MSG(false, "Unknown RendererAPI!");
		return nullptr;
#endif
	}

}
