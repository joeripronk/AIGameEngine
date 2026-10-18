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
		//
		// `fromReflectionProbe` draws the object *into* the reflection probe, so
		// the object must not sample the cubemap that is being written this pass:
		// probe draws bind the static fallback environment instead.
		void DrawObject(nvrhi::ICommandList* commandList,
			nvrhi::FramebufferHandle framebuffer,
			const Ref<Mesh>& mesh,
			const Ref<Texture>& albedoTexture,
			const glm::mat4& transform,
			const glm::mat3& normalMatrix,
			const glm::vec3& albedo,
			float metallic,
			float roughness,
			float ambientOcclusion,
			const glm::vec3& emissive,
			float emissiveIntensity,
			bool fromReflectionProbe = false);

		// Drops every cached draw state that binds `albedoTexture`. A hot-reload
		// replaces the GPU texture, and the old handle is destroyed; the cached
		// binding sets reference it and the pointer key can be reused by the
		// replacement, so entries for a dead texture must go before it dies.
		void InvalidateAlbedoCache(nvrhi::ITexture* albedoTexture);

		// --- Real-time reflection probe ---
		//
		// One scene draw per cube face, from a fixed probe position, into a
		// renderable cubemap that the main pass binds as the specular
		// environment. Polished metal blocks then mirror the scene around the
		// probe: walls, borders and the block stack appear in their surfaces.
		struct ProbeDraw
		{
			Ref<Mesh> MeshHandle;
			Ref<Texture> AlbedoTexture;
			glm::mat4 Model = glm::mat4(1.0f);
			glm::mat3 NormalMatrix = glm::mat3(1.0f);
			glm::vec3 Albedo = glm::vec3(1.0f);
			glm::vec3 Emissive = glm::vec3(0.0f);
			float Metallic = 0.0f;
			float Roughness = 1.0f;
			float AO = 1.0f;
			float EmissiveIntensity = 0.0f;
		};

		// Creates the probe cubemap, its depth buffer and the six face
		// framebuffers. Call once after Initialize(); the main pass starts
		// reflecting as soon as it exists.
		bool EnableReflectionProbe(uint32_t size);

		bool IsReflectionProbeEnabled() const { return m_ReflectionProbeEnabled; }

		void SetReflectionProbePosition(const glm::vec3& position) { m_ReflectionProbePosition = position; }

		// Records the six probe faces: clears each face, records its camera
		// uniform, and draws every supplied object into it.
		void RenderReflectionProbe(nvrhi::ICommandList* commandList,
			const glm::mat4& shadowViewProjection,
			const glm::vec3& lightDirection,
			const glm::vec3& lightColor,
			float lightIntensity,
			const glm::vec3& ambientLight,
			const std::vector<ProbeDraw>& draws);

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

		// The shadow map's resolution, so the caller can work out how wide one
		// texel is in the world - which is what a shadow bias has to be scaled by.
		glm::vec2 GetShadowMapSize() const { return m_ShadowMapSize; }

		// x = normal offset in world units, y = constant depth bias in NDC.
		// Computed from the shadow camera by the caller: the volume the map
		// covers is decided there, and the shader cannot infer it.
		void SetShadowBias(const glm::vec2& bias) { m_ShadowBias = bias; }

		bool IsValid() const { return m_VertexShader && m_FragmentShader && m_FrameLayout; }

	private:
		// Per-frame uniform block, matching ShadowMapParams in pbr.frag.
		struct ShadowMapParams
		{
			glm::vec2 ShadowMapSize;
			glm::vec2 ShadowBias;   // x normal offset (world), y depth bias (NDC)
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

		struct ObjectDrawKey
		{
			const Mesh* m_Mesh = nullptr;
			nvrhi::ITexture* m_AlbedoTexture = nullptr;
			// A draw that writes the probe samples a different environment than
			// a draw that reads it, so the env texture is part of the cache key.
			nvrhi::ITexture* m_EnvTexture = nullptr;
			bool operator==(const ObjectDrawKey& other) const
			{
				return m_Mesh == other.m_Mesh && m_AlbedoTexture == other.m_AlbedoTexture
					&& m_EnvTexture == other.m_EnvTexture;
			}
		};

		struct ObjectDrawKeyHash
		{
			size_t operator()(const ObjectDrawKey& key) const
			{
				size_t meshHash = std::hash<const Mesh*>{}(key.m_Mesh);
				size_t texHash = key.m_AlbedoTexture ? std::hash<const nvrhi::ITexture*>{}(key.m_AlbedoTexture) : 0;
				size_t envHash = key.m_EnvTexture ? std::hash<const nvrhi::ITexture*>{}(key.m_EnvTexture) : 0;
				size_t mixed = texHash ^ (envHash + 0x9e3779b97f4a7c15ULL + (texHash << 6) + (texHash >> 2));
				return meshHash ^ (mixed + 0x9e3779b97f4a7c15ULL + (meshHash << 6) + (meshHash >> 2));
			}
		};

		bool CreateBindingLayouts();
		bool CreateSharedResources();
		// Uses m_ColorFormat, which Initialize sets from the swapchain before
		// calling this. The pipeline's colour attachment has to match the
		// framebuffer it is later drawn into.
		bool CreatePipelines();

		// A pipeline bakes in its vertex input layout, so meshes with different
		// attribute sets need separate pipelines. Returns (and caches) the one
		// matching `inputLayout`.
		nvrhi::IGraphicsPipeline* GetPipelineForLayout(nvrhi::IInputLayout* inputLayout);

		// Builds (or returns the cached) GPU state for a mesh.
		const ObjectDraw* PrepareMesh(const Ref<Mesh>& mesh, const Ref<Texture>& albedoTexture,
			nvrhi::ITexture* envTexture);

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

		// --- Real-time reflection probe ---
		nvrhi::TextureHandle m_ReflectionMap;
		nvrhi::TextureHandle m_ReflectionDepth;
		std::vector<nvrhi::FramebufferHandle> m_ReflectionFramebuffers;
		uint32_t m_ReflectionSize = 0;
		bool m_ReflectionProbeEnabled = false;
		glm::vec3 m_ReflectionProbePosition = glm::vec3(0.0f);

		// ShadowMapParams uniform buffer.
		nvrhi::BufferHandle m_ShadowParamsBuffer;
		glm::vec2 m_ShadowMapSize = glm::vec2(2048.0f, 2048.0f);
		glm::vec2 m_ShadowBias = glm::vec2(0.0f);

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
		std::unordered_map<ObjectDrawKey, ObjectDraw, ObjectDrawKeyHash> m_DrawCache;

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

		// Fullscreen quad for IBL rendering (vertex buffer + binding).
		nvrhi::BufferHandle m_IBLQuadVBO;
		nvrhi::VertexBufferBinding m_IBLQuadVertexBinding;

		// IBL shader handles.
		nvrhi::ShaderHandle m_IrradianceVertexShader;
		nvrhi::ShaderHandle m_IrradianceFragmentShader;
		nvrhi::ShaderHandle m_PrefilterVertexShader;
		nvrhi::ShaderHandle m_PrefilterFragmentShader;

		// IBL binding layout (sampler2D env map + params).
		nvrhi::BindingLayoutHandle m_IBLBindingLayout;

		// IBL pipelines.
		nvrhi::GraphicsPipelineHandle m_IrradiancePipeline;
		nvrhi::GraphicsPipelineHandle m_PrefilterPipeline;

		// IBL parameters uniform buffer.
		nvrhi::BufferHandle m_IBLParamsBuffer;

		// Irradiance cubemap framebuffer (6-face rendering).
		nvrhi::FramebufferHandle m_IrradianceFramebuffer;

		// Prefilter cubemap framebuffer (mip-level rendering).
		nvrhi::FramebufferHandle m_PrefilterFramebuffer;

		// --- IBL prefiltering (private) ---
		bool CreateIBLFullscreenQuad();
		bool CreateIBLShaders();
		bool CreateIBLBindingLayout();
		bool CreateIBLPipelines();
		bool RenderIrradianceMap();
		bool RenderPrefilterMap(uint32_t mipCount);
	};
	}
