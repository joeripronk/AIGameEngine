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

	// --- Playfield ---
	//
	// The engine draws an entity only when it carries a MeshComponent; the
	// default-cube fallback covers a MeshComponent whose mesh pointer is null,
	// not a missing component. This entity had no MeshComponent at all, so it
	// was skipped entirely and the playfield was invisible.
	//
	// The backdrop is also kept well clear of the clear colour. The renderer
	// clears to (0.05, 0.06, 0.09); a slab at the same albedo lands on the same
	// pixel value after tonemapping, which is another way for the board to be
	// invisible without anything reporting an error.
	const glm::vec3 boardSize = BoardView::BoardSize();

	auto backdrop = m_Scene->CreateEntity("BoardBackdrop");
	auto& backdropTransform = backdrop.GetComponent<Forge::TransformComponent>();
	backdropTransform.Translation = centre + glm::vec3(0.0f, 0.0f, -0.4f);
	backdropTransform.Scale = glm::vec3(boardSize.x + 0.4f, boardSize.y + 0.4f, 0.4f);
	backdrop.AddComponent<Forge::MeshComponent>().ModelPath = kBlockMesh;
	auto& backdropMaterial = backdrop.AddComponent<Forge::PBRMaterialComponent>();
	backdropMaterial.Albedo = { 0.010f, 0.012f, 0.025f };
	backdropMaterial.Roughness = 0.9f;

	// A border, so the 10x20 well is legible as a playfield rather than as a
	// dark rectangle. Four thin slabs around the well; the bottom one is flush
	// with the floor, the other three stand slightly proud of it.
	//
	// Thick enough to survive being projected onto a few hundred pixels, and
	// sitting in front of the backdrop rather than inside it: the backdrop is
	// 0.4 deep and centred at z = -0.4, so its front face is at -0.2, and a
	// border behind that plane is hidden by the slab it is meant to outline.
	const float border = 0.20f;
	const float borderZ = -0.15f;
	const glm::vec3 borderColour = { 0.30f, 0.32f, 0.38f };

	const auto addBorder = [this, centre, boardSize, border, borderColour](
		const char* name, const glm::vec3& offset, const glm::vec3& scale)
	{
		auto slab = m_Scene->CreateEntity(name);
		auto& transform = slab.GetComponent<Forge::TransformComponent>();
		transform.Translation = centre + offset;
		transform.Scale = scale;
		slab.AddComponent<Forge::MeshComponent>().ModelPath = kBlockMesh;
		auto& material = slab.AddComponent<Forge::PBRMaterialComponent>();
		material.Albedo = borderColour;
		material.Roughness = 0.6f;
	};

	const float halfW = boardSize.x * 0.5f;
	const float halfH = boardSize.y * 0.5f;

	addBorder("BoardBorderLeft",
		glm::vec3(-halfW - border, 0.0f, borderZ),
		glm::vec3(border, boardSize.y + border * 2.0f, 0.3f));
	addBorder("BoardBorderRight",
		glm::vec3(halfW + border, 0.0f, borderZ),
		glm::vec3(border, boardSize.y + border * 2.0f, 0.3f));
	addBorder("BoardBorderTop",
		glm::vec3(0.0f, halfH + border, borderZ),
		glm::vec3(boardSize.x + border * 2.0f, border, 0.3f));
	addBorder("BoardBorderFloor",
		glm::vec3(0.0f, -halfH - border, borderZ),
		glm::vec3(boardSize.x + border * 2.0f, border, 0.3f));

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
			// RenderScene fills the offscreen HDR target; the post chain is what
			// actually reaches the swapchain image. Without it the window stays
			// black no matter what the scene contains.
			Forge::Renderer::RenderScene(m_Scene.get(), swapchainImage);
			Forge::Renderer::BeginSSAOPass();
			Forge::Renderer::BeginHDRPass();
			Forge::Renderer::BeginFinalPass();
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