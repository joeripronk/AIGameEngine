// Game export and prefab authoring.
//
// Both write files that an AI agent then depends on, so a silent failure here
// surfaces much later as "the shipped game is missing its scene" or "spawning
// the prefab does nothing". These tests pin the contracts that catch that.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include <filesystem>
#include <fstream>
#include <string>

#include "Forge/Core/Base.h"
#include "Forge/Project/GameExporter.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/SceneSerializer.h"
#include "Forge/Scene/Prefab.h"
#include "Forge/Physics/Physics.h"

namespace {

	namespace fs = std::filesystem;

	std::string ExportDir(const char* name)
	{
		return std::string("test_export_fixtures/") + name;
	}

	void WriteText(const std::string& path, const std::string& contents)
	{
		std::error_code ec;
		fs::create_directories(fs::path(path).parent_path(), ec);
		std::ofstream out(path, std::ios::binary);
		out << contents;
	}

	void ResetDir(const std::string& path)
	{
		std::error_code ec;
		fs::remove_all(path, ec);
		fs::create_directories(path, ec);
	}

	// Builds a small but complete source project: an executable stand-in, a
	// scene, and a nested resource tree.
	void BuildSourceProject(const std::string& root)
	{
		ResetDir(root);
		WriteText(root + "/ForgeRuntime", "not a real binary, but a regular file");
		WriteText(root + "/level.forge", "Scene: Level\nEntities: []\n");
		WriteText(root + "/resources/scripts/Player.lua", "return {}");
		WriteText(root + "/resources/scripts/nested/Helper.lua", "return {}");
		WriteText(root + "/resources/models/cube.glb", "glb");
		WriteText(root + "/resources/prefabs/Enemy.forge", "Scene: Prefab\nEntities: []\n");
	}

} // namespace

// ---------------------------------------------------------------------------
// GameExporter
// ---------------------------------------------------------------------------

TEST_CASE("Export produces a runnable-looking package", "[export]")
{
	const std::string source = ExportDir("src_ok");
	const std::string output = ExportDir("out_ok");
	BuildSourceProject(source);

	std::error_code ec;
	fs::remove_all(output, ec);

	Forge::GameExporter::Options options;
	options.OutputDirectory = output;
	options.RuntimeExecutable = source + "/ForgeRuntime";
	options.ScenePath = source + "/level.forge";
	options.ResourcesDirectory = source + "/resources";
	options.GameName = "TestGame";

	REQUIRE(Forge::GameExporter::Export(options));

	// The executable and the scene must be present alongside each other.
	REQUIRE(fs::is_regular_file(output + "/ForgeRuntime"));
	REQUIRE(fs::is_regular_file(output + "/level.forge"));

	// Resources must keep their directory structure, including nested folders.
	REQUIRE(fs::is_regular_file(output + "/resources/scripts/Player.lua"));
	REQUIRE(fs::is_regular_file(output + "/resources/scripts/nested/Helper.lua"));
	REQUIRE(fs::is_regular_file(output + "/resources/models/cube.glb"));
	REQUIRE(fs::is_regular_file(output + "/resources/prefabs/Enemy.forge"));
}

TEST_CASE("Exported files have identical contents to their sources", "[export]")
{
	const std::string source = ExportDir("src_content");
	const std::string output = ExportDir("out_content");
	BuildSourceProject(source);

	std::error_code ec;
	fs::remove_all(output, ec);

	Forge::GameExporter::Options options;
	options.OutputDirectory = output;
	options.RuntimeExecutable = source + "/ForgeRuntime";
	options.ScenePath = source + "/level.forge";
	options.ResourcesDirectory = source + "/resources";

	REQUIRE(Forge::GameExporter::Export(options));

	const auto readAll = [](const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	};

	// A copied file that exists but is empty or truncated is still broken, so
	// compare bytes rather than just checking existence.
	REQUIRE(readAll(output + "/ForgeRuntime") == readAll(source + "/ForgeRuntime"));
	REQUIRE(readAll(output + "/level.forge") == readAll(source + "/level.forge"));
	REQUIRE(readAll(output + "/resources/scripts/Player.lua") == readAll(source + "/resources/scripts/Player.lua"));
}

TEST_CASE("Export fails when the runtime executable is missing", "[export]")
{
	const std::string source = ExportDir("src_missing_exe");
	const std::string output = ExportDir("out_missing_exe");
	BuildSourceProject(source);

	std::error_code ec;
	fs::remove_all(output, ec);

	Forge::GameExporter::Options options;
	options.OutputDirectory = output;
	options.RuntimeExecutable = source + "/DoesNotExist";
	options.ScenePath = source + "/level.forge";
	options.ResourcesDirectory = source + "/resources";

	// Reporting success here would ship a package with no game in it.
	REQUIRE(Forge::GameExporter::Export(options) == false);

	// A failed export must not leave a partial package that looks usable.
	REQUIRE_FALSE(fs::is_regular_file(output + "/level.forge"));
}

TEST_CASE("Export fails when the scene is missing", "[export]")
{
	const std::string source = ExportDir("src_missing_scene");
	const std::string output = ExportDir("out_missing_scene");
	BuildSourceProject(source);

	std::error_code ec;
	fs::remove_all(output, ec);

	Forge::GameExporter::Options options;
	options.OutputDirectory = output;
	options.RuntimeExecutable = source + "/ForgeRuntime";
	options.ScenePath = source + "/NoSuchScene.forge";

	REQUIRE(Forge::GameExporter::Export(options) == false);
}

TEST_CASE("Export fails when the resources directory is missing", "[export]")
{
	const std::string source = ExportDir("src_missing_res");
	const std::string output = ExportDir("out_missing_res");
	BuildSourceProject(source);

	std::error_code ec;
	fs::remove_all(output, ec);

	Forge::GameExporter::Options options;
	options.OutputDirectory = output;
	options.RuntimeExecutable = source + "/ForgeRuntime";
	options.ResourcesDirectory = source + "/no_such_resources";

	// Silently producing a game with no scripts, models or prefabs would be a
	// confusing failure to diagnose later.
	REQUIRE(Forge::GameExporter::Export(options) == false);
}

TEST_CASE("Export rejects an empty output directory", "[export]")
{
	Forge::GameExporter::Options options;
	options.OutputDirectory = "";
	options.ScenePath = "anything.forge";

	REQUIRE(Forge::GameExporter::Export(options) == false);
}

TEST_CASE("Export succeeds when optional inputs are omitted", "[export]")
{
	const std::string output = ExportDir("out_minimal");
	std::error_code ec;
	fs::remove_all(output, ec);

	Forge::GameExporter::Options options;
	options.OutputDirectory = output;
	// No executable, no scene, no resources: still a valid (if empty) package.

	REQUIRE(Forge::GameExporter::Export(options));
	REQUIRE(fs::is_directory(output));
}

TEST_CASE("Export overwrites files from a previous run", "[export]")
{
	const std::string source = ExportDir("src_overwrite");
	const std::string output = ExportDir("out_overwrite");
	BuildSourceProject(source);

	std::error_code ec;
	fs::remove_all(output, ec);

	Forge::GameExporter::Options options;
	options.OutputDirectory = output;
	options.RuntimeExecutable = source + "/ForgeRuntime";
	options.ScenePath = source + "/level.forge";
	options.ResourcesDirectory = source + "/resources";

	REQUIRE(Forge::GameExporter::Export(options));

	// Change the source and re-export: the package must reflect the new content
	// rather than keeping the stale copy.
	WriteText(source + "/level.forge", "Scene: Changed\nEntities: []\n");
	REQUIRE(Forge::GameExporter::Export(options));

	std::ifstream in(output + "/level.forge", std::ios::binary);
	const std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	REQUIRE(contents.find("Changed") != std::string::npos);
}

TEST_CASE("Export normalizes a missing trailing separator", "[export]")
{
	const std::string source = ExportDir("src_sep");
	const std::string output = ExportDir("out_sep");   // deliberately no trailing slash
	BuildSourceProject(source);

	std::error_code ec;
	fs::remove_all(output, ec);

	Forge::GameExporter::Options options;
	options.OutputDirectory = output;
	options.ScenePath = source + "/level.forge";

	REQUIRE(Forge::GameExporter::Export(options));
	REQUIRE(fs::is_regular_file(output + "/level.forge"));
}

TEST_CASE("An exported scene can be loaded back", "[export]")
{
	const std::string source = ExportDir("src_loadable");
	const std::string output = ExportDir("out_loadable");
	BuildSourceProject(source);

	std::error_code ec;
	fs::remove_all(output, ec);

	// Author a real scene and ship it, rather than a fixture.
	const std::string authored = source + "/authored.forge";
	{
		auto scene = Forge::CreateRef<Forge::Scene>();
		auto entity = scene->CreateEntity("Shipped");
		entity.GetComponent<Forge::TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };
		entity.AddComponent<Forge::ScriptComponent>().ClassName = "Mover";

		Forge::SceneSerializer serializer(scene);
		REQUIRE(serializer.Serialize(authored));
	}

	Forge::GameExporter::Options options;
	options.OutputDirectory = output;
	options.ScenePath = authored;
	options.ResourcesDirectory = source + "/resources";

	REQUIRE(Forge::GameExporter::Export(options));

	// The whole point of shipping a scene is that the runtime can load it.
	auto loaded = Forge::CreateRef<Forge::Scene>();
	Forge::SceneSerializer reader(loaded);
	REQUIRE(reader.Deserialize(output + "/authored.forge"));

	auto shipped = loaded->FindEntityByName("Shipped");
	REQUIRE(shipped);
	REQUIRE(shipped.GetComponent<Forge::TransformComponent>().Translation == glm::vec3(1.0f, 2.0f, 3.0f));
	REQUIRE(shipped.GetComponent<Forge::ScriptComponent>().ClassName == "Mover");
}

// ---------------------------------------------------------------------------
// Prefabs
// ---------------------------------------------------------------------------

namespace {

	// A prefab source entity exercising several component types.
	Forge::Entity MakePrefabSource(Forge::Scene& scene, const std::string& name)
	{
		auto entity = scene.CreateEntity(name);

		auto& tc = entity.GetComponent<Forge::TransformComponent>();
		tc.Translation = { 5.0f, 6.0f, 7.0f };
		tc.Scale = { 1.5f, 1.5f, 1.5f };

		auto& rb = entity.AddComponent<Forge::RigidbodyComponent>();
		rb.Type = Forge::RigidbodyComponent::BodyType::Dynamic;
		rb.FixedRotation = true;

		auto& bc = entity.AddComponent<Forge::BoxColliderComponent>();
		bc.Size = glm::vec3(2.0f, 2.0f, 2.0f);
		bc.Density = 3.0f;

		auto& pbr = entity.AddComponent<Forge::PBRMaterialComponent>();
		pbr.Albedo = { 0.1f, 0.2f, 0.3f };
		pbr.Metallic = 0.75f;

		entity.AddComponent<Forge::ScriptComponent>().ClassName = "Enemy";

		return entity;
	}

} // namespace

TEST_CASE("Prefab round-trips every component", "[prefab]")
{
	std::error_code ec;
	fs::create_directories("test_prefab_fixtures", ec);

	Forge::Scene source;
	auto entity = MakePrefabSource(source, "Enemy");

	const std::string path = "test_prefab_fixtures/Enemy.forge";
	REQUIRE(Forge::Prefab::CreateFromEntity(entity, path));

	Forge::Prefab prefab(path);
	REQUIRE(prefab.IsValid());

	Forge::Scene target;
	auto instance = prefab.Instantiate(&target);
	REQUIRE(instance);

	REQUIRE(instance.GetComponent<Forge::TransformComponent>().Translation == glm::vec3(5.0f, 6.0f, 7.0f));
	REQUIRE(instance.GetComponent<Forge::TransformComponent>().Scale == glm::vec3(1.5f, 1.5f, 1.5f));
	REQUIRE(instance.GetComponent<Forge::RigidbodyComponent>().Type == Forge::RigidbodyComponent::BodyType::Dynamic);
	REQUIRE(instance.GetComponent<Forge::RigidbodyComponent>().FixedRotation == true);
	REQUIRE(instance.GetComponent<Forge::BoxColliderComponent>().Size == glm::vec3(2.0f, 2.0f, 2.0f));
	REQUIRE(instance.GetComponent<Forge::BoxColliderComponent>().Density == Approx(3.0f));
	REQUIRE(instance.GetComponent<Forge::PBRMaterialComponent>().Albedo == glm::vec3(0.1f, 0.2f, 0.3f));
	REQUIRE(instance.GetComponent<Forge::PBRMaterialComponent>().Metallic == Approx(0.75f));
	REQUIRE(instance.GetComponent<Forge::ScriptComponent>().ClassName == "Enemy");
}

TEST_CASE("Each prefab instance gets a fresh identity", "[prefab]")
{
	std::error_code ec;
	fs::create_directories("test_prefab_fixtures", ec);

	Forge::Scene source;
	auto entity = MakePrefabSource(source, "Enemy");

	const std::string path = "test_prefab_fixtures/Identity.forge";
	REQUIRE(Forge::Prefab::CreateFromEntity(entity, path));

	Forge::Prefab prefab(path);

	Forge::Scene target;
	auto first = prefab.Instantiate(&target);
	auto second = prefab.Instantiate(&target);

	REQUIRE(first);
	REQUIRE(second);

	// Two instances of one prefab must never share an identity, or saving the
	// scene would collapse them into a single entity.
	REQUIRE(first.GetUUID() != second.GetUUID());
	REQUIRE(first != second);

	// Both must exist independently.
	REQUIRE(target.FindEntityByName("Enemy"));
	REQUIRE(target.GetEntitiesInOrder().size() == 2);
}

TEST_CASE("Instantiating a prefab with a position override moves only the clone", "[prefab]")
{
	std::error_code ec;
	fs::create_directories("test_prefab_fixtures", ec);

	Forge::Scene source;
	auto entity = MakePrefabSource(source, "Enemy");
	entity.GetComponent<Forge::TransformComponent>().Translation = { 5.0f, 6.0f, 7.0f };

	const std::string path = "test_prefab_fixtures/Override.forge";
	REQUIRE(Forge::Prefab::CreateFromEntity(entity, path));

	Forge::Prefab prefab(path);

	Forge::Scene target;
	auto overridden = prefab.Instantiate(&target, true, { 100.0f, 200.0f, 300.0f });
	REQUIRE(overridden);
	REQUIRE(overridden.GetComponent<Forge::TransformComponent>().Translation == glm::vec3(100.0f, 200.0f, 300.0f));

	// The override must apply per instance, not to the prefab's stored data.
	auto untouched = prefab.Instantiate(&target);
	REQUIRE(untouched.GetComponent<Forge::TransformComponent>().Translation == glm::vec3(5.0f, 6.0f, 7.0f));
}

TEST_CASE("Instantiating without an override keeps the prefab position", "[prefab]")
{
	std::error_code ec;
	fs::create_directories("test_prefab_fixtures", ec);

	Forge::Scene source;
	auto entity = MakePrefabSource(source, "Enemy");

	const std::string path = "test_prefab_fixtures/NoOverride.forge";
	REQUIRE(Forge::Prefab::CreateFromEntity(entity, path));

	Forge::Prefab prefab(path);

	Forge::Scene target;
	auto instance = prefab.Instantiate(&target, false, { 999.0f, 999.0f, 999.0f });
	REQUIRE(instance);
	REQUIRE(instance.GetComponent<Forge::TransformComponent>().Translation == glm::vec3(5.0f, 6.0f, 7.0f));
}

TEST_CASE("An invalid prefab file reports failure instead of crashing", "[prefab]")
{
	std::error_code ec;
	fs::create_directories("test_prefab_fixtures", ec);
	WriteText("test_prefab_fixtures/Broken.forge", "this is not: [valid, yaml");

	Forge::Prefab prefab("test_prefab_fixtures/Broken.forge");

	Forge::Scene target;
	REQUIRE_FALSE(prefab.IsValid());

	// A malformed prefab must produce no entity rather than a half-built one.
	REQUIRE_FALSE(prefab.Instantiate(&target));
}

TEST_CASE("PrefabLibrary loads by name and reports misses", "[prefab]")
{
	std::error_code ec;
	const std::string dir = "test_prefab_fixtures/library";
	fs::create_directories(dir, ec);

	Forge::Scene source;
	auto entity = MakePrefabSource(source, "Soldier");
	REQUIRE(Forge::Prefab::CreateFromEntity(entity, dir + "/Soldier.forge"));

	Forge::PrefabLibrary::Clear();
	Forge::PrefabLibrary::SetDirectory(dir + "/");

	auto prefab = Forge::PrefabLibrary::Get("Soldier");
	REQUIRE(prefab);
	REQUIRE(prefab->IsValid());

	// Cached: a second lookup returns the same object.
	REQUIRE(Forge::PrefabLibrary::Get("Soldier") == prefab);

	REQUIRE_FALSE(Forge::PrefabLibrary::Get("NoSuchThing"));

	Forge::PrefabLibrary::Clear();
}

TEST_CASE("PrefabLibrary survives a directory change", "[prefab]")
{
	std::error_code ec;
	const std::string first = "test_prefab_fixtures/lib_a";
	const std::string second = "test_prefab_fixtures/lib_b";
	fs::create_directories(first, ec);
	fs::create_directories(second, ec);

	Forge::Scene source;
	auto entity = MakePrefabSource(source, "Shared");

	REQUIRE(Forge::Prefab::CreateFromEntity(entity, first + "/Shared.forge"));
	REQUIRE(Forge::Prefab::CreateFromEntity(entity, second + "/Shared.forge"));

	Forge::PrefabLibrary::Clear();

	Forge::PrefabLibrary::SetDirectory(first + "/");
	REQUIRE(Forge::PrefabLibrary::Get("Shared"));

	// Same name, different directory: a cached prefab must not leak across
	// projects.
	Forge::PrefabLibrary::SetDirectory(second + "/");
	REQUIRE(Forge::PrefabLibrary::Get("Shared"));

	Forge::PrefabLibrary::Clear();
}

TEST_CASE("Spawning from a prefab inside a running scene works", "[prefab]")
{
	std::error_code ec;
	const std::string dir = "test_prefab_fixtures/spawn";
	fs::create_directories(dir, ec);

	Forge::Scene source;
	auto entity = MakePrefabSource(source, "Falling");
	REQUIRE(Forge::Prefab::CreateFromEntity(entity, dir + "/Falling.forge"));

	Forge::PrefabLibrary::Clear();
	Forge::PrefabLibrary::SetDirectory(dir + "/");

	// Simulates what Lua's SpawnPrefab does: instantiate into a live scene and
	// expect physics to pick the new body up.
	Forge::Physics::Init();

	Forge::Scene scene;
	scene.OnPhysicsStart();

	auto spawned = Forge::PrefabLibrary::Get("Falling")->Instantiate(&scene, true, { 0.0f, 10.0f, 0.0f });
	REQUIRE(spawned);

	scene.OnUpdateSimulation(Forge::Timestep(0.016f));
	REQUIRE(spawned.GetComponent<Forge::TransformComponent>().Translation.y < 10.0f);

	scene.OnPhysicsStop();
	Forge::PrefabLibrary::Clear();
}