#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include "Forge/Core/Log.h"
#include "Forge/Editor/EditorContext.h"
#include "Forge/Editor/Gizmo.h"
#include "Forge/Assets/AssetRegistry.h"
#include "Forge/Project/GameExporter.h"
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
	const std::string root = "test_assets";
	WriteFile(root + "/level.forge", "Scene: x");
	WriteFile(root + "/player.lua", "return {}");
	WriteFile(root + "/model.gltf", "{}");
	WriteFile(root + "/albedo.png", "png");
	WriteFile(root + "/notes.txt", "hello");

	Forge::AssetRegistry::Clear();
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
