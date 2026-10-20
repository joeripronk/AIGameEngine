#include "fgpch.h"
#include <cstdlib>
#include "Forge/Renderer/Renderer.h"

#if !defined(FG_NO_VULKAN_RUNTIME)

#include "Forge/Renderer/RenderCommand.h"
#include "Forge/Renderer/Texture.h"
#include "Forge/Renderer/LOD.h"
#include "Forge/Renderer/Mesh.h"
#include "Forge/Renderer/CpuMesh.h"
#include "Forge/Renderer/CpuTexture.h"
#include "Forge/Renderer/Camera.h"
#include "Forge/Renderer/RendererStats.h"
#include "Forge/Renderer/ShadowCamera.h"

#include "Forge/Assets/AssetWatcher.h"
#include "Forge/Debug/Instrumentor.h"

#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"

#include "Platform/Vulkan/VkRenderer.h"

#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <limits>
#include <functional>
#include <unordered_map>
#include <vector>

namespace Forge {

	namespace {

		// One upload per model path, holding the CPU mesh it came from so the key
		// can never name freed memory (a raw-pointer key would be handed the next
		// mesh allocated at the same address and drawn under the wrong entity's
		// transform). Cleared in Renderer::Shutdown, with the other GPU resources.
		struct UploadedMesh
		{
			Ref<CpuMesh> Source;
			Ref<class Mesh> Upload;
		};

		std::unordered_map<std::string, UploadedMesh> s_UploadedMeshes;

		// One texture per albedo path, keyed by the file's signature so a
		// changed file re-decodes. A function-local static would be destroyed
		// at exit, in an order nobody controls and after the device it was
		// uploaded to; living here is what lets Renderer::Shutdown release it
		// while the device is still alive.
		struct AlbedoCacheEntry
		{
			Ref<Texture> Albedo;
			FileSignature Signature;
		};

		std::unordered_map<std::string, AlbedoCacheEntry> s_AlbedoTextureCache;

		RendererStatistics s_Statistics;

		// "No shadow camera this frame" is reported once per outage, not once per
		// frame: a scene with no mesh is a legitimate state, and a warning inside
		// the frame loop fills the log with the same line.
		bool s_ShadowCameraWarned = false;

		// The camera matrix RenderScene resolved this frame; the SSAO pass
		// reconstructs positions from depth through its inverse.
		glm::mat4 s_LastViewProjection = glm::mat4(1.0f);

		// Diagnostic replay state (FG_DIAG_RENDER_AFTER).
		Vulkan::VkRenderer::CameraParams s_DiagCamera{};
		std::function<void()> s_DiagReplay;
		glm::mat4 s_DiagShadow = glm::mat4(1.0f);
		std::function<void()> s_DiagShadowReplay;
		uint32_t s_CurrentDiagImage = UINT32_MAX;
		bool s_DiagEnded = false;

		// The AABB of the shared fallback cube, so a meshless entity is culled
		// against the box it is actually drawn as.
		glm::vec3 DefaultCubeMin()
		{
			static const Ref<CpuMesh> cube = Primitives::CreateCube(1.0f);
			return cube->GetMin();
		}

		glm::vec3 DefaultCubeMax()
		{
			static const Ref<CpuMesh> cube = Primitives::CreateCube(1.0f);
			return cube->GetMax();
		}

		// The world-space AABB of a mesh's bounds drawn under `model`: the
		// bounding box is not axis-aligned after a rotation, so it is the AABB
		// of the transformed corners - a box culled against that is a box that
		// is *provably* off-screen for every pixel.
		void TransformAABB(const glm::vec3& min, const glm::vec3& max, const glm::mat4& model,
			glm::vec3& outMin, glm::vec3& outMax)
		{
			outMin = glm::vec3(std::numeric_limits<float>::max());
			outMax = glm::vec3(std::numeric_limits<float>::lowest());

			for (int i = 0; i < 8; ++i)
			{
				const glm::vec3 corner(
					(i & 1) ? max.x : min.x,
					(i & 2) ? max.y : min.y,
					(i & 4) ? max.z : min.z);
				const glm::vec3 world(model * glm::vec4(corner, 1.0f));
				outMin = glm::min(outMin, world);
				outMax = glm::max(outMax, world);
			}
		}

		[[nodiscard]] uint32_t ScaledSize(uint32_t presentSize)
		{
			return std::max(1u, static_cast<uint32_t>(static_cast<float>(presentSize) * Renderer::GetRenderScale())) & ~1u;
		}

	}  // namespace


namespace {

	// The degenerate texture a failed albedo load returns: width 0 is the
	// report, and the cache entry is never null for the callers that
	// dereference it.
	class EmptyTexture final : public Texture
	{
	public:
		uint32_t GetWidth() const override { return 0; }
		uint32_t GetHeight() const override { return 0; }
	};

}  // namespace

	uint32_t Renderer::s_Width = 1920;
	uint32_t Renderer::s_Height = 1080;
	uint32_t Renderer::s_InitialWidth = 0;
	uint32_t Renderer::s_InitialHeight = 0;
	bool Renderer::s_SwapchainNeedsRecreate = false;

	// The window's size as the last resize event reported it, and the size it
	// had in the previous frame. A size that has not changed since the last
	// frame has settled, and a settled size is the one worth rebuilding the
	// swapchain and the render targets for.
	uint32_t Renderer::s_WindowWidth = 0;
	uint32_t Renderer::s_WindowHeight = 0;
	uint32_t Renderer::s_SettledWidth = 0;
	uint32_t Renderer::s_SettledHeight = 0;
	void* Renderer::s_WindowHandle = nullptr;
	float Renderer::s_RenderScale = 1.0f;

	void Renderer::Init()
	{
		RenderCommand::Init();

		if (s_InitialWidth > 0 && s_InitialHeight > 0)
		{
			s_Width = s_InitialWidth;
			s_Height = s_InitialHeight;

			// The window's size is known before any resize event arrives:
			// the swapchain is built at it. Recording it here is what lets
			// the renderer compare the swapchain's own extent with the
			// window's size instead of trusting a flag that a present can
			// clear.
			s_WindowWidth = s_Width;
			s_WindowHeight = s_Height;
			s_SettledWidth = s_Width;
			s_SettledHeight = s_Height;
		}

		if (void* windowHandle = GetActiveWindowHandle())
		{
			Vulkan::VkRenderer& renderer = Vulkan::VkRenderer::Get();
			if (!renderer.Initialize(windowHandle, s_Width, s_Height, ScaledSize(s_Width), ScaledSize(s_Height)))
				FG_CORE_ERROR("Renderer: the raw-Vulkan renderer could not be initialized; nothing will be drawn");
		}
		else
		{
			FG_CORE_WARN("Renderer: no native window handle; presentation disabled");
		}

		// Load IBL environment map if available.
		if (const char* envMapPath = std::getenv("FG_ENV_MAP"); envMapPath && *envMapPath)
		{
			Ref<CpuTexture> envTex = CpuTexture::Load(envMapPath);
			if (envTex && Vulkan::VkRenderer::Get().IsReady())
			{
				if (Vulkan::VkRenderer::Get().LoadEnvironmentMap(envTex))
					FG_CORE_INFO("IBL: environment map loaded from '{0}'", envMapPath);
				else
					FG_CORE_WARN("IBL: environment map '{0}' did not prefilter", envMapPath);
			}
		}

		FG_CORE_INFO("Renderer initialized (raw Vulkan backend)");
	}

	void Renderer::Shutdown()
	{
		// The renderer releases its own resources in device order (pipelines,
		// targets, staging, device), so the caches - which hold Refs to
		// VkMesh/VkTexture objects whose GPU memory the renderer frees - go
		// first: a cache outliving the device would hand the next mesh the
		// address of a freed one.
		s_AlbedoTextureCache.clear();
		s_UploadedMeshes.clear();

		Vulkan::VkRenderer::Get().ShutdownDevice();
	}

	bool Renderer::CaptureSwapchainImage(uint32_t imageIndex, const std::string& path)
	{
		if (imageIndex == UINT32_MAX)
			return false;

		return Vulkan::VkRenderer::Get().CaptureSwapchainImage(imageIndex, path);
	}

	void Renderer::RenderScene(Scene* scene, uint32_t swapchainImage)
	{
		FG_PROFILE_FUNCTION();
		Vulkan::VkRenderer& renderer = Vulkan::VkRenderer::Get();
		if (!scene || swapchainImage == UINT32_MAX || !renderer.IsReady() || !renderer.HasTargets())
			return;

		// The primary camera, resolved the one way the engine resolves it.
		Entity cameraEntity = scene->GetPrimaryCameraEntity();
		if (!cameraEntity)
		{
			FG_CORE_WARN("RenderScene: no primary camera; nothing to draw");
			return;
		}

		auto lights = scene->GetAllEntitiesWith<LightComponent>();
		bool hasDirectionalLight = false;
		for (auto handle : lights)
		{
			if (lights.get<LightComponent>(handle).Type == LightComponent::Type::Directional)
			{
				hasDirectionalLight = true;
				break;
			}
		}

		auto& camera = cameraEntity.GetComponent<CameraComponent>();
		const auto& cameraTransform = cameraEntity.GetComponent<TransformComponent>();

		// Derive the view and projection from the camera transform.
		glm::mat4 cameraRotation = glm::toMat4(glm::quat(cameraTransform.Rotation));
		glm::mat4 cameraTransformMatrix =
			glm::translate(glm::mat4(1.0f), cameraTransform.Translation) * cameraRotation;

		camera.ViewMatrix = glm::inverse(cameraTransformMatrix);
		camera.ProjectionMatrix = glm::perspective(
			glm::radians(camera.FieldOfView),
			s_Height > 0 ? static_cast<float>(s_Width) / static_cast<float>(s_Height) : camera.AspectRatio,
			camera.NearClip,
			camera.FarClip);
		camera.ViewProjectionMatrix = camera.ProjectionMatrix * camera.ViewMatrix;

		glm::vec3 lightDirection = { -0.5f, -1.0f, -0.3f };
		glm::vec3 lightColor = { 1.0f, 1.0f, 1.0f };
		float lightIntensity = 1.0f;

		if (hasDirectionalLight)
		{
			for (auto handle : lights)
			{
				const auto& light = lights.get<LightComponent>(handle);
				if (light.Type != LightComponent::Type::Directional)
					continue;

				lightDirection = glm::normalize(light.Direction);
				lightColor = light.Color;
				lightIntensity = light.Intensity;
				break;
			}
		}

		// The shadow transform is built from the scene's bounds and the light's
		// direction, and built *once*: the shadow pass and the fragment-side
		// lookup are two readings of one transform. A second copy of the
		// computation is a second thing to keep correct, and an identity
		// view-projection is not "no shadows" - it projects geometry by its own
		// world position, so shading lands at arbitrary places in the field.
		//
		// The map resolution the bias is computed against is scaled with the
		// render scale: the fragment side samples the map at the HDR target's
		// resolution, and with supersampling one HDR texel covers half a shadow
		// texel. A bias sized for the map alone then lifts every fragment off
		// its own shadow - the whole scene lit, the whole scene black after
		// tonemap.
		std::vector<std::pair<glm::vec3, glm::vec3>> shadowBoxes;
		const auto shadowCasters = scene->GetAllEntitiesWith<MeshComponent, TransformComponent>();
		for (auto handle : shadowCasters)
		{
			const auto& shadowMesh = shadowCasters.get<MeshComponent>(handle);
			const auto& shadowTransform = shadowCasters.get<TransformComponent>(handle);

			const glm::mat4 model = glm::translate(glm::mat4(1.0f), shadowTransform.Translation)
				* glm::toMat4(glm::quat(shadowTransform.Rotation))
				* glm::scale(glm::mat4(1.0f), shadowTransform.Scale);

			const glm::vec3 boundsMin = shadowMesh.CpuMeshData ? shadowMesh.CpuMeshData->GetMin() : DefaultCubeMin();
			const glm::vec3 boundsMax = shadowMesh.CpuMeshData ? shadowMesh.CpuMeshData->GetMax() : DefaultCubeMax();

			glm::vec3 worldMin, worldMax;
			TransformAABB(boundsMin, boundsMax, model, worldMin, worldMax);
			shadowBoxes.emplace_back(worldMin, worldMax);
		}

		// The instanced meshes cast shadows too: the box is the union of the
		// instances', because the shadow pass draws them one at a time and
		// each of them has to be inside the map.
		auto multiCasters = scene->GetAllEntitiesWith<MultiMeshComponent, TransformComponent>();
		for (auto handle : multiCasters)
		{
			const auto& multi = multiCasters.get<MultiMeshComponent>(handle);
			const auto& transform = multiCasters.get<TransformComponent>(handle);

			if (multi.Transforms.empty())
				continue;

			const glm::mat4 group = transform.GetTransform();
			const glm::vec3 boundsMin = multi.CpuMeshData ? multi.CpuMeshData->GetMin() : DefaultCubeMin();
			const glm::vec3 boundsMax = multi.CpuMeshData ? multi.CpuMeshData->GetMax() : DefaultCubeMax();

			glm::vec3 worldMin = boundsMin;
			glm::vec3 worldMax = boundsMax;
			for (const glm::mat4& instance : multi.Transforms)
			{
				glm::vec3 instanceMin;
				glm::vec3 instanceMax;
				TransformAABB(boundsMin, boundsMax, group * instance, instanceMin, instanceMax);
				worldMin = glm::min(worldMin, instanceMin);
				worldMax = glm::max(worldMax, instanceMax);
			}

			shadowBoxes.emplace_back(worldMin, worldMax);
		}

		Vulkan::VkRenderer::CameraParams cameraParams{};
		cameraParams.ViewProjection = camera.ViewProjectionMatrix;
		cameraParams.CameraPosition = cameraTransform.Translation;
		cameraParams.LightDirection = lightDirection;
		cameraParams.LightColor = lightColor;
		cameraParams.LightIntensity = lightIntensity;
		cameraParams.AmbientLight = { 0.03f, 0.03f, 0.04f };

		const float mapResolution = static_cast<float>(renderer.GetShadowMapSizeValue()) * s_RenderScale;
		const ShadowCamera shadowCamera = BuildShadowCamera(shadowBoxes, lightDirection, mapResolution);
		if (shadowCamera.Valid)
		{
			cameraParams.ShadowViewProjection = shadowCamera.ViewProjection;
			const ShadowBias bias = ComputeShadowBias(shadowCamera.TexelWorld, shadowCamera.TexelDepth);
			cameraParams.ShadowBias = glm::vec2(bias.NormalOffsetWorld, bias.DepthBiasNdc);
			s_ShadowCameraWarned = false;
		}
		else if (!s_ShadowCameraWarned)
		{
			s_ShadowCameraWarned = true;
			FG_CORE_WARN("Renderer: no shadow camera this frame ({}): shadows are off",
				shadowBoxes.empty() ? "no mesh geometry in the scene" : "the directional light has no direction");
		}

		// Shared fallback cube for meshless entities: built once and kept for
		// the lifetime of the renderer; it owns GPU buffers, and a per-frame
		// cube was being destroyed while the GPU was still reading it.
		static Ref<Mesh> s_DefaultCube;
		auto getMesh = [](const Ref<Mesh>& mesh) -> Ref<Mesh>
		{
			if (mesh)
				return mesh;

			if (!s_DefaultCube)
			{
				Ref<CpuMesh> cpuCube = Primitives::CreateCube(1.0f);
				s_DefaultCube = Mesh::CreateFromCpu(cpuCube.get(), Material{});
			}
			return s_DefaultCube;
		};

		// The component holds the CPU geometry (the engine core imports it, so a
		// broken model path is reported where the entity was authored) and the
		// GPU mesh is derived from it here, because uploading needs a device.
		// The upload is keyed by the path, so a mesh that is drawn in many
		// places is uploaded once.
		auto uploadGeometry = [](const std::string& path, const Ref<CpuMesh>& cpuMesh) -> Ref<Mesh>
		{
			if (!cpuMesh)
				return nullptr;

			auto it = s_UploadedMeshes.find(path);
			if (it != s_UploadedMeshes.end() && it->second.Source == cpuMesh)
				return it->second.Upload;

			// The material authored on the entity overrides the imported one; the
			// renderer's per-draw parameters are read from the PBR component below,
			// so the upload only carries geometry.
			Ref<Mesh> uploaded = Mesh::CreateFromCpu(cpuMesh.get(), Material{});
			if (!uploaded)
				return nullptr;

			s_UploadedMeshes[path] = UploadedMesh{ cpuMesh, uploaded };
			return uploaded;
		};

		auto uploadMesh = [&uploadGeometry](MeshComponent& component) -> Ref<Mesh>
		{
			if (component.Mesh)
				return component.Mesh;

			component.Mesh = uploadGeometry(component.ModelPath, component.CpuMeshData);
			return component.Mesh;
		};

		// The uploaded mesh of a level of detail. The levels are uploaded
		// like any other mesh - the cache is keyed by the path - so a level
		// that is never drawn is never uploaded.
		auto uploadLevel = [](const std::string& path, const Ref<CpuMesh>& cpuMesh) -> Ref<Mesh>
		{
			if (!cpuMesh)
				return nullptr;

			auto it = s_UploadedMeshes.find(path);
			if (it != s_UploadedMeshes.end() && it->second.Source == cpuMesh)
				return it->second.Upload;

			Ref<Mesh> uploaded = Mesh::CreateFromCpu(cpuMesh.get(), Material{});
			if (!uploaded)
				return nullptr;

			s_UploadedMeshes[path] = UploadedMesh{ cpuMesh, uploaded };
			return uploaded;
		};

		struct DrawRecord
		{
			Ref<Mesh> MeshHandle;
			glm::mat4 Model;
			glm::mat3 NormalMatrix;
			glm::vec3 Albedo;
			glm::vec3 Emissive;
			float Metallic;
			float Roughness;
			float AO;
			float EmissiveIntensity;
			float Depth;
			Ref<Texture> AlbedoTexture;

			// The instances of an instanced draw: the same mesh in many
			// places, one draw call. Empty for a mesh that is drawn once.
			std::vector<glm::mat4> Instances;
		};

		std::vector<DrawRecord> records;

		// One frustum for the frame. Everything drawn this pass is drawn under
		// the camera that resolved above, so every entity is culled against the
		// same six planes the shader will light under.
		const FrustumCorners frustum = Frustum::CalculateCorners(camera.ViewProjectionMatrix);

		auto meshEntities = scene->GetAllEntitiesWith<MeshComponent, TransformComponent>();
		for (auto handle : meshEntities)
		{
			auto& meshComponent = meshEntities.get<MeshComponent>(handle);
			const auto& transform = meshEntities.get<TransformComponent>(handle);

			Ref<Mesh> mesh = getMesh(uploadMesh(meshComponent));
			if (!mesh)
				continue;

			// The level of detail: a mesh that is far away is drawn with
			// fewer triangles, because the difference between the levels
			// cannot be seen at that distance.
			const Ref<CpuMesh>* geometry = &meshComponent.CpuMeshData;
			if (!meshComponent.LODModelPaths.empty() && !meshComponent.LODSwitchDistances.empty())
			{
				const float distance = glm::length(transform.Translation - cameraTransform.Translation);
				const size_t level = LOD::ChooseLevel(meshComponent.LODSwitchDistances, distance);

				if (level > 0 && level - 1 < meshComponent.LODModelPaths.size() &&
					level - 1 < meshComponent.LODCpuMeshData.size())
				{
					const size_t index = level - 1;
					if (const Ref<CpuMesh>& lodMesh = meshComponent.LODCpuMeshData[index])
					{
						Ref<Mesh> uploaded = uploadLevel(meshComponent.LODModelPaths[index], lodMesh);
						if (uploaded)
						{
							mesh = uploaded;
							geometry = &lodMesh;
						}
					}
				}
			}

			glm::mat4 model = transform.GetTransform()
				* glm::scale(glm::mat4(1.0f), meshComponent.Scale);

			// Frustum culling: a mesh whose transformed bounding box is on the
			// outer side of one plane cannot cover a single pixel, and the draw
			// that would produce nothing costs a full pipeline draw.
			glm::vec3 boundsMin = (*geometry) ? (*geometry)->GetMin() : DefaultCubeMin();
			glm::vec3 boundsMax = (*geometry) ? (*geometry)->GetMax() : DefaultCubeMax();
			glm::vec3 worldMin, worldMax;
			TransformAABB(boundsMin, boundsMax, model, worldMin, worldMax);
			if (!Frustum::IntersectsAABB(frustum, worldMin, worldMax))
			{
				s_Statistics.AddFrustumCulled();
				continue;
			}

			glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(model));

			glm::vec3 albedo = { 1.0f, 1.0f, 1.0f };
			float metallic = 0.0f;
			float roughness = 0.5f;
			float ambientOcclusion = 1.0f;
			glm::vec3 emissive = { 0.0f, 0.0f, 0.0f };
			float emissiveIntensity = 0.0f;
			Ref<Texture> albedoTexture;

			Entity entity(handle, scene);
			if (entity.HasComponent<PBRMaterialComponent>())
			{
				const auto& material = entity.GetComponent<PBRMaterialComponent>();
				albedo = material.Albedo;
				metallic = material.Metallic;
				roughness = material.Roughness;
				ambientOcclusion = material.AO;
				emissive = material.Emissive;
				emissiveIntensity = material.EmissiveIntensity;
				if (!material.AlbedoTexturePath.empty())
					albedoTexture = GetAlbedoTexture(material.AlbedoTexturePath);
			}

			const glm::vec3 objectPosition(model[3][0], model[3][1], model[3][2]);
			const glm::vec4 viewPosition = camera.ViewMatrix * glm::vec4(objectPosition, 1.0f);
			const float depth = viewPosition.z;

			records.push_back({ mesh, model, normalMatrix, albedo, emissive,
				metallic, roughness, ambientOcclusion, emissiveIntensity, depth,
				albedoTexture, {} });
		}

		// The instanced meshes: one mesh in many places, one draw call.
		//
		// The entity's own transform is applied to every instance, so the
		// whole group can be moved as one, and the instances' own transforms
		// place them inside it.
		auto multiEntities = scene->GetAllEntitiesWith<MultiMeshComponent, TransformComponent>();
		for (auto handle : multiEntities)
		{
			auto& multi = multiEntities.get<MultiMeshComponent>(handle);
			const auto& transform = multiEntities.get<TransformComponent>(handle);

			if (multi.Transforms.empty())
				continue;

			Ref<Mesh> mesh = multi.Mesh;
			if (!mesh)
			{
				mesh = uploadGeometry(multi.ModelPath, multi.CpuMeshData);
				if (!mesh)
					continue;
				multi.Mesh = mesh;
			}

			const glm::mat4 group = transform.GetTransform();

			std::vector<glm::mat4> instances;
			instances.reserve(multi.Transforms.size());
			for (const glm::mat4& instance : multi.Transforms)
				instances.push_back(group * instance);

			// The bounds are the union of the instances': the draw is culled
			// as one thing, so the box has to cover all of it.
			glm::vec3 boundsMin = multi.CpuMeshData ? multi.CpuMeshData->GetMin() : DefaultCubeMin();
			glm::vec3 boundsMax = multi.CpuMeshData ? multi.CpuMeshData->GetMax() : DefaultCubeMax();
			glm::vec3 worldMin = boundsMin;
			glm::vec3 worldMax = boundsMax;
			for (const glm::mat4& instance : instances)
			{
				glm::vec3 instanceMin;
				glm::vec3 instanceMax;
				TransformAABB(boundsMin, boundsMax, instance, instanceMin, instanceMax);
				worldMin = glm::min(worldMin, instanceMin);
				worldMax = glm::max(worldMax, instanceMax);
			}

			if (!Frustum::IntersectsAABB(frustum, worldMin, worldMax))
			{
				s_Statistics.AddFrustumCulled();
				continue;
			}

			glm::vec3 albedo = { 1.0f, 1.0f, 1.0f };
			float metallic = 0.0f;
			float roughness = 0.5f;
			float ambientOcclusion = 1.0f;
			glm::vec3 emissive = { 0.0f, 0.0f, 0.0f };
			float emissiveIntensity = 0.0f;
			Ref<Texture> albedoTexture;

			Entity entity(handle, scene);
			if (entity.HasComponent<PBRMaterialComponent>())
			{
				const auto& material = entity.GetComponent<PBRMaterialComponent>();
				albedo = material.Albedo;
				metallic = material.Metallic;
				roughness = material.Roughness;
				ambientOcclusion = material.AO;
				emissive = material.Emissive;
				emissiveIntensity = material.EmissiveIntensity;
				if (!material.AlbedoTexturePath.empty())
					albedoTexture = GetAlbedoTexture(material.AlbedoTexturePath);
			}

			// The depth is the group's: the instances are sorted as one draw.
			const glm::vec3 objectPosition(group[3][0], group[3][1], group[3][2]);
			const glm::vec4 viewPosition = camera.ViewMatrix * glm::vec4(objectPosition, 1.0f);

			records.push_back({ mesh, glm::mat4(1.0f), glm::mat3(1.0f), albedo, emissive,
				metallic, roughness, ambientOcclusion, emissiveIntensity, viewPosition.z,
				albedoTexture, std::move(instances) });
		}

		s_Statistics.Reset();

		// Shadow pass: depth-only draws of every mesh under the light's
		// transform, before the PBR pass reads the map.
		renderer.BeginShadowScene(cameraParams.ShadowViewProjection);
		for (const DrawRecord& record : records)
		{
			if (record.Instances.empty())
			{
				renderer.DrawShadowObject(record.MeshHandle, record.Model);
				continue;
			}

			// The shadow pass draws the instances one at a time: the shadow
			// pipeline is not the instanced one, and a shadow of a forest is
			// not what the frame is about.
			for (const glm::mat4& instance : record.Instances)
				renderer.DrawShadowObject(record.MeshHandle, instance);
		}

		// Reflection probe pass: redraw the scene from the probe into a cubemap
		// *before* BeginScene records the real camera, because the probe writes
		// the shared camera buffer once per face and BeginScene overwrites it
		// with the main camera right after.
		if (renderer.IsReflectionProbeEnabled() && !records.empty())
		{
			std::vector<Vulkan::VkRenderer::ProbeDraw> probeDraws;
			probeDraws.reserve(records.size());
			for (const DrawRecord& record : records)
			{
				// An instanced draw is probed as its instances: the probe
				// draws the scene the way it is drawn, one mesh at a time.
				if (record.Instances.empty())
				{
					Vulkan::VkRenderer::ProbeDraw probe{};
					probe.MeshHandle = record.MeshHandle;
					probe.AlbedoTexture = record.AlbedoTexture;
					probe.Model = record.Model;
					probe.NormalMatrix = record.NormalMatrix;
					probe.Albedo = record.Albedo;
					probe.Emissive = record.Emissive;
					probe.Metallic = record.Metallic;
					probe.Roughness = record.Roughness;
					probe.AO = record.AO;
					probe.EmissiveIntensity = record.EmissiveIntensity;
					probeDraws.push_back(std::move(probe));
					continue;
				}

				for (const glm::mat4& instance : record.Instances)
				{
					Vulkan::VkRenderer::ProbeDraw probe{};
					probe.MeshHandle = record.MeshHandle;
					probe.AlbedoTexture = record.AlbedoTexture;
					probe.Model = instance;
					probe.NormalMatrix = glm::inverseTranspose(glm::mat3(instance));
					probe.Albedo = record.Albedo;
					probe.Emissive = record.Emissive;
					probe.Metallic = record.Metallic;
					probe.Roughness = record.Roughness;
					probe.AO = record.AO;
					probe.EmissiveIntensity = record.EmissiveIntensity;
					probeDraws.push_back(std::move(probe));
				}
			}
			renderer.RenderReflectionProbe(cameraParams, probeDraws);
		}

		// Record the per-frame uniforms and clear the HDR target.
		renderer.BeginScene(cameraParams);

		std::sort(records.begin(), records.end(), [](const DrawRecord& a, const DrawRecord& b)
		{
			return a.Depth < b.Depth;
		});

		uint32_t drawn = 0;
		for (const DrawRecord& record : records)
		{
			Vulkan::VkRenderer::ObjectParams object{};
			object.Model = record.Model;
			object.NormalMatrix = record.NormalMatrix;
			object.Albedo = record.Albedo;
			object.Metallic = record.Metallic;
			object.Roughness = record.Roughness;
			object.AO = record.AO;
			object.Emissive = record.Emissive;
			object.EmissiveIntensity = record.EmissiveIntensity;

			if (record.Instances.empty())
			{
				renderer.DrawObject(record.MeshHandle, record.AlbedoTexture, object);
				s_Statistics.AddDrawCall(record.MeshHandle->GetIndexCount() / 3, record.AlbedoTexture != nullptr);
			}
			else
			{
				// One draw for the whole group: the instances are the
				// vertices the same mesh is drawn with.
				renderer.DrawInstanced(record.MeshHandle, record.AlbedoTexture, record.Instances, object);
				s_Statistics.AddDrawCall(record.MeshHandle->GetIndexCount() / 3 *
					static_cast<int>(record.Instances.size()), record.AlbedoTexture != nullptr);
			}
			++drawn;
		}

		// SSAO reconstructs positions from depth through this matrix.
		s_LastViewProjection = camera.ViewProjectionMatrix;

		// Diagnostic storage for the FG_DIAG_RENDER_AFTER replay.
		s_DiagCamera = cameraParams;
		s_DiagShadow = cameraParams.ShadowViewProjection;
		{
			auto shadowRecords = records;
			s_DiagShadowReplay = [shadowRecords]()
			{
				for (const DrawRecord& record : shadowRecords)
					Vulkan::VkRenderer::Get().DrawShadowObject(record.MeshHandle, record.Model);
			};
		}
		auto replayRecords = records;
		s_DiagReplay = [replayRecords]()
		{
			for (const DrawRecord& record : replayRecords)
			{
				Vulkan::VkRenderer::ObjectParams object{};
				object.Model = record.Model;
				object.NormalMatrix = record.NormalMatrix;
				object.Albedo = record.Albedo;
				object.Metallic = record.Metallic;
				object.Roughness = record.Roughness;
				object.AO = record.AO;
				object.Emissive = record.Emissive;
				object.EmissiveIntensity = record.EmissiveIntensity;
				Vulkan::VkRenderer::Get().DrawObject(record.MeshHandle, record.AlbedoTexture, object);
			}
		};

		// A diagnostic, off by default: at any log level this is one
		// line per frame, and a game loop that logs every frame scrolls
		// the console away at sixty lines a second. The same numbers are
		// available through GetStatistics(), which the editor displays.
		if (drawn > 0 && std::getenv("FG_DIAG_FRAME_LOG"))
		{
			FG_CORE_INFO("RenderScene: presenting {0}x{1}, camera at ({2}, {3}, {4}), {5} mesh entities ({6} frustum-culled)",
				s_Width, s_Height,
				cameraTransform.Translation.x, cameraTransform.Translation.y, cameraTransform.Translation.z,
				drawn, s_Statistics.FrustumCulled);
		}
	}

	RendererStatistics Renderer::GetStatistics()
	{
		return s_Statistics;
	}

	bool Renderer::EnableReflectionProbe(uint32_t size)
	{
		Vulkan::VkRenderer& renderer = Vulkan::VkRenderer::Get();
		if (!renderer.IsReady())
		{
			FG_CORE_WARN("Renderer: reflection probe requested without a renderer device");
			return false;
		}
		return renderer.EnableReflectionProbe(size);
	}

	bool Renderer::IsReflectionProbeEnabled()
	{
		return Vulkan::VkRenderer::Get().IsReflectionProbeEnabled();
	}

	void Renderer::SetReflectionProbePosition(const glm::vec3& position)
	{
		Vulkan::VkRenderer::Get().SetReflectionProbePosition(position);
	}

	void Renderer::SetRenderScale(float scale)
	{
		s_RenderScale = scale > 1.0f ? std::min(scale, 4.0f) : 1.0f;

		if (Vulkan::VkRenderer::Get().HasSwapchain())
			ApplyFrameSize(s_Width, s_Height);
	}

	float Renderer::GetRenderScale()
	{
		return s_RenderScale;
	}

	Ref<Texture> Renderer::GetAlbedoTexture(const std::string& path)
	{
		// Hot-reload contract (the Hazel AssetManager pattern): the file is
		// re-stat-ed per path per frame - one stat - and a size/timestamp
		// change re-decodes and re-uploads it. A failed load is retried as
		// soon as the file appears, and a failed reload keeps the texture that
		// worked rather than swapping in a broken one.
		const FileSignature current = FileSignature::ForFile(path);

		auto cached = s_AlbedoTextureCache.find(path);
		if (cached != s_AlbedoTextureCache.end() && current == cached->second.Signature)
			return cached->second.Albedo;

		const bool hadEntry = cached != s_AlbedoTextureCache.end();
		const Ref<Texture> previous = hadEntry ? cached->second.Albedo : Ref<Texture>{};

		Ref<Texture> created = Texture::Create(path);
		const bool loaded = created && created->GetWidth() > 0;

		// A failed load stays in the cache as a degenerate texture (width 0),
		// never as a null: callers dereference the result, and the hot-reload
		// retry below is what makes "not authored yet" recover when the file
		// appears.
		Ref<Texture> texture = loaded ? created : CreateRef<EmptyTexture>();

		if (!hadEntry && !loaded)
			FG_CORE_ERROR("Renderer: failed to load PBR albedo texture '{0}'", path);

		if (hadEntry && !loaded)
		{
			// Keep the texture that works; record the new signature so the
			// broken state is not re-reloaded every frame.
			FG_CORE_WARN("Renderer: reload of PBR albedo texture '{0}' failed; keeping previous texture", path);
			cached->second.Signature = current;
			return cached->second.Albedo;
		}

		if (hadEntry)
			FG_CORE_INFO("Renderer: reloaded PBR albedo texture '{0}'", path);

		(void)previous;  // the renderer's upload cache is keyed by the texture
		// object itself, so the replacement - a new object - uploads afresh on
		// its first draw and the old one's cache entry simply goes unused.

		AlbedoCacheEntry entry;
		entry.Albedo = texture;
		entry.Signature = current;
		s_AlbedoTextureCache[path] = entry;
		return texture;
	}

	void Renderer::ApplyFrameSize(uint32_t width, uint32_t height)
	{
		s_Width = width;
		s_Height = height;

		Vulkan::VkRenderer& renderer = Vulkan::VkRenderer::Get();
		if (renderer.IsReady())
			(void)renderer.CreateTargets(width, height, ScaledSize(width), ScaledSize(height));
	}

	void Renderer::OnWindowResize(uint32_t width, uint32_t height)
	{
		// The window's new size is recorded here, and the swapchain and the
		// render targets are rebuilt from it once the size has settled. The
		// Final pass viewport follows the swapchain extent, which the renderer
		// reads from the swapchain itself.
		//
		// Ignoring the size here is what left the frame rendering at the
		// window's old size: the frame size was only corrected when a
		// swapchain happened to fail, and a driver that keeps handing out the
		// old images never fails.
		//
		// The size is in pixels - the framebuffer's, which is the size of the
		// image that is presented. On a scaled display it is not the window's
		// size in points, and building the targets at the point size is what
		// left the overlay drawn at the wrong scale and spilling out of the
		// window it was drawn into.
		if (width == 0 || height == 0)
			return;

		s_WindowWidth = width;
		s_WindowHeight = height;
		if (Vulkan::VkRenderer::Get().HasSwapchain())
			s_SwapchainNeedsRecreate = true;
	}

	void Renderer::BeginScene()
	{
		// The frame's command buffer is begun by AcquireSwapchainImage; the
		// camera upload and target clear are recorded by RenderScene.
	}

	void Renderer::EndScene()
	{
		Vulkan::VkRenderer::Get().EndScene();
	}

	bool Renderer::HasSwapchain()
	{
		return Vulkan::VkRenderer::Get().HasSwapchain();
	}

	uint32_t Renderer::AcquireSwapchainImage()
	{
		Vulkan::VkRenderer& renderer = Vulkan::VkRenderer::Get();
		if (!renderer.HasSwapchain())
			return UINT32_MAX;

		// A resize is rebuilt before the frame is acquired, not after a failed
		// acquire. Waiting for the acquire to fail leaves the swapchain and the
		// render targets at the size the window had before, and whether the
		// acquire fails is the driver's business: a driver that keeps handing
		// out the old swapchain's images never reports the resize at all, and
		// the scene is drawn into a corner of the window with the rest of it
		// black. The window's size is a fact, not something to infer from a
		// failure.
		//
		// It is rebuilt once the size has settled, though, and not on every
		// event: a drag-resize delivers a size per frame, and rebuilding the
		// swapchain and every offscreen target on each of them is a
		// hundred-megabyte allocation per frame on a high-resolution display.
		// The GPU is waited on for each of those rebuilds, so the whole thing
		// becomes a stall per frame - the screen flashes, and the driver
		// eventually gives up on the queue. One rebuild when the drag stops is
		// the same picture without the churn.
		const bool sizeSettled = s_WindowWidth == s_SettledWidth &&
			s_WindowHeight == s_SettledHeight;

		// A swapchain that is not the size the window is draws the frame
		// into a corner of the window with the rest of it black, so the size
		// is checked against the swapchain itself as well as against the
		// flag. The flag records that a resize arrived; the swapchain's own
		// extent is the fact, and a fact that disagrees with the window is
		// rebuilt whatever the flag says.
		const bool swapchainIsWrongSize =
			renderer.GetPresentWidth() != s_WindowWidth ||
			renderer.GetPresentHeight() != s_WindowHeight;

		if ((s_SwapchainNeedsRecreate || swapchainIsWrongSize) && sizeSettled)
		{
			s_SwapchainNeedsRecreate = false;
			if (renderer.RecreateSwapchain(s_WindowWidth, s_WindowHeight))
				ApplyFrameSize(renderer.GetPresentWidth(), renderer.GetPresentHeight());
		}
		s_SettledWidth = s_WindowWidth;
		s_SettledHeight = s_WindowHeight;

		uint32_t imageIndex = renderer.BeginFrame();

		// A swapchain that went out of date on its own - the surface moved
		// under it - is rebuilt too, so the next frame has a usable
		// presentation target. The rebuilt swapchain clamps itself to the
		// surface extent, so the render targets follow *it*, or every pass
		// would set a viewport that does not match the attachment it draws
		// into.
		if (imageIndex == UINT32_MAX && renderer.SwapchainNeedsRebuild())
		{
			if (renderer.RecreateSwapchain(s_WindowWidth, s_WindowHeight))
			{
				ApplyFrameSize(renderer.GetPresentWidth(), renderer.GetPresentHeight());
				imageIndex = renderer.BeginFrame();
			}
		}

		s_CurrentDiagImage = imageIndex;
		return imageIndex;
	}

	void Renderer::PresentSwapchainImage(uint32_t imageIndex)
	{
		if (imageIndex == UINT32_MAX)
			return;

		if (s_DiagEnded)
		{
			s_DiagEnded = false;
			return;
		}

		Vulkan::VkRenderer::Get().EndFrame(imageIndex);

		// The present can say that the swapchain has gone out of date - the
		// surface moved under it. That sets the flag; it never clears it.
		//
		// Assigning the present's answer to the flag threw a resize away:
		// the flag was set when the resize event arrived, the rebuild was
		// deferred by one frame (a size that has not settled is not worth
		// rebuilding for), and the present at the end of that frame
		// overwrote the flag with "the swapchain is fine" - which on X11 it
		// is, because the X11 presentation path does not notice a resize.
		// The flag was then false when the rebuild came round, the
		// swapchain stayed at the size it started at, and the window showed
		// the old image in the corner with the rest of it black.
		if (Vulkan::VkRenderer::Get().SwapchainNeedsRebuild())
			s_SwapchainNeedsRecreate = true;
	}

	void Renderer::BeginShadowPass(class Scene* scene, const glm::mat4& shadowViewProjection)
	{
		Vulkan::VkRenderer& renderer = Vulkan::VkRenderer::Get();
		if (!renderer.IsReady() || !scene)
			return;

		// The transform arrives from RenderScene, which builds it once per
		// frame: the map and the lookup must be the same projection.
		renderer.BeginShadowScene(shadowViewProjection);

		auto meshEntities = scene->GetAllEntitiesWith<MeshComponent, TransformComponent>();
		for (auto handle : meshEntities)
		{
			const auto& meshComponent = meshEntities.get<MeshComponent>(handle);
			const auto& transform = meshEntities.get<TransformComponent>(handle);

			Ref<Mesh> mesh = meshComponent.Mesh;
			if (!mesh)
				continue;

			glm::mat4 model = glm::translate(glm::mat4(1.0f), transform.Translation)
				* glm::toMat4(glm::quat(transform.Rotation))
				* glm::scale(glm::mat4(1.0f), transform.Scale);

			renderer.DrawShadowObject(mesh, model);
		}
	}

	void Renderer::EndShadowPass()
	{
		// Shadow pass is complete; the shadow map is now ready for the PBR pass.
		// The render pass closes when the next pass opens (the renderer tracks
		// the active pass), so there is nothing to record here.
	}

	void Renderer::BeginSSAOPass()
	{
		FG_PROFILE_FUNCTION();
		Vulkan::VkRenderer::Get().RenderSSAOPass(s_LastViewProjection);


		// Diagnostic: re-render the scene *after* the SSAO pass closed the
		// HDR pass, then re-run SSAO. The HDR capture reads the last
		// submitted frame: a lit scene there means the PBR draws land.
		if (std::getenv("FG_DIAG_RENDER_AFTER") && s_DiagReplay)
		{
			Vulkan::VkRenderer::Get().BeginScene(s_DiagCamera);
			s_DiagReplay();
			Vulkan::VkRenderer::Get().EndScene();
			s_DiagEnded = true;
		}

		// Diagnostic: end the frame's recording, submit and idle, so the
		// capture below reads this frame's work.
		if (std::getenv("FG_DIAG_SUBMIT_FIRST"))
		{
			Vulkan::VkRenderer::Get().EndScene();
			Vulkan::VkRenderer::Get().SubmitFrameOnly();
		}

		// Diagnostic: the SSAO pipeline, drawn into the SSAO target,
		// recorded a second time in this frame.
		if (std::getenv("FG_DIAG_SECOND_SSAO"))
		{
			Vulkan::VkRenderer::Get().ClearHdrTarget();
			Vulkan::VkRenderer::Get().RenderSSAOPass(s_LastViewProjection);

		if (std::getenv("FG_DIAG_SKIP_POST"))
		{
			Vulkan::VkRenderer::Get().ClearHdrTargetRed();
			Vulkan::VkRenderer::Get().BeginScene(s_DiagCamera);
			s_DiagReplay();
			Vulkan::VkRenderer::Get().EndScene();
			s_DiagEnded = true;
			return;
		}

		// Diagnostic: re-render the scene *after* the SSAO pass closed the
		// HDR pass, then re-run SSAO. The HDR capture reads the last
		// submitted frame: a lit scene there means the PBR draws land.
		if (std::getenv("FG_DIAG_RENDER_AFTER") && s_DiagReplay)
		{
			Vulkan::VkRenderer::Get().BeginScene(s_DiagCamera);
			s_DiagReplay();
			Vulkan::VkRenderer::Get().EndScene();
			s_DiagEnded = true;
		}

		// Diagnostic: end the frame's recording, submit and idle, so the
		// capture below reads this frame's work.
		if (std::getenv("FG_DIAG_SUBMIT_FIRST"))
		{
			Vulkan::VkRenderer::Get().EndScene();
			Vulkan::VkRenderer::Get().SubmitFrameOnly();
		}
		}

		// Diagnostic: draw the SSAO pipeline (known to rasterize) into the
		// RGBA16F tonemap target via a compatible dynamic render pass.
		if (std::getenv("FG_DIAG_DYNAMIC_COLOR16"))
			Vulkan::VkRenderer::Get().DiagDrawSsaoIntoTonemap();

		// Diagnostic: clear the HDR target with a bright red clear.
		if (const char* diag = std::getenv("FG_DIAG_RED_HDR"); diag && *diag)
		{
			Vulkan::VkRenderer::Get().ClearHdrTargetRed();
		}

		// Diagnostic: the depth target (D32 read as a byte per channel).
		if (const char* diag = std::getenv("FG_DIAG_CAPTURE_DEPTH"); diag && *diag)
			(void)Vulkan::VkRenderer::Get().CaptureTargetImage(
				Vulkan::VkRenderer::Get().GetDepthImage(),
				Vulkan::VkRenderer::Get().GetRenderWidth(), Vulkan::VkRenderer::Get().GetRenderHeight(),
				false, diag);

		// Diagnostic: an unlit clear of the HDR target, recorded after the
		// SSAO pass has closed it. A visible clear proves the HDR target,
		// the tonemap chain and the clear path are all healthy; a black
		// tonemap then isolates the tonemap draw itself.
		if (const char* diag = std::getenv("FG_DIAG_CLEAR_HDR"); diag && *diag)
		{
			Vulkan::VkRenderer::Get().ClearHdrTarget();
			Vulkan::VkRenderer::Get().RenderTonemapPass();
			(void)Vulkan::VkRenderer::Get().CaptureTargetImage(
				Vulkan::VkRenderer::Get().GetTonemapImage(),
				Vulkan::VkRenderer::Get().GetPresentWidth(), Vulkan::VkRenderer::Get().GetPresentHeight(),
				true, diag);
		}
	}

	void Renderer::EndSSAOPass()
	{
	}

	void Renderer::BeginBloomPass()
	{
		FG_PROFILE_FUNCTION();
		Vulkan::VkRenderer::Get().RenderBloomPass();
	}

	void Renderer::EndBloomPass()
	{
	}

	void Renderer::SetBloomIntensity(float intensity)
	{
		Vulkan::VkRenderer::SetBloomIntensity(intensity);
	}

	float Renderer::GetBloomIntensity()
	{
		return Vulkan::VkRenderer::GetBloomIntensity();
	}

	void Renderer::BeginHDRPass()
	{
		FG_PROFILE_FUNCTION();
		Vulkan::VkRenderer::Get().RenderTonemapPass();
	}

	void Renderer::EndHDRPass()
	{
	}

	void Renderer::BeginFinalPass()
	{
		FG_PROFILE_FUNCTION();
		Vulkan::VkRenderer::Get().RenderFinalPass();

		// Diagnostic: re-clear + re-render the scene, then end and submit the
		// frame here. The last submitted HDR content is then the PBR draws
		// themselves - the HDR capture reads them directly.
		if (std::getenv("FG_DIAG_SKIP_POST"))
		{
			Vulkan::VkRenderer::Get().ClearHdrTargetRed();
			Vulkan::VkRenderer::Get().BeginScene(s_DiagCamera);
			s_DiagReplay();
			Vulkan::VkRenderer::Get().EndScene();
			s_DiagEnded = true;
			return;
		}

		// Diagnostic: the Final pass output, captured mid-frame.
		if (const char* diag = std::getenv("FG_DIAG_CAPTURE_FINAL"); diag && *diag)
		{
			const uint32_t idx = Vulkan::VkRenderer::Get().CurrentImageIndex();
			(void)Vulkan::VkRenderer::Get().CaptureSwapchainImage(idx, diag);
		}

		// Diagnostic knob (not an engine feature): dump the tonemapped target
		// to bisect the pass chain on a software driver.
		if (const char* diag = std::getenv("FG_DIAG_CAPTURE_BLOOM"); diag && *diag)
			(void)Vulkan::VkRenderer::Get().CaptureTargetImage(
				Vulkan::VkRenderer::Get().GetBloomHalfImage(),
				Vulkan::VkRenderer::Get().GetRenderWidth() / 2, Vulkan::VkRenderer::Get().GetRenderHeight() / 2,
				true, diag);
		if (const char* diag = std::getenv("FG_DIAG_CAPTURE_SSAO"); diag && *diag)
			(void)Vulkan::VkRenderer::Get().CaptureTargetImage(
				Vulkan::VkRenderer::Get().GetSsaoImage(),
				Vulkan::VkRenderer::Get().GetRenderWidth(), Vulkan::VkRenderer::Get().GetRenderHeight(),
				false, diag);
		if (const char* diag = std::getenv("FG_DIAG_CAPTURE_HDR"); diag && *diag)
			(void)Vulkan::VkRenderer::Get().CaptureTargetImage(
				Vulkan::VkRenderer::Get().GetHdrImage(),
				Vulkan::VkRenderer::Get().GetRenderWidth(), Vulkan::VkRenderer::Get().GetRenderHeight(),
				true, diag);
		if (const char* diag = std::getenv("FG_DIAG_CAPTURE_TONEMAP"); diag && *diag)
			(void)Vulkan::VkRenderer::Get().CaptureTargetImage(
				Vulkan::VkRenderer::Get().GetTonemapImage(),
				Vulkan::VkRenderer::Get().GetPresentWidth(), Vulkan::VkRenderer::Get().GetPresentHeight(),
				true, diag);
	}

	void Renderer::EndFinalPass()
	{
	}

	// ---------------------------------------------------------------------------
	// ImGui overlay accessors
	// ---------------------------------------------------------------------------

	void* Renderer::GetVulkanInstanceHandle()
	{
		return reinterpret_cast<void*>(Vulkan::VkRenderer::Get().GetInstanceHandle());
	}

	void* Renderer::GetVulkanPhysicalDeviceHandle()
	{
		return reinterpret_cast<void*>(Vulkan::VkRenderer::Get().GetPhysicalDeviceHandle());
	}

	void* Renderer::GetVulkanDeviceHandle()
	{
		return reinterpret_cast<void*>(Vulkan::VkRenderer::Get().GetLogicalDeviceHandle());
	}

	void* Renderer::GetVulkanGraphicsQueueHandle()
	{
		return reinterpret_cast<void*>(Vulkan::VkRenderer::Get().GetGraphicsQueueHandle());
	}

	uint32_t Renderer::GetVulkanGraphicsQueueIndex()
	{
		return Vulkan::VkRenderer::Get().GetGraphicsQueueIndex();
	}

	uint32_t Renderer::GetSwapchainImageCount()
	{
		return Vulkan::VkRenderer::Get().GetImageCount();
	}

	uint32_t Renderer::GetSwapchainVkFormat()
	{
		return static_cast<uint32_t>(Vulkan::VkRenderer::Get().GetSwapchainFormat());
	}

	void* Renderer::GetCurrentCommandBuffer()
	{
		return reinterpret_cast<void*>(Vulkan::VkRenderer::Get().CurrentCommandBuffer());
	}

	void* Renderer::GetPresentRenderPass()
	{
		return reinterpret_cast<void*>(Vulkan::VkRenderer::Get().GetPresentRenderPass());
	}

	void* Renderer::GetCompositedTextureView()
	{
		return reinterpret_cast<void*>(Vulkan::VkRenderer::Get().GetTonemapView());
	}

}

#endif // FG_NO_VULKAN_RUNTIME
