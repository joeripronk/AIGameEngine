#include "fgpch.h"
#include "Forge/Debug/Instrumentor.h"

#include "TetrisApp.h"

#include "Forge/Core/Log.h"
#include "Forge/Core/Time.h"
#include "Forge/Events/ApplicationEvent.h"
#include "Forge/Renderer/Renderer.h"
#include "Forge/Scene/Components.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <string>

namespace Tetris {

namespace {

// The preview sits to the right of the playfield. It is laid out on a 4x4
// footprint - the largest bounding box any rotation of any piece needs - so a
// piece stays centred in it whatever shape it has.
constexpr float kPreviewColumns = 4.0f;
constexpr float kPreviewRows = 4.0f;
constexpr float kPreviewGap = 2.0f;
constexpr float kPreviewBlockScale = 0.92f;

const char* kBlockMesh = "resources/models/cube.glb";

// Euler radians for a camera transform whose -Z axis points from `eye` at
// `target`. Scene::RenderScene builds the view matrix as
// inverse(translate * toMat4(quat(rotation))) with rotation (pitch, yaw, 0),
// so the forward vector is Ry(yaw) * Rx(pitch) * (0,0,-1)
// = (-sin(yaw)cos(pitch), sin(pitch), -cos(yaw)cos(pitch)).
glm::vec3 CameraOrientation(const glm::vec3& eye, const glm::vec3& target)
{
	const glm::vec3 forward = glm::normalize(target - eye);
	const float pitch = std::asin(std::clamp(forward.y, -1.0f, 1.0f));
	const float yaw = std::atan2(-forward.x, -forward.z);
	return glm::vec3(pitch, yaw, 0.0f);
}

}  // namespace

TetrisApp::TetrisApp(const Forge::ApplicationCommandLineArgs& args)
	: Forge::Application(Forge::ApplicationSpecification{ "Forge Tetris", "", args })
{
	// Supersample the offscreen pass so Tetris blocks read with crisper edges
	// without asking the engine to know this is Tetris.
	Forge::Renderer::SetRenderScale(2.0f);
	Forge::Renderer::Init();

	// Mirror-polish the blocks: redraw the scene from the playfield centre into
	// a cubemap each frame so the metal surfaces reflect the well, the border
	// and the block stack. A missing device just means unreflected blocks.
	if (Forge::Renderer::EnableReflectionProbe())
		Forge::Renderer::SetReflectionProbePosition(BoardView::BoardCentre() + glm::vec3(0.0f, 0.0f, 1.2f));

	if (!ParseCommandLineMode(args))
	{
		FG_INFO("Tetris: usage: ForgeTetris [--mode 2d|3d]");
		Close();
		return;
	}

	if (!CreateScene())
	{
		FG_ERROR("Tetris: could not build the scene; nothing to play");
		Close();
		return;
	}

	if (!m_BoardView.Initialize(m_Scene, kBlockMesh))
	{
		// A failure here would otherwise look like a game that starts and then
		// does nothing, so say so and stop rather than showing an empty window.
		FG_ERROR("Tetris: could not attach the board view to the scene");
		Close();
		return;
	}

	// Input and overlay are combined into a single layer so the window
	// receives events and pushes ImGui calls from one object.
	m_InputOverlayLayer = std::make_unique<InputOverlayLayer>(m_BoardView.GetManager());
	m_InputOverlayLayer->SetViewToggledCallback([this] { ToggleViewMode(); });
	PushLayer(m_InputOverlayLayer.get());

	if (!InitImGui())
		FG_INFO("Tetris: continuing without the on-screen controls");

	FG_INFO("Tetris: up. Arrows or WASD to move, Up/X to rotate, Space to drop, P to pause, R to restart, V to switch 2D/3D.");
}

TetrisApp::~TetrisApp()
{
	ShutdownImGui();

	// Unhook the layer before it can be reached through: it points into the
	// board view, and the stack only erases, it does not delete.
	if (m_InputOverlayLayer)
	{
		GetLayerStack().PopLayer(m_InputOverlayLayer.get());
		m_InputOverlayLayer.reset();
	}

	m_BoardView.Shutdown();

	for (auto& [key, entity] : m_PreviewBlocks)
	{
		(void)key;
		if (m_Scene && entity)
			m_Scene->DestroyEntity(entity);
	}
	m_PreviewBlocks.clear();

	if (m_Scene && m_PreviewRoot)
		m_Scene->DestroyEntity(m_PreviewRoot);
}

bool TetrisApp::ParseCommandLineMode(const Forge::ApplicationCommandLineArgs& args)
{
	m_ThreeDimensional = false;

	for (int i = 1; i < args.Count; ++i)
	{
		std::string arg = args[i];
		std::transform(arg.begin(), arg.end(), arg.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		if (arg == "--3d" || arg == "-3d" || arg == "3d")
			m_ThreeDimensional = true;
		else if (arg == "--2d" || arg == "-2d" || arg == "2d")
			m_ThreeDimensional = false;
		else if (arg == "--mode")
		{
			if (++i >= args.Count)
			{
				FG_ERROR("Tetris: --mode needs an argument: 2d or 3d");
				return false;
			}

			arg = args[i];
			std::transform(arg.begin(), arg.end(), arg.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

			if (arg == "2d")
				m_ThreeDimensional = false;
			else if (arg == "3d")
				m_ThreeDimensional = true;
			else
			{
				FG_ERROR("Tetris: unknown mode '{0}' (expected 2d or 3d)", arg);
				return false;
			}
		}
		else if (arg == "--help" || arg == "-h")
		{
			FG_INFO("Tetris: usage: ForgeTetris [--mode 2d|3d] [--2d] [--3d]");
			return false;
		}
		else if (!arg.empty() && arg[0] == '-')
		{
			FG_ERROR("Tetris: unknown option '{0}'", arg);
			return false;
		}
	}

	return true;
}

bool TetrisApp::CreateScene()
{
	m_Scene = Forge::CreateRef<Forge::Scene>();

	// --- Camera ---
	//
	// Framed on the playfield; ApplyViewLayout places it for the mode.
	m_CameraEntity = m_Scene->CreateEntity("MainCamera");
	auto& cameraComponent = m_CameraEntity.AddComponent<Forge::CameraComponent>();
	cameraComponent.Primary = true;
	cameraComponent.AspectRatio = 16.0f / 9.0f;

	// --- Light ---
	m_LightEntity = m_Scene->CreateEntity("DirectionalLight");
	auto& lightComponent = m_LightEntity.AddComponent<Forge::LightComponent>();
	lightComponent.Type = Forge::LightComponent::Type::Directional;

	ApplyViewLayout();
	CreateDecor();

	return true;
}

void TetrisApp::ApplyViewLayout()
{
	const glm::vec3 centre = BoardView::BoardCentre();

	auto& cameraTransform = m_CameraEntity.GetComponent<Forge::TransformComponent>();
	auto& cameraComponent = m_CameraEntity.GetComponent<Forge::CameraComponent>();

	if (m_ThreeDimensional)
	{
		const glm::vec3 eye = centre + glm::vec3(8.5f, 5.0f, 20.5f);
		const glm::vec3 target = centre + glm::vec3(-1.5f, 0.5f, -0.5f);
		cameraTransform.Translation = eye;
		cameraTransform.Rotation = CameraOrientation(eye, target);
		cameraComponent.FieldOfView = 65.0f;
	}
	else
	{
		cameraTransform.Translation = centre + glm::vec3(0.0f, 0.0f, 22.0f);
		cameraTransform.Rotation = { 0.0f, 0.0f, 0.0f };
		cameraComponent.FieldOfView = 60.0f;
	}

	cameraComponent.UpdateProjection();

	auto& lightComponent = m_LightEntity.GetComponent<Forge::LightComponent>();
	if (m_ThreeDimensional)
	{
		lightComponent.Intensity = 1.65f;
		lightComponent.Direction = glm::normalize(glm::vec3(0.45f, 0.45f, 0.75f));
	}
	else
	{
		lightComponent.Intensity = 1.25f;
		lightComponent.Direction = glm::normalize(glm::vec3(0.20f, 0.20f, 0.95f));
	}
}

void TetrisApp::CreateDecor()
{
	const glm::vec3 centre = BoardView::BoardCentre();
	const glm::vec3 boardSize = BoardView::BoardSize();
	const float halfW = boardSize.x * 0.5f;
	const float halfH = boardSize.y * 0.5f;

	const auto addSlab = [this, centre](const char* name, const glm::vec3& offset, const glm::vec3& scale, const glm::vec3& albedo, float roughness)
	{
		auto slab = m_Scene->CreateEntity(name);
		auto& transform = slab.GetComponent<Forge::TransformComponent>();
		transform.Translation = centre + offset;
		transform.Scale = scale;
		slab.AddComponent<Forge::MeshComponent>().ModelPath = kBlockMesh;
		auto& material = slab.AddComponent<Forge::PBRMaterialComponent>();
		material.Albedo = albedo;
		material.Roughness = roughness;
		m_DecorEntities.push_back(slab);
	};

	addSlab("BoardBackdrop", glm::vec3(0.0f, 0.0f, -4.2f), glm::vec3(boardSize.x + 1.4f, boardSize.y + 1.4f, 0.4f), { 0.018f, 0.024f, 0.042f }, 0.95f);

	if (m_ThreeDimensional)
	{
		addSlab("WellBack", glm::vec3(0.0f, 0.0f, -4.4f), glm::vec3(boardSize.x + 1.2f, boardSize.y + 1.2f, 0.2f), { 0.08f, 0.10f, 0.14f }, 0.75f);
		addSlab("WellLeft", glm::vec3(-halfW - 0.35f, 0.0f, -2.0f), glm::vec3(0.3f, boardSize.y + 0.7f, 1.3f), { 0.12f, 0.14f, 0.18f }, 0.75f);
		addSlab("WellRight", glm::vec3(halfW + 0.35f, 0.0f, -2.0f), glm::vec3(0.3f, boardSize.y + 0.7f, 1.3f), { 0.16f, 0.18f, 0.22f }, 0.75f);
		addSlab("WellFloor", glm::vec3(0.0f, -halfH - 1.2f, -2.0f), glm::vec3(boardSize.x + 1.4f, 0.4f, 2.0f), { 0.18f, 0.20f, 0.24f }, 0.75f);
		addSlab("WellCeiling", glm::vec3(0.0f, halfH + 1.2f, -2.0f), glm::vec3(boardSize.x + 1.4f, 0.4f, 2.0f), { 0.10f, 0.12f, 0.16f }, 0.75f);

		auto tray = m_Scene->CreateEntity("PreviewTray");
		auto& trayTransform = tray.GetComponent<Forge::TransformComponent>();
		trayTransform.Translation =
			centre + glm::vec3(BoardView::BoardSize().x * 0.5f + kPreviewGap + kPreviewColumns * 0.5f,
				BoardView::BoardSize().y * 0.5f - 2.0f, -0.52f);
		trayTransform.Scale = glm::vec3(kPreviewColumns + 0.5f, kPreviewRows + 0.5f, 0.3f);
		tray.AddComponent<Forge::MeshComponent>().ModelPath = kBlockMesh;
		auto& trayMaterial = tray.AddComponent<Forge::PBRMaterialComponent>();
		trayMaterial.Albedo = { 0.11f, 0.13f, 0.17f };
		trayMaterial.Roughness = 0.85f;
		m_DecorEntities.push_back(tray);
	}
	else
	{
		const float border = 0.20f;
		const float borderZ = -0.2f;
		const glm::vec3 borderColour = { 0.30f, 0.32f, 0.38f };

		addSlab("BoardBorderLeft", glm::vec3(-halfW - border, 0.0f, borderZ), glm::vec3(border, boardSize.y + border * 2.0f, 0.3f), borderColour, 0.6f);
		addSlab("BoardBorderRight", glm::vec3(halfW + border, 0.0f, borderZ), glm::vec3(border, boardSize.y + border * 2.0f, 0.3f), borderColour, 0.6f);
		addSlab("BoardBorderTop", glm::vec3(0.0f, halfH + border, borderZ), glm::vec3(boardSize.x + border * 2.0f, border, 0.3f), borderColour, 0.6f);
		addSlab("BoardBorderFloor", glm::vec3(0.0f, -halfH - border, borderZ), glm::vec3(boardSize.x + border * 2.0f, border, 0.3f), borderColour, 0.6f);
	}

	m_PreviewRoot = m_Scene->CreateEntity("NextPiecePreview");
	auto& previewTransform = m_PreviewRoot.GetComponent<Forge::TransformComponent>();
	previewTransform.Translation =
		centre + glm::vec3(BoardView::BoardSize().x * 0.5f + kPreviewGap + kPreviewColumns * 0.5f,
			BoardView::BoardSize().y * 0.5f - 2.0f, -0.3f);
	m_DecorEntities.push_back(m_PreviewRoot);
}

void TetrisApp::DestroyDecor()
{
	if (!m_Scene)
	{
		m_DecorEntities.clear();
		return;
	}

	for (Forge::Entity& entity : m_DecorEntities)
	{
		if (entity)
			m_Scene->DestroyEntity(entity);
	}
	m_DecorEntities.clear();

	// The preview root is part of the decor; its blocks are recreated by
	// SyncPreview against the new root on the next frame.
	m_PreviewRoot = Forge::Entity();
}

void TetrisApp::ToggleViewMode()
{
	m_ThreeDimensional = !m_ThreeDimensional;

	DestroyDecor();
	ApplyViewLayout();
	CreateDecor();

	FG_INFO("Tetris: switched to {0} view", m_ThreeDimensional ? "3D" : "2D");
}

bool TetrisApp::InitImGui()
{
	// The engine owns the ImGui context and the GLFW/Vulkan backends; the
	// game just pushes the layer and drives its frame calls.
	m_ImGuiLayer = std::make_unique<Forge::ImGuiLayer>();
	m_ImGuiLayer->OnAttach();
	if (!m_ImGuiLayer->IsReady())
	{
		m_ImGuiLayer.reset();
		return false;
	}

	PushOverlay(m_ImGuiLayer.get());
	return true;
}

void TetrisApp::ShutdownImGui()
{
	if (!m_ImGuiLayer)
		return;

	GetLayerStack().PopOverlay(m_ImGuiLayer.get());
	m_ImGuiLayer->OnDetach();
	m_ImGuiLayer.reset();
}

void TetrisApp::SyncPreview()
{
	const TetrisGameState& state = m_BoardView.GetManager().GetState();

	// Four blocks of whatever is queued, laid out in its rotation-0 shape.
	const glm::ivec2 origin(
		-static_cast<int32_t>(kPreviewColumns) / 2,
		-static_cast<int32_t>(kPreviewRows) / 2);

	TetrominoType shown = TetrominoType::Count;
	if (state.CurrentState == TetrisGameState::State::Playing)
		shown = state.NextPiece.Type;

	glm::ivec2 blocks[4];
	if (shown != TetrominoType::Count)
		GetRotatedBlocks(shown, 0, blocks);

	for (int i = 0; i < 4; ++i)
	{
		const bool visible = shown != TetrominoType::Count;
		const uint64_t key = static_cast<uint64_t>(i);

		if (!visible)
		{
			const auto stale = m_PreviewBlocks.find(key);
			if (stale != m_PreviewBlocks.end())
			{
				m_Scene->DestroyEntity(stale->second);
				m_PreviewBlocks.erase(stale);
			}
			continue;
		}

		const glm::vec3 local(
			(static_cast<float>(blocks[i].x + origin.x) + 0.5f) * BoardView::BlockSize(),
			(static_cast<float>(blocks[i].y + origin.y) + 0.5f) * BoardView::BlockSize(),
			0.46f);

		const auto cached = m_PreviewBlocks.find(key);
		if (cached == m_PreviewBlocks.end())
		{
			Forge::Entity entity = m_Scene->CreateEntity("PreviewBlock_" + std::to_string(i));
			auto& transform = entity.GetComponent<Forge::TransformComponent>();
			transform.Translation = m_PreviewRoot.GetComponent<Forge::TransformComponent>().Translation + local;
			transform.Scale = glm::vec3(kPreviewBlockScale);

			auto& material = entity.AddComponent<Forge::PBRMaterialComponent>();
			material.Albedo = TetrisManager::GetTetrominoDefinition(shown).color;
			material.Metallic = kBlockMetallic;
			material.Roughness = kBlockRoughness;
			material.AO = 1.0f;
			material.AlbedoTexturePath = kBlockAlbedoTexture;
			material.Emissive = TetrisManager::GetTetrominoDefinition(shown).color;
			material.EmissiveIntensity = kBlockEmissiveIntensity;

			entity.AddComponent<Forge::MeshComponent>().ModelPath = kBlockMesh;

			m_PreviewBlocks.emplace(key, entity);
		}
		else
		{
			auto& cachedMaterial = cached->second.GetComponent<Forge::PBRMaterialComponent>();
			const glm::vec3 previewColour = TetrisManager::GetTetrominoDefinition(shown).color;
			cachedMaterial.Albedo = previewColour;
			cachedMaterial.Metallic = kBlockMetallic;
			cachedMaterial.Roughness = kBlockRoughness;
			cachedMaterial.AO = 1.0f;
			cachedMaterial.Emissive = previewColour;
			cachedMaterial.EmissiveIntensity = kBlockEmissiveIntensity;
		}
	}
}

void TetrisApp::Run()
{
	float lastFrameTime = static_cast<float>(Forge::Time::GetTime());

	// Headless verification (CI, screenshots) needs the process to end on its
	// own, because no window-close event ever arrives without a user. Unset by
	// default: a game that quits after three seconds is not a game.
	uint64_t autoExitFrames = 0;
	if (const char* frames = std::getenv("FG_AUTO_EXIT_FRAMES"))
	{
		const long long parsed = std::atoll(frames);
		if (parsed > 0)
			autoExitFrames = static_cast<uint64_t>(parsed);
	}

	while (IsRunning())
	{
		FG_PROFILE_SCOPE("Frame");
		const float time = static_cast<float>(Forge::Time::GetTime());
		const float frameTime = time - lastFrameTime;
		lastFrameTime = time;

		// Advance the game and mirror it into the scene before anything draws.
		m_BoardView.Update(frameTime);
		SyncPreview();
		if (!IsMinimized())
		{
			for (Forge::Layer* layer : GetLayerStack())
				layer->OnUpdate(Forge::Timestep(frameTime));

			const uint32_t swapchainImage = Forge::Renderer::AcquireSwapchainImage();
			Forge::Renderer::BeginScene();
			// RenderScene fills the offscreen HDR target; the post chain is what
			// actually reaches the swapchain image. Without it the window stays
			// black no matter what the scene contains.
			Forge::Renderer::RenderScene(m_Scene.get(), swapchainImage);
			Forge::Renderer::BeginSSAOPass();
			Forge::Renderer::BeginHDRPass();
			Forge::Renderer::BeginFinalPass();

			if (m_ImGuiLayer && m_ImGuiLayer->IsReady())
			{
				m_ImGuiLayer->NewFrame();

				for (Forge::Layer* layer : GetLayerStack())
					layer->OnImGuiRender();

				m_ImGuiLayer->Render();
			}

			Forge::Renderer::EndScene();

			// Read the frame back when asked, so a verification script has
			// something to look at.
			//
			// Once, not every frame. The capture opens the file and rewrites it
			// from scratch, so a per-frame capture leaves the file permanently
			// mid-rewrite: a reader sees a torn image, which is exactly what a
			// verification script cannot make sense of.
			if (const char* capturePath = std::getenv("FG_CAPTURE_PATH"))
			{
				if (m_Captured)
					(void)0;
				else if (!Forge::Renderer::CaptureSwapchainImage(swapchainImage, capturePath))
					FG_ERROR("Tetris: failed to capture frame to '{0}'", capturePath);
				else
					m_Captured = true;
			}

			Forge::Renderer::PresentSwapchainImage(swapchainImage);
		}

		GetWindowScope()->OnUpdate();
		ExecuteMainThreadQueue();

		++m_FrameCount;
		if (autoExitFrames != 0 && m_FrameCount >= autoExitFrames)
		{
			FG_INFO("Tetris: ran {0} frames", m_FrameCount);
			Close();
		}
	}

	FG_INFO("Tetris: final score {0}, {1} lines cleared",
		m_BoardView.GetManager().GetState().Score,
		m_BoardView.GetManager().GetState().LinesCleared);
}

}  // namespace Tetris

// The engine's entry point asks the client for its application. The symbol has
// to live in Forge, since that is where the declaration is; the game itself is
// entirely in namespace Tetris above.
namespace Forge {

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		return new Tetris::TetrisApp(args);
	}

}  // namespace Forge
