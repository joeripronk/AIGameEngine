#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include "Forge/Core/Log.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/SceneSerializer.h"
#include "Forge/Scene/Prefab.h"
#include "Forge/Physics/Physics.h"
#include "Forge/Scripting/ScriptEngine.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

	void WriteFile(const std::string& path, const std::string& contents)
	{
		std::filesystem::path p(path);
		if (p.has_parent_path())
			std::filesystem::create_directories(p.parent_path());

		std::ofstream out(path, std::ios::binary);
		out << contents;
	}

	// The script and prefab directories are process-global, so a test that
	// points them at a temp folder would otherwise change the behaviour of
	// whichever test runs next. This restores whatever was set beforehand.
	struct DirectoryGuard
	{
		DirectoryGuard()
			: m_ScriptDir(Forge::ScriptEngine::GetScriptDirectory())
			, m_PrefabDir(Forge::PrefabLibrary::GetDirectory())
		{
		}

		~DirectoryGuard()
		{
			Forge::ScriptEngine::SetScriptDirectory(m_ScriptDir);
			Forge::PrefabLibrary::SetDirectory(m_PrefabDir);
		}

		std::string m_ScriptDir;
		std::string m_PrefabDir;
	};

	Forge::Scene& MakeFullScene(Forge::Scene& scene)
	{
		auto entity = scene.CreateEntity("Full");
		auto& tc = entity.GetComponent<Forge::TransformComponent>();
		tc.Translation = { 1.0f, 2.0f, 3.0f };
		tc.Rotation = { 10.0f, 20.0f, 30.0f };
		tc.Scale = { 2.0f, 2.0f, 2.0f };

		auto& camera = entity.AddComponent<Forge::CameraComponent>();
		camera.Primary = true;
		// Deliberately not the default: the runtime format has to carry a
		// projection the scene did not use before.
		camera.Type = Forge::CameraComponent::ProjectionType::Orthographic;
		camera.OrthographicSize = 24.0f;
		camera.NearClip = 0.5f;
		camera.FarClip = 250.0f;
		camera.AspectRatio = 2.0f;
		camera.UpdateProjection();
		entity.AddComponent<Forge::ScriptComponent>().ClassName = "FullScript";

		auto& rb = entity.AddComponent<Forge::RigidbodyComponent>();
		rb.Type = Forge::RigidbodyComponent::BodyType::Dynamic;
		rb.FixedRotation = true;

		auto& bc = entity.AddComponent<Forge::BoxColliderComponent>();
		bc.Offset = { 0.5f, 0.0f, 0.0f };
		bc.Size = { 2.0f, 1.0f, 1.0f };
		bc.Density = 2.0f;
		bc.Friction = 0.7f;
		bc.Restitution = 0.25f;

		auto& sc = entity.AddComponent<Forge::SphereColliderComponent>();
		sc.Offset = { 0.0f, 1.0f, 0.0f };
		sc.Radius = 1.5f;
		sc.Density = 1.5f;
		sc.Friction = 0.6f;
		sc.Restitution = 0.9f;

		auto& light = entity.AddComponent<Forge::LightComponent>();
		light.Type = Forge::LightComponent::Type::Point;
		light.Color = { 1.0f, 0.8f, 0.6f };
		light.Intensity = 2.5f;
		light.Direction = { 0.0f, -1.0f, 0.0f };
		light.Constant = 1.0f;
		light.Linear = 0.09f;
		light.Quadratic = 0.032f;
		light.CutOff = 0.9f;
		light.OuterCutOff = 0.8f;

		auto& pbr = entity.AddComponent<Forge::PBRMaterialComponent>();
		pbr.Albedo = { 0.9f, 0.1f, 0.2f };
		pbr.Metallic = 0.75f;
		pbr.Roughness = 0.25f;
		pbr.AO = 0.5f;
		pbr.Emissive = { 0.0f, 0.0f, 1.0f };
		pbr.EmissiveIntensity = 2.0f;
		pbr.AlbedoTexturePath = "textures/full_albedo.png";

		auto& audio = entity.AddComponent<Forge::AudioSourceComponent>();
		audio.SourcePath = "audio/full.wav";
		audio.Volume = 0.35f;
		audio.Pitch = 1.25f;
		audio.Loop = true;
		audio.PlayOnStart = false;
		audio.Bus = Forge::AudioBus::Music;
		audio.Spatial = true;
		audio.MinDistance = 2.0f;
		audio.MaxDistance = 40.0f;

		auto& mesh = entity.AddComponent<Forge::MeshComponent>();
		mesh.ModelPath = "models/full.gltf";
		mesh.Scale = { 1.5f, 1.5f, 1.5f };

		return scene;
	}

}

TEST_CASE("Scene serializer round-trips every component", "[serializer][feature]")
{
	auto scene = Forge::CreateRef<Forge::Scene>();
	MakeFullScene(*scene);

	const std::string path = "test_full_scene.forge";
	Forge::SceneSerializer serializer(scene);
	REQUIRE(serializer.Serialize(path));

	auto loaded = Forge::CreateRef<Forge::Scene>();
	Forge::SceneSerializer loader(loaded);
	REQUIRE(loader.Deserialize(path));

	auto entity = loaded->FindEntityByName("Full");
	REQUIRE(entity);

	auto& tc = entity.GetComponent<Forge::TransformComponent>();
	REQUIRE(tc.Translation == glm::vec3(1.0f, 2.0f, 3.0f));
	REQUIRE(tc.Rotation == glm::vec3(10.0f, 20.0f, 30.0f));
	REQUIRE(tc.Scale == glm::vec3(2.0f, 2.0f, 2.0f));

	REQUIRE(entity.GetComponent<Forge::CameraComponent>().Primary);
	REQUIRE(entity.GetComponent<Forge::ScriptComponent>().ClassName == "FullScript");

	auto& rb = entity.GetComponent<Forge::RigidbodyComponent>();
	REQUIRE(rb.Type == Forge::RigidbodyComponent::BodyType::Dynamic);
	REQUIRE(rb.FixedRotation);

	auto& bc = entity.GetComponent<Forge::BoxColliderComponent>();
	REQUIRE(bc.Offset == glm::vec3(0.5f, 0.0f, 0.0f));
	REQUIRE(bc.Size == glm::vec3(2.0f, 1.0f, 1.0f));
	REQUIRE(bc.Density == Approx(2.0f));
	REQUIRE(bc.Friction == Approx(0.7f));
	REQUIRE(bc.Restitution == Approx(0.25f));

	auto& sc = entity.GetComponent<Forge::SphereColliderComponent>();
	REQUIRE(sc.Offset == glm::vec3(0.0f, 1.0f, 0.0f));
	REQUIRE(sc.Radius == Approx(1.5f));
	REQUIRE(sc.Density == Approx(1.5f));
	REQUIRE(sc.Friction == Approx(0.6f));
	REQUIRE(sc.Restitution == Approx(0.9f));

	auto& light = entity.GetComponent<Forge::LightComponent>();
	REQUIRE(light.Type == Forge::LightComponent::Type::Point);
	REQUIRE(light.Color == glm::vec3(1.0f, 0.8f, 0.6f));
	REQUIRE(light.Intensity == Approx(2.5f));
	REQUIRE(light.Direction == glm::vec3(0.0f, -1.0f, 0.0f));
	REQUIRE(light.Constant == Approx(1.0f));
	REQUIRE(light.Linear == Approx(0.09f));
	REQUIRE(light.Quadratic == Approx(0.032f));
	REQUIRE(light.CutOff == Approx(0.9f));
	REQUIRE(light.OuterCutOff == Approx(0.8f));

	auto& pbr = entity.GetComponent<Forge::PBRMaterialComponent>();
	REQUIRE(pbr.Albedo == glm::vec3(0.9f, 0.1f, 0.2f));
	REQUIRE(pbr.Metallic == Approx(0.75f));
	REQUIRE(pbr.Roughness == Approx(0.25f));
	REQUIRE(pbr.AO == Approx(0.5f));
	REQUIRE(pbr.Emissive == glm::vec3(0.0f, 0.0f, 1.0f));
	REQUIRE(pbr.EmissiveIntensity == Approx(2.0f));

	auto& mesh = entity.GetComponent<Forge::MeshComponent>();
	REQUIRE(mesh.ModelPath == "models/full.gltf");
	REQUIRE(mesh.Scale == glm::vec3(1.5f, 1.5f, 1.5f));
}

TEST_CASE("Runtime serialization round-trips every component", "[serializer][runtime][feature]")
{
	auto scene = Forge::CreateRef<Forge::Scene>();
	MakeFullScene(*scene);

	const std::string path = "test_full_scene.rt";
	Forge::SceneSerializer serializer(scene);
	serializer.SerializeRuntime(path);

	auto loaded = Forge::CreateRef<Forge::Scene>();
	Forge::SceneSerializer loader(loaded);
	REQUIRE(loader.DeserializeRuntime(path));

	auto entity = loaded->FindEntityByName("Full");
	REQUIRE(entity);

	auto& tc = entity.GetComponent<Forge::TransformComponent>();
	REQUIRE(tc.Translation == glm::vec3(1.0f, 2.0f, 3.0f));
	REQUIRE(tc.Rotation == glm::vec3(10.0f, 20.0f, 30.0f));
	REQUIRE(tc.Scale == glm::vec3(2.0f, 2.0f, 2.0f));

	auto& rb = entity.GetComponent<Forge::RigidbodyComponent>();
	REQUIRE(rb.Type == Forge::RigidbodyComponent::BodyType::Dynamic);
	REQUIRE(rb.FixedRotation);

	auto& bc = entity.GetComponent<Forge::BoxColliderComponent>();
	REQUIRE(bc.Size == glm::vec3(2.0f, 1.0f, 1.0f));
	REQUIRE(bc.Density == Approx(2.0f));

	auto& sc = entity.GetComponent<Forge::SphereColliderComponent>();
	REQUIRE(sc.Radius == Approx(1.5f));

	auto& light = entity.GetComponent<Forge::LightComponent>();
	REQUIRE(light.Type == Forge::LightComponent::Type::Point);
	REQUIRE(light.Intensity == Approx(2.5f));

	auto& pbr = entity.GetComponent<Forge::PBRMaterialComponent>();
	REQUIRE(pbr.Albedo == glm::vec3(0.9f, 0.1f, 0.2f));
	REQUIRE(pbr.Metallic == Approx(0.75f));

	auto& mesh = entity.GetComponent<Forge::MeshComponent>();
	REQUIRE(mesh.ModelPath == "models/full.gltf");

	// The camera's projection and its parameters: dropped here, every runtime
	// camera reverted to a default perspective and the scene it shipped from
	// was not the scene that ran.
	const auto& camera = entity.GetComponent<Forge::CameraComponent>();
	REQUIRE(camera.Type == Forge::CameraComponent::ProjectionType::Orthographic);
	REQUIRE(camera.OrthographicSize == Approx(24.0f));
	REQUIRE(camera.NearClip == Approx(0.5f));
	REQUIRE(camera.FarClip == Approx(250.0f));
	REQUIRE(camera.AspectRatio == Approx(2.0f));

	// An albedo texture the runtime dropped leaves every object flat-coloured.
	REQUIRE(entity.GetComponent<Forge::PBRMaterialComponent>().AlbedoTexturePath
		== "textures/full_albedo.png");

	// Audio had no flag bit at all, so the whole layer vanished from a runtime
	// scene - every sound in the game was silently gone.
	const auto& audio = entity.GetComponent<Forge::AudioSourceComponent>();
	REQUIRE(audio.SourcePath == "audio/full.wav");
	REQUIRE(audio.Volume == Approx(0.35f));
	REQUIRE(audio.Pitch == Approx(1.25f));
	REQUIRE(audio.Loop);
	REQUIRE(audio.PlayOnStart == false);
	REQUIRE(audio.Bus == Forge::AudioBus::Music);
	REQUIRE(audio.Spatial);
	REQUIRE(audio.MinDistance == Approx(2.0f));
	REQUIRE(audio.MaxDistance == Approx(40.0f));
}

TEST_CASE("Runtime serialization rejects corrupt files", "[serializer][runtime][feature]")
{
	auto loaded = Forge::CreateRef<Forge::Scene>();
	Forge::SceneSerializer loader(loaded);

	// Missing file
	REQUIRE_FALSE(loader.DeserializeRuntime("does_not_exist.rt"));

	// Bad magic
	WriteFile("bad.rt", "not a runtime scene");
	REQUIRE_FALSE(loader.DeserializeRuntime("bad.rt"));
}

TEST_CASE("Physics writes simulation results back to scene transforms", "[physics][feature]")
{
	Forge::Log::Init();
	Forge::Physics::Init();

	Forge::Scene scene;
	auto entity = scene.CreateEntity("Falling");
	auto& tc = entity.GetComponent<Forge::TransformComponent>();
	tc.Translation = { 0.0f, 10.0f, 0.0f };

	auto& rb = entity.AddComponent<Forge::RigidbodyComponent>();
	rb.Type = Forge::RigidbodyComponent::BodyType::Dynamic;

	auto& bc = entity.AddComponent<Forge::BoxColliderComponent>();
	bc.Size = { 1.0f, 1.0f, 1.0f };

	scene.OnPhysicsStart();
	scene.OnSimulationStart();

	for (int i = 0; i < 10; i++)
		scene.OnUpdateSimulation(0.016f);

	auto& result = entity.GetComponent<Forge::TransformComponent>();
	REQUIRE(result.Translation.y < 10.0f);

	scene.OnSimulationStop();
	Forge::Physics::Shutdown();
}

TEST_CASE("Physics raycast hits a sphere", "[physics][raycast][feature]")
{
	Forge::Physics::Init();

	auto body = new Forge::PhysicsBody(7);
	body->Shape = Forge::PhysicsBody::ShapeType::Sphere;
	body->SphereRadius = 1.0f;
	body->Position = { 0.0f, 0.0f, 0.0f };
	Forge::Physics::AddBody(body);

	glm::vec3 hitPosition, hitNormal;
	uint64_t hitBodyID = 0;
	bool hit = Forge::Physics::Raycast({ 0.0f, 0.0f, 5.0f }, { 0.0f, 0.0f, -1.0f }, 10.0f, hitPosition, hitNormal, hitBodyID);

	REQUIRE(hit);
	REQUIRE(hitBodyID == 7);
	REQUIRE(hitPosition.z == Approx(1.0f));
	REQUIRE(hitNormal.z == Approx(1.0f));

	Forge::Physics::Shutdown();
}

TEST_CASE("Physics raycast hits a box", "[physics][raycast][feature]")
{
	Forge::Physics::Init();

	auto body = new Forge::PhysicsBody(9);
	body->Shape = Forge::PhysicsBody::ShapeType::Box;
	body->BoxSize = { 2.0f, 2.0f, 2.0f };
	body->Position = { 0.0f, 0.0f, 0.0f };
	Forge::Physics::AddBody(body);

	glm::vec3 hitPosition, hitNormal;
	uint64_t hitBodyID = 0;
	bool hit = Forge::Physics::Raycast({ 0.0f, 0.0f, 5.0f }, { 0.0f, 0.0f, -1.0f }, 10.0f, hitPosition, hitNormal, hitBodyID);

	REQUIRE(hit);
	REQUIRE(hitBodyID == 9);
	REQUIRE(hitPosition.z == Approx(1.0f));
	REQUIRE(hitNormal.z == Approx(1.0f));

	Forge::Physics::Shutdown();
}

TEST_CASE("Physics raycast misses when nothing is in the way", "[physics][raycast][feature]")
{
	Forge::Physics::Init();

	auto body = new Forge::PhysicsBody(3);
	body->Shape = Forge::PhysicsBody::ShapeType::Sphere;
	body->SphereRadius = 1.0f;
	body->Position = { 0.0f, 0.0f, 0.0f };
	Forge::Physics::AddBody(body);

	glm::vec3 hitPosition, hitNormal;
	uint64_t hitBodyID = 0;
	bool hit = Forge::Physics::Raycast({ 0.0f, 5.0f, 5.0f }, { 0.0f, 0.0f, -1.0f }, 10.0f, hitPosition, hitNormal, hitBodyID);

	REQUIRE_FALSE(hit);

	Forge::Physics::Shutdown();
}

TEST_CASE("Prefab create and instantiate", "[prefab][feature]")
{
	Forge::Scene scene;
	auto entity = scene.CreateEntity("Proto");
	auto& tc = entity.GetComponent<Forge::TransformComponent>();
	tc.Translation = { 1.0f, 2.0f, 3.0f };

	auto& rb = entity.AddComponent<Forge::RigidbodyComponent>();
	rb.Type = Forge::RigidbodyComponent::BodyType::Dynamic;

	auto& pbr = entity.AddComponent<Forge::PBRMaterialComponent>();
	pbr.Albedo = { 1.0f, 0.0f, 0.0f };

	const std::string path = "test_prefabs/Proto.forge";
	REQUIRE(Forge::Prefab::CreateFromEntity(entity, path));

	auto loaded = Forge::CreateRef<Forge::Scene>();
	auto instance = Forge::Prefab(path).Instantiate(loaded.get());

	REQUIRE(instance);
	REQUIRE(instance.GetName() == "Proto");
	REQUIRE(instance.HasComponent<Forge::RigidbodyComponent>());
	REQUIRE(instance.GetComponent<Forge::RigidbodyComponent>().Type == Forge::RigidbodyComponent::BodyType::Dynamic);
	REQUIRE(instance.GetComponent<Forge::PBRMaterialComponent>().Albedo == glm::vec3(1.0f, 0.0f, 0.0f));
	REQUIRE(instance.GetUUID() != entity.GetUUID());
}

TEST_CASE("Prefab instantiate with position override", "[prefab][feature]")
{
	DirectoryGuard directoryGuard;

	Forge::Scene scene;
	auto entity = scene.CreateEntity("Proto");
	entity.AddComponent<Forge::PBRMaterialComponent>();

	const std::string path = "test_prefabs/Proto.forge";
	REQUIRE(Forge::Prefab::CreateFromEntity(entity, path));

	auto loaded = Forge::CreateRef<Forge::Scene>();
	auto instance = Forge::Prefab(path).Instantiate(loaded.get(), true, { 4.0f, 5.0f, 6.0f });

	REQUIRE(instance);
	REQUIRE(instance.GetComponent<Forge::TransformComponent>().Translation == glm::vec3(4.0f, 5.0f, 6.0f));
}

TEST_CASE("Prefab library loads committed prefab", "[prefab][feature]")
{
	DirectoryGuard directoryGuard;

	Forge::PrefabLibrary::SetDirectory(std::string(FORGE_SOURCE_DIR) + "/resources/prefabs/");
	auto prefab = Forge::PrefabLibrary::Get("Enemy");
	REQUIRE(prefab);
	REQUIRE(prefab->IsValid());

	auto scene = Forge::CreateRef<Forge::Scene>();
	auto instance = prefab->Instantiate(scene.get());
	REQUIRE(instance);
	REQUIRE(instance.HasComponent<Forge::RigidbodyComponent>());
	REQUIRE(instance.HasComponent<Forge::BoxColliderComponent>());
}

TEST_CASE("Scripting: full API surface via Lua", "[scripting][feature]")
{
	DirectoryGuard directoryGuard;

	Forge::Log::Init();
	Forge::ScriptEngine::Init();

	const std::string scriptDir = "test_scripts";
	WriteFile(scriptDir + "/ApiExercise.lua", R"(
local M = {}

function M.OnCreate(entity)
    entity:SetName("RenamedByLua")
    assert(entity:GetName() == "RenamedByLua")

    entity:SetPosition(1, 2, 3)
    local px, py, pz = entity:GetPosition()
    assert(px == 1 and py == 2 and pz == 3)

    entity:SetScale(2, 2, 2)
    local sx, sy, sz = entity:GetScale()
    assert(sx == 2 and sy == 2 and sz == 2)

    entity:SetRotation(10, 20, 30)

    entity:AddComponent("Light")
    assert(entity:HasComponent("Light"))
    entity:SetFloat("Light", "Intensity", 3.5)
    entity:SetVector3("Light", "Color", 1, 0.5, 0.25)

    entity:AddComponent("Rigidbody")
    assert(entity:HasComponent("Rigidbody"))
    entity:SetBool("Rigidbody", "FixedRotation", true)

    entity:SetString("Script", "ClassName", "ApiExercise")

    local created = Scene.CreateEntity("ApiCreated")
    assert(created ~= nil)
    created:SetPosition(9, 9, 9)

    M.created = created
    M.entity = entity
end

function M.OnUpdate(entity, dt)
    M.updateCount = (M.updateCount or 0) + 1
    local x, y, z = entity:GetPosition()
    entity:SetPosition(x + dt, y, z)
end

return M
)");

	auto scene = Forge::CreateRef<Forge::Scene>();
	auto entity = scene->CreateEntity("ApiEntity");
	entity.AddComponent<Forge::ScriptComponent>().ClassName = "ApiExercise";

	Forge::ScriptEngine::SetScriptDirectory(scriptDir);
	Forge::ScriptEngine::OnRuntimeStart(scene.get());

	// OnCreate ran
	auto renamed = scene->FindEntityByName("RenamedByLua");
	REQUIRE(renamed);
	REQUIRE(renamed.GetComponent<Forge::TransformComponent>().Translation == glm::vec3(1.0f, 2.0f, 3.0f));
	REQUIRE(renamed.GetComponent<Forge::TransformComponent>().Scale == glm::vec3(2.0f, 2.0f, 2.0f));
	REQUIRE(renamed.HasComponent<Forge::LightComponent>());
	REQUIRE(renamed.GetComponent<Forge::LightComponent>().Intensity == Approx(3.5f));
	REQUIRE(renamed.HasComponent<Forge::RigidbodyComponent>());
	REQUIRE(renamed.GetComponent<Forge::RigidbodyComponent>().FixedRotation);

	auto created = scene->FindEntityByName("ApiCreated");
	REQUIRE(created);
	REQUIRE(created.GetComponent<Forge::TransformComponent>().Translation == glm::vec3(9.0f, 9.0f, 9.0f));

	// OnUpdate drives position
	for (int i = 0; i < 5; i++)
		Forge::ScriptEngine::OnUpdateEntity(renamed, Forge::Timestep(0.016f));

	auto& tc = renamed.GetComponent<Forge::TransformComponent>();
	REQUIRE(tc.Translation.x > 1.0f);

	Forge::ScriptEngine::OnRuntimeStop();
	Forge::ScriptEngine::Shutdown();
}

TEST_CASE("Scripting: entity create and destroy from Lua", "[scripting][feature]")
{
	DirectoryGuard directoryGuard;

	Forge::Log::Init();
	Forge::ScriptEngine::Init();

	const std::string scriptDir = "test_scripts";
	WriteFile(scriptDir + "/EntityOps.lua", R"(
local M = {}

function M.OnCreate(entity)
    local e = Scene.CreateEntity("LuaCreated")
    e:SetPosition(1, 1, 1)
    M.created = e
end

function M.OnUpdate(entity, dt)
    if M.created then
        Scene.DestroyEntity(M.created)
        M.created = nil
    end
end

return M
)");

	auto scene = Forge::CreateRef<Forge::Scene>();
	auto host = scene->CreateEntity("Host");
	host.AddComponent<Forge::ScriptComponent>().ClassName = "EntityOps";

	Forge::ScriptEngine::SetScriptDirectory(scriptDir);
	Forge::ScriptEngine::OnRuntimeStart(scene.get());

	auto created = scene->FindEntityByName("LuaCreated");
	REQUIRE(created);
	auto uuid = created.GetUUID();

	Forge::ScriptEngine::OnUpdateEntity(host, Forge::Timestep(0.016f));

	REQUIRE_FALSE(scene->GetEntityByUUID(uuid));

	Forge::ScriptEngine::OnRuntimeStop();
	Forge::ScriptEngine::Shutdown();
}

TEST_CASE("Scripting: committed Mover script drives OnUpdate", "[scripting][feature]")
{
	DirectoryGuard directoryGuard;

	Forge::Log::Init();
	Forge::ScriptEngine::Init();

	auto scene = Forge::CreateRef<Forge::Scene>();
	auto entity = scene->CreateEntity("Mover");
	entity.AddComponent<Forge::ScriptComponent>().ClassName = "Mover";

	Forge::ScriptEngine::SetScriptDirectory(std::string(FORGE_SOURCE_DIR) + "/resources/scripts/");
	Forge::ScriptEngine::OnRuntimeStart(scene.get());

	auto mover = scene->FindEntityByName("Mover");
	REQUIRE(mover);
	REQUIRE(mover.GetComponent<Forge::TransformComponent>().Translation == glm::vec3(0.0f, 0.0f, 0.0f));

	for (int i = 0; i < 5; i++)
		Forge::ScriptEngine::OnUpdateEntity(mover, Forge::Timestep(0.016f));

	auto& tc = mover.GetComponent<Forge::TransformComponent>();
	REQUIRE(tc.Translation.x > 0.0f);

	Forge::ScriptEngine::OnRuntimeStop();
	Forge::ScriptEngine::Shutdown();
}

TEST_CASE("Scripting: SpawnPrefab from Lua", "[scripting][feature]")
{
	DirectoryGuard directoryGuard;

	Forge::Log::Init();
	Forge::ScriptEngine::Init();

	auto scene = Forge::CreateRef<Forge::Scene>();
	auto host = scene->CreateEntity("Spawner");
	host.AddComponent<Forge::ScriptComponent>().ClassName = "Spawner";

	Forge::ScriptEngine::SetScriptDirectory(std::string(FORGE_SOURCE_DIR) + "/resources/scripts/");
	Forge::PrefabLibrary::SetDirectory(std::string(FORGE_SOURCE_DIR) + "/resources/prefabs/");
	Forge::ScriptEngine::OnRuntimeStart(scene.get());

	// Spawner's OnCreate creates a plain entity; SpawnPrefab is exercised via Lua.
	std::string error;
	REQUIRE(Forge::ScriptEngine::ExecuteString("local e = SpawnPrefab('Enemy') return e ~= nil", &error));

	Forge::ScriptEngine::OnRuntimeStop();
	Forge::ScriptEngine::Shutdown();
}

TEST_CASE("Scripting: ExecuteString reports errors", "[scripting][feature]")
{
	Forge::Log::Init();
	Forge::ScriptEngine::Init();

	std::string error;
	REQUIRE_FALSE(Forge::ScriptEngine::ExecuteString("this is not lua", &error));
	REQUIRE_FALSE(error.empty());

	Forge::ScriptEngine::Shutdown();
}
