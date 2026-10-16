#include "fgpch.h"

#include "TetrisApp.h"
#include "TetrisInputLayer.h"

#include "Forge/Core/Log.h"
#include "Forge/Events/ApplicationEvent.h"
#include "Forge/Renderer/Renderer.h"
#include "Forge/Scene/Components.h"

#include <GLFW/glfw3.h>

#include <cstdlib>
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

}  // namespace

TetrisApp::TetrisApp(const Forge::ApplicationCommandLineArgs& args)
	: Forge::Application(Forge::ApplicationSpecification{ "Forge Tetris", "", args })
{
	Forge::Renderer::Init();

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

	// Input is a layer so it sees every key event the window reports, rather
	// than a polled subset.
	m_InputLayer = std::make_unique<InputLayer>(m_BoardView.GetManager());
	PushLayer(m_InputLayer.get());

	FG_INFO("Tetris: up. Arrows or WASD to move, Up/X to rotate, Space to drop, P to pause, R to restart.");
}

TetrisApp::~TetrisApp()
{
	// Unhook the layer before it can be reached through: it points into the
	// board view, and the stack only erases, it does not delete.
	if (m_InputLayer)
	{
		GetLayerStack().PopLayer(m_InputLayer.get());
		m_InputLayer.reset();
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

bool TetrisApp::CreateScene()
{
	m_Scene = Forge::CreateRef<Forge::Scene>();

	// --- Camera ---
	//
	// Framed on the playfield: far enough back that 20 rows fit vertically at
	// this field of view.
	const glm::vec3 centre = BoardView::BoardCentre();
	auto camera = m_Scene->CreateEntity("MainCamera");
	camera.GetComponent<Forge::TransformComponent>().Translation = centre + glm::vec3(0.0f, 0.0f, 22.0f);
	auto& cameraComponent = camera.AddComponent<Forge::CameraComponent>();
	cameraComponent.Primary = true;
	cameraComponent.FieldOfView = 60.0f;
	cameraComponent.AspectRatio = 16.0f / 9.0f;
	cameraComponent.UpdateProjection();

	// --- Light ---
	auto light = m_Scene->CreateEntity("DirectionalLight");
	auto& lightComponent = light.AddComponent<Forge::LightComponent>();
	lightComponent.Type = Forge::LightComponent::Type::Directional;
	lightComponent.Intensity = 1.2f;
	lightComponent.Direction = { -0.4f, -0.6f, -0.7f };

	// --- Backdrop ---
	//
	// A dark slab behind the blocks, so empty cells read as empty. A material
	// with no mesh is drawn as the engine's default cube, so no model is needed.
	auto backdrop = m_Scene->CreateEntity("BoardBackdrop");
	auto& backdropTransform = backdrop.GetComponent<Forge::TransformComponent>();
	backdropTransform.Translation = centre + glm::vec3(0.0f, 0.0f, -0.4f);
	const glm::vec3 boardSize = BoardView::BoardSize();
	backdropTransform.Scale = glm::vec3(boardSize.x + 0.4f, boardSize.y + 0.4f, 0.4f);
	auto& backdropMaterial = backdrop.AddComponent<Forge::PBRMaterialComponent>();
	backdropMaterial.Albedo = { 0.05f, 0.06f, 0.09f };
	backdropMaterial.Roughness = 0.9f;

	// --- Next-piece preview ---
	m_PreviewRoot = m_Scene->CreateEntity("NextPiecePreview");
	m_PreviewRoot.GetComponent<Forge::TransformComponent>().Translation =
		centre + glm::vec3(BoardView::BoardSize().x * 0.5f + kPreviewGap + kPreviewColumns * 0.5f,
			BoardView::BoardSize().y * 0.5f - 2.0f, 0.0f);

	return true;
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
			0.0f);

		const auto cached = m_PreviewBlocks.find(key);
		if (cached == m_PreviewBlocks.end())
		{
			Forge::Entity entity = m_Scene->CreateEntity("PreviewBlock_" + std::to_string(i));
			auto& transform = entity.GetComponent<Forge::TransformComponent>();
			transform.Translation = m_PreviewRoot.GetComponent<Forge::TransformComponent>().Translation + local;
			transform.Scale = glm::vec3(kPreviewBlockScale);

			auto& material = entity.AddComponent<Forge::PBRMaterialComponent>();
			material.Albedo = TetrisManager::GetTetrominoDefinition(shown).color;
			material.Metallic = 0.1f;
			material.Roughness = 0.35f;

			entity.AddComponent<Forge::MeshComponent>().ModelPath = kBlockMesh;

			m_PreviewBlocks.emplace(key, entity);
		}
		else
		{
			cached->second.GetComponent<Forge::PBRMaterialComponent>().Albedo =
				TetrisManager::GetTetrominoDefinition(shown).color;
		}
	}
}

void TetrisApp::Run()
{
	float lastFrameTime = static_cast<float>(glfwGetTime());

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
		const float time = static_cast<float>(glfwGetTime());
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
			Forge::Renderer::RenderScene(m_Scene.get(), swapchainImage);
			Forge::Renderer::EndScene();

			// Read the frame back when asked, so a verification script has
			// something to look at.
			if (const char* capturePath = std::getenv("FG_CAPTURE_PATH"))
			{
				if (!Forge::Renderer::CaptureSwapchainImage(swapchainImage, capturePath))
					FG_ERROR("Tetris: failed to capture frame to '{0}'", capturePath);
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