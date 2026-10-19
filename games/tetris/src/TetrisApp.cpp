#include "fgpch.h"
#include "Forge/Debug/Instrumentor.h"

#include "TetrisApp.h"

#include "TetrisAnimation.h"

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
#include <chrono>
#include <thread>

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

// The impact shake: a damped oscillation about the camera's base position, in
// world units (a block is 1.0). The vertical axis leads and the others trail,
// which is what makes it read as the ground moving rather than as the camera
// wobbling on its axis. The frequencies sit low - a ground shake is felt as a
// rumble - and the damping leaves a saturated strike about a second of it.
constexpr float kPi = 3.14159265358979323846f;
constexpr float kShakeMaxAmplitude = 0.45f;
constexpr float kShakeVerticalFrequency = 7.0f;    // Hz
constexpr float kShakeHorizontalFrequency = 4.3f;  // not a multiple: the axes do not march in step
constexpr float kShakeDepthFrequency = 3.1f;
constexpr float kShakeDamping = 5.0f;              // per second
constexpr float kShakeSettle = 0.003f;             // below this the camera is home
constexpr float kShakeHorizontalRatio = 0.45f;
constexpr float kShakeDepthRatio = 0.25f;

// How much of the fire meter a strike feeds it: the fire chance rises by the
// shake amplitude it produces, so a hard drop (which shakes the most) charges
// a fire block fastest. Amplitude tops out at kShakeMaxAmplitude; the scale
// keeps even the hardest strike to a few percent, so the meter is charged by
// play over time rather than by one drop.
constexpr float kFireChancePerShake = 0.1f;

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
	// No window, no surface, no renderer - the base has logged why.
	if (!StartupSucceeded())
		return;

	// Supersample the offscreen pass so Tetris blocks read with crisper
	// edges - off by default: a 2x target quadruples the pixel work of every
	// pass, and on an integrated GPU that halves the frame rate. The engine
	// stays at 1.0; FG_RENDER_SCALE=2 opts back in per run.
	float renderScale = 1.0f;
	if (const char* rs = std::getenv("FG_RENDER_SCALE"))
		renderScale = std::atof(rs);
	Forge::Renderer::SetRenderScale(renderScale);
	Forge::Renderer::Init();

	// Mirror-polish the blocks: redraw the scene from the playfield centre into
	// a cubemap each frame so the metal surfaces reflect the well, the border
	// and the block stack. A missing device just means unreflected blocks.
	//
	// The probe must not sit inside the playfield: a probe 1.2 units in front of
	// the centre block captures a close-up of that block, and the block then
	// reflects itself as a saturated patch of "texture" - the stray tile. Place
	// it behind the camera instead, so it samples the scene from the viewer's
	// side rather than from inside the well.
	if (Forge::Renderer::EnableReflectionProbe())
		Forge::Renderer::SetReflectionProbePosition(BoardView::BoardCentre() + glm::vec3(0.0f, 0.0f, 6.0f));

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

	// Open the audio device and make sure the theme is on disk. The tune is
	// silent until the game starts - the menu is silent by design - so all
	// that happens here is the device and the bake. No device is not a
	// failure: the game plays silently.
	m_Music.Start();

	// Input and overlay are combined into a single layer so the window
	// receives events and pushes ImGui calls from one object.
	m_InputOverlayLayer = std::make_unique<InputOverlayLayer>(m_BoardView.GetManager());
	m_InputOverlayLayer->SetViewToggledCallback([this] { ToggleViewMode(); });
	m_InputOverlayLayer->SetMusicToggledCallback([this] { ToggleMusic(); });
	m_InputOverlayLayer->SetMusicSkippedCallback([this](int delta) { SkipMusic(delta); });
	m_InputOverlayLayer->SetMusic(&m_Music);
	PushLayer(m_InputOverlayLayer.get());

	if (!InitImGui())
		FG_INFO("Tetris: continuing without the on-screen controls");

	FG_INFO("Tetris: up. Arrows or WASD to move, Up/X to rotate, Space to drop, P to pause, R to restart, V to switch 2D/3D, M to mute, N/B to skip music.");
}

TetrisApp::~TetrisApp()
{
	// No window at startup means no ImGui backend to shut down.
	if (!StartupSucceeded())
		return;

	ShutdownImGui();

	// Unhook the layer before it can be reached through: it points into the
	// board view, and the stack only erases, it does not delete.
	if (m_InputOverlayLayer)
	{
		GetLayerStack().PopLayer(m_InputOverlayLayer.get());
		m_InputOverlayLayer.reset();
	}

	// Music last on the way out: the engine releases its sounds outright at
	// teardown, which is what shutdown wants - a fade-out is for stopping a
	// sound in a running game, not for outliving the app that owns it.
	m_Music.Shutdown();

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
		else if (arg.rfind("--profile", 0) == 0 || arg.rfind("--project", 0) == 0)
		{
			// Engine-level options: Application parses --profile and
			// --project for every application. Rejecting them here killed
			// the game before its first frame - a --profile run produced
			// an empty trace and an instant exit.
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
		m_CameraBasePosition = eye;
		cameraTransform.Rotation = CameraOrientation(eye, target);
		cameraComponent.FieldOfView = 65.0f;
	}
	else
	{
		const glm::vec3 eye = centre + glm::vec3(0.0f, 0.0f, 22.0f);
		cameraTransform.Translation = eye;
		m_CameraBasePosition = eye;
		cameraTransform.Rotation = { 0.0f, 0.0f, 0.0f };
		cameraComponent.FieldOfView = 60.0f;
	}

	cameraComponent.UpdateProjection();

	auto& lightComponent = m_LightEntity.GetComponent<Forge::LightComponent>();
	if (m_ThreeDimensional)
	{
		// A bright key light overexposes the lit faces, and ACES tonemapping
		// rolls an overexposed saturated colour toward white - the pastel wash.
		// Keeping the key near 1.0 stops the top faces blowing out, so the
		// albedo reads as the block's own colour and the shadowed faces stay
		// dark against it.
		//
		// The key comes from the front and only grazes the sides. pbr.frag
		// shades each face by NdotL, so a direction with equal X and Z lights
		// the front (normal +Z) and the side (normal +X) by the same amount and
		// every face reads the same brightness - a flat, featureless cube.
		// Loading Z (front, facing the camera) and pulling X back (side) makes
		// the front the brightest face, the top a lit bevel, and the sides fall
		// into shade, which is the lit/shadow face split that reads as depth.
		lightComponent.Intensity = 1.6f;
		lightComponent.Direction = glm::normalize(glm::vec3(0.22f, 0.40f, 0.90f));
	}
	else
	{
		lightComponent.Intensity = 1.25f;
		lightComponent.Direction = glm::normalize(glm::vec3(0.20f, 0.20f, 0.95f));
	}
	// DIAGNOSTIC ONLY: FG_LIGHT_SCALE scales the key light.
	if (const char* v = std::getenv("FG_LIGHT_SCALE"))
		lightComponent.Intensity *= std::atof(v);
}

void TetrisApp::CreateDecor()
{
	const glm::vec3 centre = BoardView::BoardCentre();
	const glm::vec3 boardSize = BoardView::BoardSize();
	const float halfW = boardSize.x * 0.5f;
	const float halfH = boardSize.y * 0.5f;
	// The preview tray and its blocks are laid out in cells, so they scale with
	// the block size and stay proportional to the blocks on the board.
	const float block = BoardView::BlockSize();

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
			centre + glm::vec3(BoardView::BoardSize().x * 0.5f + block * (kPreviewGap + kPreviewColumns * 0.5f),
				BoardView::BoardSize().y * 0.5f - 2.0f * block, -0.52f);
		trayTransform.Scale = glm::vec3((kPreviewColumns + 0.5f) * block, (kPreviewRows + 0.5f) * block, 0.3f);
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
		centre + glm::vec3(BoardView::BoardSize().x * 0.5f + block * (kPreviewGap + kPreviewColumns * 0.5f),
			BoardView::BoardSize().y * 0.5f - 2.0f * block, -0.3f);
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

void TetrisApp::ToggleMusic()
{
	const bool muted = !m_Music.IsMuted();
	m_Music.SetMuted(muted);
	FG_INFO("Tetris: music {0}", muted ? "muted" : "unmuted");
}

void TetrisApp::SkipMusic(int delta)
{
	m_Music.SkipTrack(delta);

	const char* title = m_Music.CurrentTitle();
	if (title && *title)
		FG_INFO("Tetris: music {0}/{1} - {2}",
			m_Music.CurrentTrackIndex() + 1, m_Music.TrackCount(), title);
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
	TetrominoType shown = TetrominoType::Count;
	if (state.CurrentState == TetrisGameState::State::Playing)
		shown = state.NextPiece.Type;

	const bool visible = shown != TetrominoType::Count;

	// The preview draws the piece the board will show next, so it uses the same
	// theme table - a preview in a different surface from the piece it
	// previews tells the player nothing.
	const glm::vec3 previewColour =
		visible ? (state.NextPiece.IsFire
			? TetrisManager::kFireColor
			: TetrisManager::GetTetrominoDefinition(shown).color)
		: glm::vec3(0.0f);


	glm::vec3 local[4];
	if (visible)
		BoardView::PreviewLocalOffsets(shown, local);

	for (int i = 0; i < 4; ++i)
	{
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

		const glm::vec3 offset =
			m_PreviewRoot.GetComponent<Forge::TransformComponent>().Translation + local[i];

		const auto cached = m_PreviewBlocks.find(key);
		if (cached == m_PreviewBlocks.end())
		{
			Forge::Entity entity = m_Scene->CreateEntity("PreviewBlock_" + std::to_string(i));
			auto& transform = entity.GetComponent<Forge::TransformComponent>();
			transform.Translation = offset;
			transform.Scale = glm::vec3(kPreviewBlockScale * BoardView::BlockSize());

			auto& material = entity.AddComponent<Forge::PBRMaterialComponent>();
			material.Albedo = previewColour;
			material.Metallic = kBlockMetallic;
			material.Roughness = kBlockRoughness;
			material.AO = 1.0f;

			material.Emissive = previewColour;
			material.EmissiveIntensity = kBlockEmissiveIntensity;

			entity.AddComponent<Forge::MeshComponent>().ModelPath = kBlockMesh;

			m_PreviewBlocks.emplace(key, entity);
		}
		else
		{
			// The shape moves with the piece: blocks are cached by slot index,
			// so a new queue entry has to reposition them as well as recolour
			// them. Leaving the translation alone drew the new piece's colour
			// at the old piece's cells.
			cached->second.GetComponent<Forge::TransformComponent>().Translation = offset;

			auto& cachedMaterial = cached->second.GetComponent<Forge::PBRMaterialComponent>();
			cachedMaterial.Albedo = previewColour;
			cachedMaterial.Metallic = kBlockMetallic;
			cachedMaterial.Roughness = kBlockRoughness;
			cachedMaterial.AO = 1.0f;

			cachedMaterial.Emissive = previewColour;
			cachedMaterial.EmissiveIntensity = kBlockEmissiveIntensity;
		}
	}
}

void TetrisApp::ApplyImpactShake(float dt)
{
	const BoardView::Impact impact = m_BoardView.GetLastImpact();
	const int clearedLines = static_cast<int>(m_BoardView.GetLastClearedRows().size());
	const bool lineCleared = clearedLines > 0;

	if (impact.Occurred || lineCleared)
	{
		const int lines = impact.Occurred ? impact.LinesCleared : clearedLines;
		const int totalLines = lines > 0 ? lines : clearedLines;
		const float speed = impact.Occurred ? impact.Speed : 0.0f;
		const float amplitude = ShakeAmplitudeForClear(totalLines, speed, kShakeMaxAmplitude);
		if (amplitude > m_ShakeAmplitude)
		{
			// A harder landing takes over a softer one, and the rumble starts
			// at the impact rather than continuing whatever swing the camera
			// happened to be mid-way through. Line clears shake too, harder
			// with more lines and with how fast the blocks were moving.
			m_ShakeAmplitude = amplitude;
			m_ShakePhase = 0.0f;

			// The strike charges the fire meter by the same amount it shakes.
			m_BoardView.GetManager().AddFireChance(amplitude * kFireChancePerShake);
		}
	}

	if (!m_CameraEntity)
		return;

	auto& transform = m_CameraEntity.GetComponent<Forge::TransformComponent>();

	if (m_ShakeAmplitude <= kShakeSettle)
	{
		if (m_Shaking)
		{
			transform.Translation = m_CameraBasePosition;
			m_Shaking = false;
		}
		m_ShakeAmplitude = 0.0f;
		return;
	}

	m_ShakePhase += dt;
	m_ShakeAmplitude *= std::exp(-kShakeDamping * dt);

	const float phase = 2.0f * kPi * m_ShakePhase;
	transform.Translation = m_CameraBasePosition + m_ShakeAmplitude * glm::vec3(
		kShakeHorizontalRatio * std::sin(kShakeHorizontalFrequency * phase),
		std::sin(kShakeVerticalFrequency * phase),
		kShakeDepthRatio * std::sin(kShakeDepthFrequency * phase + 2.2f));
	m_Shaking = true;
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

	// Frame pacing. A real window is paced by the swapchain's present, which
	// blocks until an image is free; with a software driver (headless capture,
	// CI) present never blocks, so the loop runs as fast as the CPU allows and
	// burns a whole core submitting frames the GPU can never drain. Cap the
	// loop to a target frame rate so the automation does not peg the machine.
	double targetFps = 60.0;
	if (const char* fps = std::getenv("FG_TARGET_FPS"))
		targetFps = std::atof(fps);
	const double frameBudget = targetFps > 0.0 ? 1.0 / targetFps : 0.0;

	// DIAGNOSTIC ONLY: fill the board so a capture shows every tile at once.
	// Applied after the first Update, because StartGame() clears the board.

	while (IsRunning())
	{
		FG_PROFILE_SCOPE("Frame");
		const double frameStart = Forge::Time::GetTime();
		const float time = static_cast<float>(frameStart);
		const float frameTime = time - lastFrameTime;
		lastFrameTime = time;

		// Advance the game and mirror it into the scene before anything draws.
		m_BoardView.Update(frameTime);

		// DIAGNOSTIC ONLY: start the game headless (no key to press), so the
		// board fills, lines clear and blocks are created and destroyed.
		if (std::getenv("FG_AUTOSTART") && m_FrameCount == 0)
			m_BoardView.GetManager().StartGame();

		// DIAGNOSTIC ONLY: force the falling piece to be a fire block.
		if (std::getenv("FG_FORCE_FIRE"))
			m_BoardView.GetManager().GetState().CurrentPiece.IsFire = true;

		// DIAGNOSTIC ONLY: fill the board so a capture shows every tile at once.
		// Seeded after an Update so StartGame() has already cleared the board.
		if (std::getenv("FG_SEED_BOARD") && m_FrameCount == 0)
		{
			const int rows = std::getenv("FG_SEED_ROWS") ? std::atoi(std::getenv("FG_SEED_ROWS")) : 6;
			TetrisGameState& seeded = m_BoardView.GetManager().GetState();
			for (uint32_t row = 0; row < static_cast<uint32_t>(rows); ++row)
				for (uint32_t col = 0; col < seeded.Width; ++col)
					seeded.Board[row * seeded.Width + col] =
						static_cast<uint8_t>((row + col) % 7 + 1);
			m_BoardView.Update(frameTime);
		}
		// DIAGNOSTIC ONLY: fill the bottom rows so a falling piece lands on the
		// stack, locks and clears them - a line clear with its reward, with the
		// spawn row left free.
		if (std::getenv("FG_SEED_BOTTOM_ROWS") && m_FrameCount == 0)
		{
			const int rows = std::atoi(std::getenv("FG_SEED_BOTTOM_ROWS"));
			TetrisGameState& seeded = m_BoardView.GetManager().GetState();
			for (int r = 0; r < rows; ++r)
			{
				const uint32_t row = seeded.Height - 1 - static_cast<uint32_t>(r);
				for (uint32_t col = 0; col < seeded.Width; ++col)
					seeded.Board[row * seeded.Width + col] =
						static_cast<uint8_t>((row + col) % 7 + 1);
			}
			m_BoardView.Update(frameTime);
		}
		// DIAGNOSTIC ONLY: FG_ALL_TEX=<path> gives every board block the same
		// tile, so a capture separates "the UVs do not address the tile" from
		// "the tile is sampled and the lighting flattens it".
		if (const char* allTex = std::getenv("FG_ALL_TEX"))
		{
			if (m_FrameCount == 0)
			{
				for (auto handle : m_Scene->GetAllEntitiesWith<Forge::TagComponent, Forge::PBRMaterialComponent>())
				{
					Forge::Entity entity(handle, m_Scene.get());
					const std::string tag = entity.GetComponent<Forge::TagComponent>().Tag;
					if (tag.rfind("TetBlock_", 0) != 0)
						continue;
					entity.GetComponent<Forge::PBRMaterialComponent>().AlbedoTexturePath = allTex;
				}
			}
		}
		// DIAGNOSTIC ONLY: strip every albedo path, so a capture separates "the
		// tile is not bound" from "the tile is bound and the lighting drowns it".
		if (std::getenv("FG_NO_TEX") && m_FrameCount == 0)
		{
			for (auto handle : m_Scene->GetAllEntitiesWith<Forge::PBRMaterialComponent>())
			{
				Forge::Entity entity(handle, m_Scene.get());
				entity.GetComponent<Forge::PBRMaterialComponent>().AlbedoTexturePath.clear();
			}
		}
		// DIAGNOSTIC ONLY: paint every board cell with a unique flat colour so
		// the frame becomes a map from screen position to grid cell.
		if (std::getenv("FG_COLOR_CELLS") && m_FrameCount == 0)
		{
			auto hsv = [](float h, float s, float v) {
				const float i = std::floor(h * 6.0f);
				const float f = h * 6.0f - i;
				const float p = v * (1.0f - s), q = v * (1.0f - f * s), t = v * (1.0f - (1.0f - f) * s);
				switch (static_cast<int>(i) % 6)
				{
				case 0: return glm::vec3(v, t, p);
				case 1: return glm::vec3(q, v, p);
				case 2: return glm::vec3(p, v, t);
				case 3: return glm::vec3(p, q, v);
				case 4: return glm::vec3(t, p, v);
				default: return glm::vec3(v, p, q);
				}
			};
			for (auto handle : m_Scene->GetAllEntitiesWith<Forge::TagComponent, Forge::PBRMaterialComponent>())
			{
				Forge::Entity entity(handle, m_Scene.get());
				const std::string tag = entity.GetComponent<Forge::TagComponent>().Tag;
				int a = -1, b = -1;
				if (std::sscanf(tag.c_str(), "TetBlock_%d_%d", &a, &b) != 2)
					continue;
				auto& material = entity.GetComponent<Forge::PBRMaterialComponent>();
				const float hue = static_cast<float>((a * 10 + b) % 20) / 20.0f;
				material.AlbedoTexturePath.clear();
				material.Albedo = hsv(hue, 1.0f, 1.0f);
				material.Metallic = 0.0f;
				material.EmissiveIntensity = 0.0f;
			}
		}
		if (m_FrameCount == 0)
		{
			const bool noEmit = []() { const char* v = std::getenv("FG_NO_EMIT"); return v && v[0] == '1'; }();
			const bool noMetal = []() { const char* v = std::getenv("FG_NO_METAL"); return v && v[0] == '1'; }();
			const bool flatMetal = []() { const char* v = std::getenv("FG_FLAT_METAL"); return v && v[0] == '1'; }();
			if (noEmit || noMetal || flatMetal)
			{
				for (auto handle : m_Scene->GetAllEntitiesWith<Forge::PBRMaterialComponent>())
				{
					Forge::Entity entity(handle, m_Scene.get());
					auto& material = entity.GetComponent<Forge::PBRMaterialComponent>();
					if (noEmit)
						material.EmissiveIntensity = 0.0f;
					if (noMetal)
						material.Metallic = 0.0f;
					if (flatMetal)
						material.Roughness = 1.0f;
				}
			}
		}
		// The landing animation is written onto the scene by the view; the
		// camera answers to the same impact, so the strike and the shake are
		// two readings of one event rather than two events.
		ApplyImpactShake(frameTime);
		// Music runs outside the render branch on purpose: a minimised window
		// still has to drain finished sounds, or the theme keeps playing with
		// nothing on screen to pause it.
		m_Music.Update(m_BoardView.GetManager().GetState());
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
			// DIAGNOSTIC ONLY: report what the draw loop actually bound.
			if (std::getenv("FG_STATS"))
			{
				const Forge::RendererStatistics stats = Forge::Renderer::GetStatistics();
				FG_INFO("Tetris DIAG frame {0}: draws={1} tris={2} texturesBound={3} culled={4}",
					m_FrameCount, stats.DrawCalls3D, stats.TrianglesSubmitted, stats.TexturesBound,
					stats.FrustumCulled);
			}
			// DIAGNOSTIC ONLY: FG_NO_SSAO skips the SSAO pass.
			if (![]() { const char* v = std::getenv("FG_NO_SSAO"); return v && v[0] == '1'; }())
				Forge::Renderer::BeginSSAOPass();
			Forge::Renderer::BeginBloomPass();
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
				const long long want = std::getenv("FG_CAPTURE_FRAME")
					? std::atoll(std::getenv("FG_CAPTURE_FRAME"))
					: 1LL;
				if (m_Captured)
					(void)0;  // one capture per run: see the comment above
				else if (static_cast<long long>(m_FrameCount) + 1 < want)
					(void)0;  // DIAGNOSTIC ONLY: capture a later frame
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

		// Sleep out the rest of the frame budget. Only when the frame ran
		// faster than the budget: a frame that already overran (a slow
		// software render) is not made slower.
		if (frameBudget > 0.0)
		{
			const double spent = Forge::Time::GetTime() - frameStart;
			if (spent < frameBudget)
				std::this_thread::sleep_for(std::chrono::duration<double>(frameBudget - spent));
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
