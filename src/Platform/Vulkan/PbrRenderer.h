#pragma once

#include "Forge/Core/Base.h"
#include "Forge/Renderer/Mesh.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

#include <glm/glm.hpp>

#include <unordered_map>
#include <vector>

namespace Forge {

	class VulkanShader;

	// Draws the scene with the PBR shader into a swapchain framebuffer.
	//
	// The per-frame and per-object uniform blocks declared here match
	// resources/shaders/pbr.vert and pbr.frag exactly.
	//
	// A frame is recorded in two steps so that per-object constant buffer writes
	// (which nvrhi records into the command list) are issued before the draw
	// that consumes them:
	//   1. UpdateFrame()  - records the camera uniform upload and the background clear
	//   2. DrawObject()   - records one object's uniform upload and draw call
	class PbrRenderer
	{
	public:
		PbrRenderer() = default;
		~PbrRenderer() = default;

		PbrRenderer(const PbrRenderer&) = delete;
		PbrRenderer& operator=(const PbrRenderer&) = delete;

		// Builds the pipeline and the shared (per-frame) resources.
		bool Initialize(nvrhi::Format colorFormat);

		// Records the presentation size used for the viewport and scissor.
		void SetFrameSize(uint32_t width, uint32_t height)
		{
			m_FrameWidth = static_cast<float>(width);
			m_FrameHeight = static_cast<float>(height);
		}

		// Records the per-frame camera uniform and clears the framebuffer.
		// `shadowViewProjection` is the light's matrix used by the shadow map.
		void UpdateFrame(nvrhi::ICommandList* commandList,
			nvrhi::FramebufferHandle framebuffer,
			const glm::mat4& viewProjection,
			const glm::mat4& shadowViewProjection,
			const glm::vec3& cameraPosition,
			const glm::vec3& lightDirection,
			const glm::vec3& lightColor,
			float lightIntensity,
			const glm::vec3& ambientLight);

		// --- Shadow pass ---

		// Records the per-frame shadow uniform (light view-projection matrix).
		// `shadowViewProjection` is the light's matrix used by the shadow map.
		void UpdateShadowFrame(nvrhi::ICommandList* commandList,
			nvrhi::FramebufferHandle framebuffer,
			const glm::mat4& shadowViewProjection);

		// Records the draw call for one object in the shadow pass.
		void DrawShadowObject(nvrhi::ICommandList* commandList,
			nvrhi::FramebufferHandle framebuffer,
			const Ref<Mesh>& mesh,
			const glm::mat4& transform);

		// Recreates the shadow map for a new size.
		bool ResizeShadowMap(uint32_t width, uint32_t height);

		// Records the draw call for one object. Geometry and its input layout are
		// uploaded on first use and cached.
		void DrawObject(nvrhi::ICommandList* commandList,
			nvrhi::FramebufferHandle framebuffer,
			const Ref<Mesh>& mesh,
			const glm::mat4& transform,
			const glm::mat3& normalMatrix,
			const glm::vec3& albedo,
			float metallic,
			float roughness,
			float ambientOcclusion,
			const glm::vec3& emissive,
			float emissiveIntensity);

		// Recreates the depth buffer for a new framebuffer size.
		bool Resize(uint32_t width, uint32_t height);

		// Sets the HDR framebuffer for offscreen rendering.
		void SetHDRFramebuffer(nvrhi::FramebufferHandle framebuffer, nvrhi::TextureHandle hdrTexture, nvrhi::TextureHandle depthTexture);

		// --- IBL (Image-Based Lighting) ---

		// Loads an HDR environment map (equirectangular or cubemap) and creates
		// the prefiltered mip levels (prefilter + irradiance).
		bool LoadEnvironmentMap(nvrhi::TextureHandle envMap);

		// Creates the irradiance map (diffuse IBL) from the environment map.
		bool CreateIrradianceMap();

		// Creates the prefilter map (specular IBL) with mip levels for each roughness.
		bool CreatePrefilterMap(uint32_t mipCount);

		// Accessors.
		nvrhi::FramebufferHandle GetShadowFramebuffer() const { return m_ShadowFramebuffer; }
		nvrhi::TextureHandle GetShadowMap() const { return m_ShadowMap; }
		nvrhi::FramebufferHandle GetHDRFramebuffer() const { return m_HDRFramebuffer; }
		nvrhi::TextureHandle GetHDRTexture() const { return m_HDRTexture; }
		nvrhi::TextureHandle GetDepthTexture() const { return m_DepthTexture; }

		bool IsValid() const { return m_VertexShader && m_FragmentShader && m_FrameLayout; }

	private:
		// Per-frame uniform block, matching CameraData in pbr.vert / pbr.frag.
		struct ShadowMapParams
		{
			glm::vec2 ShadowMapSize;
		};

		struct CameraUniforms
		{
			glm::mat4 ViewProjection;
			glm::mat4 ShadowViewProjection;
			glm::vec4 LightDirection;   // xyz = direction the light travels
			glm::vec4 LightColor;       // rgb = colour, a = intensity
			glm::vec4 CameraPosition;   // xyz = world position
			glm::vec4 AmbientLight;     // rgb = ambient colour
			glm::vec4 SSAOParams;       // x = shadow bias, y = ao strength
		};

		// Per-object uniform block, matching ObjectData in pbr.vert / pbr.frag.
		struct ObjectUniforms
		{
			glm::mat4 Transform;
			glm::mat4 NormalMatrix;
			glm::vec4 Albedo;                 // rgb = base colour
			glm::vec4 MetallicRoughnessAO;    // x metallic, y roughness, z ao
			glm::vec4 Emissive;               // rgb emissive, a intensity
		};

		// GPU state cached per mesh (geometry, input layout, per-object buffers).
		struct ObjectDraw
		{
			nvrhi::BufferHandle objectBuffer;
			nvrhi::BindingSetHandle bindingSet;
			nvrhi::InputLayoutHandle inputLayout;
			nvrhi::VertexBufferBinding vertexBinding;
			nvrhi::IndexBufferBinding indexBinding;
			bool indexed = false;
			uint32_t indexCount = 0;
		};

		bool CreateBindingLayouts();
		bool CreateSharedResources();
		bool CreatePipelines(nvrhi::Format colorFormat);

		// A pipeline bakes in its vertex input layout, so meshes with different
		// attribute sets need separate pipelines. Returns (and caches) the one
		// matching `inputLayout`.
		nvrhi::IGraphicsPipeline* GetPipelineForLayout(nvrhi::IInputLayout* inputLayout);

		// Builds (or returns the cached) GPU state for a mesh.
		const ObjectDraw* PrepareMesh(const Ref<Mesh>& mesh);

		// Shadow draw cache (per mesh: input layout, vertex/index bindings, object buffer, binding set).
		struct ShadowDraw
		{
			nvrhi::BufferHandle objectBuffer;
			nvrhi::BindingSetHandle bindingSet;
			nvrhi::InputLayoutHandle inputLayout;
			nvrhi::VertexBufferBinding vertexBinding;
			nvrhi::IndexBufferBinding indexBinding;
			bool indexed = false;
			uint32_t indexCount = 0;
		};

		// --- Shadow pass helpers (private) ---
		bool CreateShadowMap(uint32_t width, uint32_t height);
		bool CreateShadowBindingLayout();
		bool CreateShadowResources();
		bool CreateShadowPipelines();
		nvrhi::IGraphicsPipeline* GetShadowPipelineForLayout(nvrhi::IInputLayout* inputLayout);
		const ShadowDraw* PrepareShadowMesh(const Ref<Mesh>& mesh);

		nvrhi::ShaderHandle m_VertexShader;
		nvrhi::ShaderHandle m_FragmentShader;

		nvrhi::BindingLayoutHandle m_FrameLayout;

		nvrhi::BufferHandle m_CameraBuffer;

		nvrhi::SamplerHandle m_LinearSampler;
		nvrhi::TextureHandle m_WhiteTexture;
		nvrhi::TextureHandle m_ShadowTexture;

		// Environment map (1x1 white fallback until a real env map is loaded).
		nvrhi::TextureHandle m_EnvMapFallback;

		// ShadowMapParams uniform buffer.
		nvrhi::BufferHandle m_ShadowParamsBuffer;
		glm::vec2 m_ShadowMapSize = glm::vec2(2048.0f, 2048.0f);

		nvrhi::TextureHandle m_DepthTexture;
		uint32_t m_DepthWidth = 0;
		uint32_t m_DepthHeight = 0;

		// --- Shadow map ---
		nvrhi::TextureHandle m_ShadowMap;
		uint32_t m_ShadowMapWidth = 0;
		uint32_t m_ShadowMapHeight = 0;
		nvrhi::FramebufferHandle m_ShadowFramebuffer;

		// Shadow binding layout (different from PBR: binding 1 = ObjectData, binding 4 = ShadowData).
		nvrhi::BindingLayoutHandle m_ShadowLayout;
		// Shadow camera uniform buffer (per-frame).
		nvrhi::BufferHandle m_ShadowCameraBuffer;

		// Shadow pipeline cache (one per input layout, like PBR).
		std::unordered_map<nvrhi::IInputLayout*, nvrhi::GraphicsPipelineHandle> m_ShadowPipelineCache;
		std::unordered_map<const Mesh*, ShadowDraw> m_ShadowDrawCache;

		// Shadow vertex/fragment shaders (from the Shadow shader library entry).
		nvrhi::ShaderHandle m_ShadowVertexShader;
		nvrhi::ShaderHandle m_ShadowFragmentShader;

		// Shadow sampler (linear, clamped for depth comparison).
		nvrhi::SamplerHandle m_ShadowSampler;

		std::unordered_map<nvrhi::IInputLayout*, nvrhi::GraphicsPipelineHandle> m_PipelineCache;
		std::unordered_map<const Mesh*, ObjectDraw> m_DrawCache;

		nvrhi::Format m_ColorFormat = nvrhi::Format::UNKNOWN;
		float m_FrameWidth = 0.0f;
		float m_FrameHeight = 0.0f;

		// HDR offscreen framebuffer and textures.
		nvrhi::FramebufferHandle m_HDRFramebuffer;
		nvrhi::TextureHandle m_HDRTexture;

		// --- IBL (Image-Based Lighting) ---

		// Environment map texture (HDR equirectangular or cubemap).
		nvrhi::TextureHandle m_EnvMap;

		// Irradiance map (diffuse IBL, prefiltered).
		nvrhi::TextureHandle m_IrradianceMap;

		// Prefilter map mip levels (specular IBL, prefiltered per roughness).
		nvrhi::TextureHandle m_PrefilterMap;

		// Number of mip levels in the prefilter map.
		uint32_t m_PrefilterMipCount = 0;
	};

}
