#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include "Forge/Core/Log.h"
#include "Forge/Editor/EditorContext.h"
#include "Forge/Editor/Gizmo.h"
#include "Forge/Assets/AssetRegistry.h"
#include "Forge/Project/GameExporter.h"
#include "Forge/Audio/AudioEngine.h"
#include "Forge/Scene/Components.h"
#include "Forge/Physics/Physics.h"
#include "Forge/Scripting/ScriptEngine.h"
#include "Forge/Scene/Prefab.h"

#include <filesystem>
#include <fstream>

namespace {

	void WriteFile(const std::string& path, const std::string& contents)
	{
		std::filesystem::path p(path);
		if (p.has_parent_path())
			std::filesystem::create_directories(p.parent_path());

		std::ofstream out(path, std::ios::binary);
		out << contents;
	}

}

TEST_CASE("Gizmo ray-plane intersection", "[gizmo][tooling]")
{
	glm::vec3 hit;
	const glm::vec3* result = Forge::Gizmo::RayPlaneIntersect(
		{ 0.0f, 5.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, hit);
	REQUIRE(result != nullptr);
	REQUIRE(hit.y == Approx(0.0f));

	// Parallel ray misses.
	const glm::vec3* miss = Forge::Gizmo::RayPlaneIntersect(
		{ 0.0f, 5.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, hit);
	REQUIRE(miss == nullptr);
}

TEST_CASE("Gizmo ray-point distance", "[gizmo][tooling]")
{
	float dist = Forge::Gizmo::RayPointDistance({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 2.0f, 5.0f });
	REQUIRE(dist == Approx(2.0f));
}

TEST_CASE("Gizmo axis projection", "[gizmo][tooling]")
{
	float t = Forge::Gizmo::ProjectOntoAxis({ 3.0f, 7.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f });
	REQUIRE(t == Approx(3.0f));
}

TEST_CASE("Gizmo snapping", "[gizmo][tooling]")
{
	REQUIRE(Forge::Gizmo::Snap(1.3f, 0.5f) == Approx(1.5f));
	REQUIRE(Forge::Gizmo::Snap(1.3f, 0.0f) == Approx(1.3f));

	auto v = Forge::Gizmo::Snap(glm::vec3(0.14f, 0.26f, 0.35f), 0.1f);
	REQUIRE(v.x == Approx(0.1f));
	REQUIRE(v.y == Approx(0.3f));
	REQUIRE(v.z == Approx(0.4f));
}

TEST_CASE("AssetRegistry scans and classifies assets", "[assets][tooling]")
{
	// A dedicated directory: this test asserts an exact asset count, so it must
	// not scan a folder other tests also write into.
	const std::string root = "test_asset_scan";
	Forge::AssetRegistry::Clear();

	WriteFile(root + "/level.forge", "Scene: x");
	WriteFile(root + "/player.lua", "return {}");
	WriteFile(root + "/model.gltf", "{}");
	WriteFile(root + "/albedo.png", "png");
	WriteFile(root + "/notes.txt", "hello");

	Forge::AssetRegistry::Scan(root);

	const auto& all = Forge::AssetRegistry::GetAllAssets();
	REQUIRE(all.size() == 4); // .txt is Unknown and skipped

	const auto& scripts = Forge::AssetRegistry::GetAssetsOfType(Forge::AssetType::Script);
	REQUIRE(scripts.size() == 1);
	REQUIRE(scripts[0].Name == "player");

	const auto& textures = Forge::AssetRegistry::GetAssetsOfType(Forge::AssetType::Texture);
	REQUIRE(textures.size() == 1);

	const auto* found = Forge::AssetRegistry::FindByPath(root + "/level.forge");
	REQUIRE(found);
	REQUIRE(found->Type == Forge::AssetType::Scene);

	Forge::AssetRegistry::Clear();
}

TEST_CASE("GameExporter packages a game", "[export][tooling]")
{
	const std::string root = "test_export";
	WriteFile(root + "/game.forge", "Scene: Game");
	WriteFile(root + "/resources/scripts/a.lua", "return {}");
	WriteFile(root + "/resources/prefabs/e.forge", "Prefab: e");

	// Dummy runtime executable.
	WriteFile(root + "/runtime.bin", "binary");

	Forge::GameExporter::Options options;
	options.OutputDirectory = root + "/dist/";
	options.ScenePath = root + "/game.forge";
	options.ResourcesDirectory = root + "/resources/";
	options.RuntimeExecutable = root + "/runtime.bin";
	options.GameName = "TestGame";

	REQUIRE(Forge::GameExporter::Export(options));

	REQUIRE(std::filesystem::exists(root + "/dist/game.forge"));
	REQUIRE(std::filesystem::exists(root + "/dist/runtime.bin"));
	REQUIRE(std::filesystem::exists(root + "/dist/resources/scripts/a.lua"));
	REQUIRE(std::filesystem::exists(root + "/dist/resources/prefabs/e.forge"));
}

TEST_CASE("EditorContext entity lifecycle", "[editor][tooling]")
{
	Forge::Log::Init();
	Forge::EditorContext context;

	auto entity = context.CreateEntity("Player");
	REQUIRE(entity);
	REQUIRE(context.GetSelectedEntity() == Forge::Entity{});

	context.SelectEntity(entity);
	REQUIRE(context.HasSelection());

	auto duplicate = context.DuplicateEntity(entity);
	REQUIRE(duplicate);
	REQUIRE(duplicate.GetName() == "Player");

	auto found = context.FindEntityByName("Player");
	REQUIRE(found);

	auto uuid = entity.GetUUID();
	context.DestroyEntity(entity);
	REQUIRE(!context.GetEntityByUUID(uuid));

	std::vector<Forge::Entity> entities = context.GetAllEntities();
	// "Player" was destroyed; only "Player" duplicate remains.
	REQUIRE(entities.size() == 1);
}

TEST_CASE("EditorContext scene save and open", "[editor][tooling]")
{
	Forge::Log::Init();

	Forge::EditorContext writer;
	auto entity = writer.CreateEntity("Cube");
	auto& tc = entity.GetComponent<Forge::TransformComponent>();
	tc.Translation = { 1.0f, 2.0f, 3.0f };

	const std::string path = "test_editor_scene.forge";
	REQUIRE(writer.SaveScene(path));

	Forge::EditorContext reader;
	REQUIRE(reader.OpenScene(path));

	auto loaded = reader.FindEntityByName("Cube");
	REQUIRE(loaded);
	REQUIRE(loaded.GetComponent<Forge::TransformComponent>().Translation == glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST_CASE("EditorContext playback controls", "[editor][tooling]")
{
	Forge::Log::Init();
	Forge::ScriptEngine::Init();
	// Physics is initialized right before Play() (the context expects a fresh
	// world). This also mirrors how the editor starts a runtime.
	Forge::Physics::Init();

	Forge::EditorContext context;
	auto entity = context.CreateEntity("Dynamic");
	entity.AddComponent<Forge::RigidbodyComponent>().Type = Forge::RigidbodyComponent::BodyType::Dynamic;
	entity.AddComponent<Forge::BoxColliderComponent>();

	REQUIRE(!context.IsPlaying());
	context.Play();
	REQUIRE(context.IsPlaying());
	REQUIRE(!context.IsPaused());

	context.Pause();
	REQUIRE(context.IsPaused());

	// Step a few frames while paused.
	auto& tc = entity.GetComponent<Forge::TransformComponent>();
	tc.Translation = { 0.0f, 10.0f, 0.0f };
	Forge::Timestep ts(0.016f);
	for (int i = 0; i < 5; i++)
		context.Update(ts);

	// While paused, Update calls OnUpdateEditor (no physics), so transform unchanged.
	REQUIRE(tc.Translation.y == Approx(10.0f));

	context.Pause(); // unpause
	for (int i = 0; i < 5; i++)
		context.Update(ts);

	// Now physics runs and the body falls.
	REQUIRE(tc.Translation.y < 10.0f);

	context.Stop();
	REQUIRE(!context.IsPlaying());

	Forge::ScriptEngine::Shutdown();
	Forge::Physics::Shutdown();
}

TEST_CASE("EditorContext SetPlaying toggles play/stop", "[editor][tooling]")
{
	Forge::Log::Init();
	Forge::Physics::Init();

	Forge::EditorContext context;
	REQUIRE(!context.IsPlaying());

	context.SetPlaying(true);
	REQUIRE(context.IsPlaying());

	context.SetPlaying(false);
	REQUIRE(!context.IsPlaying());

	Forge::Physics::Shutdown();
}

TEST_CASE("Camera projection updates on viewport resize", "[scene][camera][tooling]")
{
	Forge::Scene scene;
	auto entity = scene.CreateEntity("Camera");
	auto& camera = entity.AddComponent<Forge::CameraComponent>();
	camera.FieldOfView = 60.0f;
	camera.NearClip = 0.1f;
	camera.FarClip = 1000.0f;
	camera.AspectRatio = 16.0f / 9.0f;
	camera.UpdateProjection();

	// Resize to a 1:1 viewport.
	scene.OnViewportResize(800, 800);
	REQUIRE(camera.AspectRatio == Approx(1.0f));

	// The projection matrix reflects the new aspect ratio.
	glm::mat4 expected = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 1000.0f);
	REQUIRE(camera.ProjectionMatrix[0][0] == Approx(expected[0][0]));
	REQUIRE(camera.ProjectionMatrix[1][1] == Approx(expected[1][1]));

	// A zero-sized viewport is ignored.
	scene.OnViewportResize(0, 0);
	REQUIRE(camera.AspectRatio == Approx(1.0f));
}

TEST_CASE("AudioEngine decodes a WAV file", "[audio][tooling]")
{
    REQUIRE_NOTHROW(Forge::AudioEngine::Init());
    REQUIRE(Forge::AudioEngine::IsInitialized());

    auto buffer = Forge::AudioEngine::LoadAudioBuffer(std::string(FORGE_SOURCE_DIR) + "/resources/audio/test_tone.wav");
    REQUIRE(buffer);

    // A 0.1s 440Hz stereo tone resampled to 48kHz by the decoder.
    REQUIRE(buffer->GetChannels() == 2);
    REQUIRE(buffer->GetSampleRate() == 48000);
    REQUIRE(buffer->GetFrameCount() > 0);
    REQUIRE(buffer->GetDuration() == Approx(0.1f).epsilon(0.05f));
    REQUIRE(buffer->GetSamples() != nullptr);

    // Playback must be safe even with no real output device (headless).
    REQUIRE_NOTHROW(Forge::AudioEngine::PlayAudioBuffer(buffer));
    REQUIRE_NOTHROW(Forge::AudioEngine::StopAudioBuffer(buffer));

    Forge::AudioEngine::SetMasterVolume(0.5f);
    REQUIRE(Forge::AudioEngine::GetMasterVolume() == Approx(0.5f));

    Forge::AudioEngine::Shutdown();
    REQUIRE_FALSE(Forge::AudioEngine::IsInitialized());
}

TEST_CASE("AudioEngine handles missing files", "[audio][tooling]")
{
    REQUIRE_NOTHROW(Forge::AudioEngine::Init());
    REQUIRE(Forge::AudioEngine::LoadAudioBuffer("missing.wav") == nullptr);
    Forge::AudioEngine::Shutdown();
}

TEST_CASE("EditorContext prefab instantiation", "[editor][tooling]")
{
	Forge::Log::Init();
	Forge::EditorContext context;

	auto entity = context.CreateEntity("Proto");
	entity.AddComponent<Forge::PBRMaterialComponent>().Albedo = { 1.0f, 0.0f, 0.0f };

	REQUIRE(Forge::Prefab::CreateFromEntity(entity, "test_prefabs/EditorProto.forge"));

	Forge::PrefabLibrary::SetDirectory("test_prefabs/");
	auto instance = context.InstantiatePrefab("EditorProto");
	REQUIRE(instance);
	REQUIRE(instance.GetComponent<Forge::PBRMaterialComponent>().Albedo == glm::vec3(1.0f, 0.0f, 0.0f));

	auto positioned = context.InstantiatePrefab("EditorProto", true, { 1.0f, 2.0f, 3.0f });
	REQUIRE(positioned);
	REQUIRE(positioned.GetComponent<Forge::TransformComponent>().Translation == glm::vec3(1.0f, 2.0f, 3.0f));
}

// ---------------------------------------------------------------------------
// EditorContext: the AI-agent entry point.
//
// These cover the surface an agent drives directly: selection, duplication,
// queries, playback state transitions and gizmo configuration. They also pin
// down two state-desync bugs that previously made the context disagree with the
// scene it wraps.
// ---------------------------------------------------------------------------

TEST_CASE("EditorContext destroys only the requested entity", "[editor][tooling]")
{
    Forge::Log::Init();

    Forge::EditorContext context;
    context.CreateEntity("Keep");
    auto drop = context.CreateEntity("Drop");

    context.DestroyEntity(drop);

    REQUIRE(context.FindEntityByName("Keep"));
    REQUIRE(!context.FindEntityByName("Drop"));
}

TEST_CASE("Destroying the selected entity clears the selection", "[editor][tooling]")
{
    Forge::Log::Init();

    Forge::EditorContext context;
    auto entity = context.CreateEntity("Target");

    context.SelectEntity(entity);
    REQUIRE(context.HasSelection());

    context.DestroyEntity(entity);
    REQUIRE(!context.HasSelection());
}

TEST_CASE("Destroying an invalid entity does not clear the selection", "[editor][tooling]")
{
    Forge::Log::Init();

    Forge::EditorContext context;
    auto selected = context.CreateEntity("Selected");
    context.SelectEntity(selected);
    REQUIRE(context.HasSelection());

    // A default-constructed Entity is invalid. Destroying it must be a no-op;
    // treating "invalid" as "clear everything" would desync the editor's
    // selection from the scene for no reason.
    Forge::Entity nothing;
    context.DestroyEntity(nothing);

    REQUIRE(context.HasSelection());
    REQUIRE(context.GetSelectedEntity() == selected);
}

TEST_CASE("EditorContext ClearSelection empties the selection", "[editor][tooling]")
{
    Forge::Log::Init();

    Forge::EditorContext context;
    auto entity = context.CreateEntity("Thing");

    context.SelectEntity(entity);
    context.ClearSelection();

    REQUIRE(!context.HasSelection());
    REQUIRE(!context.GetSelectedEntity());
}

TEST_CASE("EditorContext duplicates an entity with its components", "[editor][tooling]")
{
    Forge::Log::Init();

    Forge::EditorContext context;
    auto original = context.CreateEntity("Original");

    auto& tc = original.GetComponent<Forge::TransformComponent>();
    tc.Translation = { 1.0f, 2.0f, 3.0f };
    tc.Scale = { 2.0f, 2.0f, 2.0f };

    auto& pbr = original.AddComponent<Forge::PBRMaterialComponent>();
    pbr.Albedo = { 0.25f, 0.5f, 0.75f };
    original.AddComponent<Forge::BoxColliderComponent>();

    auto copy = context.DuplicateEntity(original);
    REQUIRE(copy);
    REQUIRE(copy.GetUUID() != original.GetUUID());

    REQUIRE(copy.GetComponent<Forge::TransformComponent>().Translation == glm::vec3(1.0f, 2.0f, 3.0f));
    REQUIRE(copy.GetComponent<Forge::TransformComponent>().Scale == glm::vec3(2.0f, 2.0f, 2.0f));
    REQUIRE(copy.HasComponent<Forge::BoxColliderComponent>());
    REQUIRE(copy.GetComponent<Forge::PBRMaterialComponent>().Albedo == glm::vec3(0.25f, 0.5f, 0.75f));

    // The copy must appear in queries alongside the original.
    REQUIRE(context.GetAllEntities().size() == 2);
}

TEST_CASE("EditorContext queries find entities by name and UUID", "[editor][tooling]")
{
    Forge::Log::Init();

    Forge::EditorContext context;
    auto alpha = context.CreateEntity("Alpha");
    auto beta = context.CreateEntity("Beta");

    REQUIRE(context.FindEntityByName("Alpha") == alpha);
    REQUIRE(context.FindEntityByName("Beta") == beta);
    REQUIRE(!context.FindEntityByName("Missing"));

    REQUIRE(context.GetEntityByUUID(alpha.GetUUID()) == alpha);
    REQUIRE(!context.GetEntityByUUID(Forge::UUID()));

    const auto all = context.GetAllEntities();
    REQUIRE(all.size() == 2);
}

TEST_CASE("EditorContext GetAllEntities is empty for a new scene", "[editor][tooling]")
{
    Forge::Log::Init();

    Forge::EditorContext context;
    REQUIRE(context.GetAllEntities().empty());
}

TEST_CASE("EditorContext NewScene discards the previous scene", "[editor][tooling]")
{
    Forge::Log::Init();

    Forge::EditorContext context;
    auto entity = context.CreateEntity("Old");
    context.SelectEntity(entity);
    REQUIRE(context.GetAllEntities().size() == 1);

    context.NewScene();

    REQUIRE(context.GetAllEntities().empty());
    REQUIRE(!context.HasSelection());
}

TEST_CASE("EditorContext reports missing scene files without losing state", "[editor][tooling]")
{
    Forge::Log::Init();

    Forge::EditorContext context;
    auto entity = context.CreateEntity("Keep");
    context.SelectEntity(entity);

    // A failed open must leave the current scene and selection untouched.
    REQUIRE(context.OpenScene("test_assets/does_not_exist.forge") == false);
    REQUIRE(context.GetAllEntities().size() == 1);
    REQUIRE(context.GetSelectedEntity() == entity);
}

TEST_CASE("Play resumes from a paused state", "[editor][tooling]")
{
    Forge::Log::Init();
    Forge::Physics::Init();

    Forge::EditorContext context;
    context.Play();
    REQUIRE(context.IsPlaying());

    context.Pause();
    REQUIRE(context.IsPaused());

    // Play() while paused must resume, not be swallowed by an early return.
    context.Play();
    REQUIRE(context.IsPlaying());
    REQUIRE(!context.IsPaused());

    context.Stop();
    Forge::Physics::Shutdown();
}

TEST_CASE("Pause before Play does not leave the scene paused", "[editor][tooling]")
{
    Forge::Log::Init();
    Forge::Physics::Init();

    Forge::EditorContext context;
    auto entity = context.CreateEntity("Faller");
    entity.AddComponent<Forge::RigidbodyComponent>().Type = Forge::RigidbodyComponent::BodyType::Dynamic;
    entity.AddComponent<Forge::BoxColliderComponent>();
    entity.GetComponent<Forge::TransformComponent>().Translation = { 0.0f, 10.0f, 0.0f };

    // Pause() outside playback is ignored, so it cannot poison later playback.
    context.Pause();

    context.Play();
    REQUIRE(!context.IsPaused());

    auto& tc = entity.GetComponent<Forge::TransformComponent>();
    for (int i = 0; i < 5; i++)
        context.Update(Forge::Timestep(0.016f));

    // If the Scene's own pause flag had been left set, this would not move.
    REQUIRE(tc.Translation.y < 10.0f);

    context.Stop();
    Forge::Physics::Shutdown();
}

TEST_CASE("Pause outside playback is ignored", "[editor][tooling]")
{
    Forge::Log::Init();

    Forge::EditorContext context;
    context.Pause();
    REQUIRE(!context.IsPaused());

    context.Stop();
    REQUIRE(!context.IsPlaying());
}

TEST_CASE("Step advances exactly the requested frames", "[editor][tooling]")
{
    Forge::Log::Init();
    Forge::Physics::Init();

    Forge::EditorContext context;
    auto entity = context.CreateEntity("Faller");
    entity.AddComponent<Forge::RigidbodyComponent>().Type = Forge::RigidbodyComponent::BodyType::Dynamic;
    entity.AddComponent<Forge::BoxColliderComponent>();
    entity.GetComponent<Forge::TransformComponent>().Translation = { 0.0f, 10.0f, 0.0f };

    context.Play();
    context.Pause();
    REQUIRE(context.IsPaused());

    auto& tc = entity.GetComponent<Forge::TransformComponent>();

    // No stepping yet: paused frames must not advance the simulation.
    context.Update(Forge::Timestep(0.016f));
    REQUIRE(tc.Translation.y == Approx(10.0f));

    // Three explicit steps.
    context.Step(3);
    for (int i = 0; i < 3; i++)
        context.Update(Forge::Timestep(0.016f));

    REQUIRE(tc.Translation.y < 10.0f);

    // The step budget is spent, so further paused frames do nothing.
    const float afterSteps = tc.Translation.y;
    context.Update(Forge::Timestep(0.016f));
    REQUIRE(tc.Translation.y == Approx(afterSteps));

    context.Stop();
    Forge::Physics::Shutdown();
}

TEST_CASE("Stop is safe to call repeatedly and outside playback", "[editor][tooling]")
{
    Forge::Log::Init();
    Forge::Physics::Init();

    Forge::EditorContext context;
    REQUIRE_NOTHROW(context.Stop());
    REQUIRE_NOTHROW(context.Stop());

    context.Play();
    REQUIRE_NOTHROW(context.Stop());
    REQUIRE_NOTHROW(context.Stop());
    REQUIRE(!context.IsPlaying());
    REQUIRE(!context.IsPaused());

    Forge::Physics::Shutdown();
}

	TEST_CASE("Simulate runs the scene under its own camera state machine", "[editor][simulate][tooling]")
	{
		Forge::Log::Init();
		Forge::ScriptEngine::Init();
		Forge::Physics::Init();

		Forge::EditorContext context;

		context.Simulate();
		REQUIRE(context.IsPlaying());
		REQUIRE(context.IsSimulate());
		REQUIRE(!context.IsPaused());

		// The toolbar's Simulate button doubles as Stop while simulating.
		context.Simulate();
		REQUIRE(!context.IsPlaying());
		REQUIRE(!context.IsSimulate());

		// Simulate from Play tears the play session down before restarting.
		context.Play();
		REQUIRE(!context.IsSimulate());
		context.Simulate();
		REQUIRE(context.IsSimulate());

		// Play takes the camera back under the author without dropping scripts.
		context.Play();
		REQUIRE(context.IsPlaying());
		REQUIRE(!context.IsSimulate());

		// Pause is orthogonal to the mode: a paused frame keeps its viewpoint.
		context.Simulate();
		context.Pause();
		REQUIRE(context.IsSimulate());
		REQUIRE(context.IsPaused());

		context.Stop();
		REQUIRE(!context.IsPlaying());
		REQUIRE(!context.IsSimulate());

		Forge::ScriptEngine::Shutdown();
		Forge::Physics::Shutdown();
	}

	TEST_CASE("Simulate renders through the scene's primary camera", "[editor][simulate][tooling]")
	{
		Forge::Log::Init();
		Forge::ScriptEngine::Init();
		Forge::Physics::Init();

		Forge::EditorContext context;

		const glm::mat4 sentinel = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 3.0f, 4.0f));

		Forge::Entity camera = context.CreateEntity("Cam");
		auto& cc = camera.AddComponent<Forge::CameraComponent>();
		cc.Primary = true;
		cc.ViewProjectionMatrix = sentinel;

		// Edit and play both view through the editor camera; only simulate
		// hands the view to the scene's camera.
		REQUIRE(context.GetViewProjection() != sentinel);
		context.Play();
		REQUIRE(context.GetViewProjection() != sentinel);

		context.Simulate();
		REQUIRE(context.GetViewProjection() == sentinel);

		context.Pause();
		REQUIRE(context.GetViewProjection() == sentinel);

		context.Stop();
		REQUIRE(context.GetViewProjection() != sentinel);

		Forge::ScriptEngine::Shutdown();
		Forge::Physics::Shutdown();
	}

	TEST_CASE("EditorContext exposes gizmo mode, space and snap", "[editor][tooling]")
{
    Forge::Log::Init();

    Forge::EditorContext context;

    REQUIRE(context.GetGizmoMode() == Forge::Gizmo::Mode::Translate);
    REQUIRE(context.GetGizmoSpace() == Forge::Gizmo::Space::World);
    REQUIRE(context.GetTranslationSnap() == Approx(0.0f));

    context.SetGizmoMode(Forge::Gizmo::Mode::Rotate);
    context.SetGizmoSpace(Forge::Gizmo::Space::Local);
    context.SetTranslationSnap(0.25f);

    REQUIRE(context.GetGizmoMode() == Forge::Gizmo::Mode::Rotate);
    REQUIRE(context.GetGizmoSpace() == Forge::Gizmo::Space::Local);
    REQUIRE(context.GetTranslationSnap() == Approx(0.25f));
}

TEST_CASE("EditorContext exposes a camera with a usable projection", "[editor][tooling]")
{
    Forge::Log::Init();

    Forge::EditorContext context;
    context.GetCamera().SetProjection(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    context.GetCamera().SetPosition({ 0.0f, 0.0f, 5.0f });
    context.GetCamera().OnUpdate(0.016f);

    REQUIRE(context.GetCamera().GetPosition() == glm::vec3(0.0f, 0.0f, 5.0f));

    // After an update the combined matrix must reflect the camera placement
    // rather than still being the untouched default.
    REQUIRE(context.GetViewProjection() != glm::mat4(1.0f));

    // Moving the camera must change the matrix an agent is handed.
    const glm::mat4 before = context.GetViewProjection();
    context.GetCamera().SetPosition({ 10.0f, 2.0f, 5.0f });
    context.GetCamera().OnUpdate(0.016f);
    REQUIRE(context.GetViewProjection() != before);
}

TEST_CASE("EditorContext InstantiatePrefab reports unknown prefabs", "[editor][tooling]")
{
    Forge::Log::Init();
    Forge::PrefabLibrary::SetDirectory(std::string(FORGE_SOURCE_DIR) + "/resources/prefabs/");

    Forge::EditorContext context;
    REQUIRE(!context.InstantiatePrefab("NoSuchPrefab"));

    const size_t before = context.GetAllEntities().size();
    REQUIRE_FALSE(context.InstantiatePrefab("NoSuchPrefab", true, glm::vec3(1.0f)));
    REQUIRE(context.GetAllEntities().size() == before);
}

TEST_CASE("EditorContext round-trips a scene through save and open", "[editor][tooling]")
{
    Forge::Log::Init();

    Forge::EditorContext context;
    auto entity = context.CreateEntity("Persisted");
    entity.GetComponent<Forge::TransformComponent>().Translation = { 4.0f, 5.0f, 6.0f };
    auto& pbr = entity.AddComponent<Forge::PBRMaterialComponent>();
    pbr.Metallic = 0.75f;
    pbr.Roughness = 0.25f;

    const std::string path = "test_assets/EditorContextRoundTrip.forge";
    REQUIRE(context.SaveScene(path));

    context.NewScene();
    REQUIRE(context.GetAllEntities().empty());

    REQUIRE(context.OpenScene(path));
    auto reloaded = context.FindEntityByName("Persisted");
    REQUIRE(reloaded);
    REQUIRE(reloaded.GetComponent<Forge::TransformComponent>().Translation == glm::vec3(4.0f, 5.0f, 6.0f));
    REQUIRE(reloaded.GetComponent<Forge::PBRMaterialComponent>().Metallic == Approx(0.75f));
    REQUIRE(reloaded.GetComponent<Forge::PBRMaterialComponent>().Roughness == Approx(0.25f));
}

TEST_CASE("An agent can build a small scene entirely through the context", "[editor][tooling]")
{
    Forge::Log::Init();

    // This mirrors the real use case: assemble a scene headlessly, save it, and
    // confirm every entity and component survived.
    Forge::EditorContext context;

    auto floor = context.CreateEntity("Floor");
    floor.AddComponent<Forge::BoxColliderComponent>().Size = glm::vec3(20.0f, 1.0f, 20.0f);

    auto player = context.CreateEntity("Player");
    player.GetComponent<Forge::TransformComponent>().Translation = { 0.0f, 2.0f, 0.0f };
    auto& rb = player.AddComponent<Forge::RigidbodyComponent>();
    rb.Type = Forge::RigidbodyComponent::BodyType::Dynamic;
    player.AddComponent<Forge::BoxColliderComponent>().Size = glm::vec3(1.0f);
    player.AddComponent<Forge::ScriptComponent>().ClassName = "Mover";

    auto& material = player.AddComponent<Forge::PBRMaterialComponent>();
    material.Albedo = { 0.8f, 0.2f, 0.2f };

    REQUIRE(context.GetAllEntities().size() == 2);
    REQUIRE(context.FindEntityByName("Player").HasComponent<Forge::ScriptComponent>());

    const std::string path = "test_assets/AgentBuiltScene.forge";
    REQUIRE(context.SaveScene(path));

    Forge::EditorContext reloaded;
    REQUIRE(reloaded.OpenScene(path));

    auto reloadedPlayer = reloaded.FindEntityByName("Player");
    REQUIRE(reloadedPlayer);
    REQUIRE(reloadedPlayer.GetComponent<Forge::TransformComponent>().Translation == glm::vec3(0.0f, 2.0f, 0.0f));
    REQUIRE(reloadedPlayer.GetComponent<Forge::RigidbodyComponent>().Type == Forge::RigidbodyComponent::BodyType::Dynamic);
    REQUIRE(reloadedPlayer.GetComponent<Forge::ScriptComponent>().ClassName == "Mover");
    REQUIRE(reloadedPlayer.GetComponent<Forge::PBRMaterialComponent>().Albedo == glm::vec3(0.8f, 0.2f, 0.2f));
    REQUIRE(reloaded.FindEntityByName("Floor").HasComponent<Forge::BoxColliderComponent>());
}

// ---------------------------------------------------------------------------
// Viewport picking (the Hazel SceneTree click-select, on the editor model)
// ---------------------------------------------------------------------------

TEST_CASE("SelectEntityAtRay selects the hit entity", "[editor][picking]")
{
    Forge::EditorContext context;
    context.NewScene();

    auto cube = context.CreateEntity("PickedCube");
    cube.GetComponent<Forge::TransformComponent>().Translation = { 0.0f, 0.0f, -5.0f };
    cube.AddComponent<Forge::BoxColliderComponent>().Size = { 1.0f, 1.0f, 1.0f };

    auto other = context.CreateEntity("Other");
    other.GetComponent<Forge::TransformComponent>().Translation = { 10.0f, 0.0f, -5.0f };
    other.AddComponent<Forge::BoxColliderComponent>().Size = { 1.0f, 1.0f, 1.0f };

    const Forge::Entity initial = context.CreateEntity("Initial");
    context.SelectEntity(initial);

    REQUIRE(context.SelectEntityAtRay({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }));
    REQUIRE(context.HasSelection());
    REQUIRE(context.GetSelectedEntity().GetName() == "PickedCube");
}

TEST_CASE("SelectEntityAtRay leaves the selection alone on a miss", "[editor][picking]")
{
    Forge::EditorContext context;
    context.NewScene();

    auto cube = context.CreateEntity("PickedCube");
    cube.GetComponent<Forge::TransformComponent>().Translation = { 0.0f, 0.0f, -5.0f };
    cube.AddComponent<Forge::BoxColliderComponent>().Size = { 1.0f, 1.0f, 1.0f };

    auto keeper = context.CreateEntity("Keeper");
    context.SelectEntity(keeper);

    // A ray that hits nothing reports false and must not clear the selection -
    // a miss in a viewport is usually the end of a camera drag, not a request
    // to forget what was selected.
    REQUIRE_FALSE(context.SelectEntityAtRay({ 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }));
    REQUIRE(context.HasSelection());
    REQUIRE(context.GetSelectedEntity().GetName() == "Keeper");
}
