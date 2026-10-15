// Serialization round-trip fidelity.
//
// The boundary where authored data crosses into and out of the engine is where
// silent data loss is most damaging: a dropped field corrupts every saved game
// without any error being reported.
//
// Each test below sets EVERY field of a component to a distinctive non-default
// value, saves, reloads, and compares. A field that is not round-tripped fails
// here rather than in someone's exported build.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Forge/Core/Base.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/SceneSerializer.h"

namespace {

	std::string TempPath(const char* name)
	{
		return std::string("test_roundtrip/") + name;
	}

	// A deliberately distinctive value per axis so a swapped or transposed field
	// cannot pass.
	const glm::vec3 kDistinct = { 11.5f, -22.25f, 33.125f };

	// Round-trips a scene through disk. Returns false if the write or the read
	// failed; the caller asserts on that. This deliberately avoids assertion
	// macros: Catch2 signals failure by throwing, and throwing through a
	// non-void return aborts the process instead of reporting the test.
	bool RoundTrip(const Forge::Ref<Forge::Scene>& source, const char* name,
		Forge::Ref<Forge::Scene>& reloaded)
	{
		const std::string path = TempPath(name);
		std::error_code ec;
		std::filesystem::create_directories("test_roundtrip", ec);

		Forge::SceneSerializer serializer(source);
		serializer.Serialize(path);

		reloaded = Forge::CreateRef<Forge::Scene>();
		Forge::SceneSerializer reader(reloaded);
		return reader.Deserialize(path);
	}

} // namespace

// ---------------------------------------------------------------------------
// Per-component fidelity
// ---------------------------------------------------------------------------

TEST_CASE("TransformComponent round-trips every field", "[serialization][roundtrip]")
{
	Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();
	auto entity = scene->CreateEntity("Transform");

	// CreateEntity already adds a TransformComponent.
	auto& tc = entity.GetComponent<Forge::TransformComponent>();
	tc.Translation = kDistinct;
	tc.Rotation = glm::vec3(1.5f, -2.25f, 3.75f);
	tc.Scale = glm::vec3(4.0f, 0.25f, 2.5f);

	Forge::Ref<Forge::Scene> reloaded;
	REQUIRE(RoundTrip(scene, "transform.forge", reloaded));
	auto e = reloaded->FindEntityByName("Transform");
	REQUIRE(e);

	const auto& rtc = e.GetComponent<Forge::TransformComponent>();
	REQUIRE(rtc.Translation == kDistinct);
	REQUIRE(rtc.Rotation == glm::vec3(1.5f, -2.25f, 3.75f));
	REQUIRE(rtc.Scale == glm::vec3(4.0f, 0.25f, 2.5f));
}

TEST_CASE("CameraComponent round-trips every field", "[serialization][roundtrip]")
{
	Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();
	auto entity = scene->CreateEntity("Camera");

	auto& camera = entity.AddComponent<Forge::CameraComponent>();
	camera.Primary = false;
	camera.FieldOfView = 73.5f;
	camera.NearClip = 0.25f;
	camera.FarClip = 4321.0f;
	camera.AspectRatio = 2.35f;

	Forge::Ref<Forge::Scene> reloaded;
	REQUIRE(RoundTrip(scene, "camera.forge", reloaded));
	auto e = reloaded->FindEntityByName("Camera");
	REQUIRE(e);

	const auto& rcam = e.GetComponent<Forge::CameraComponent>();
	REQUIRE(rcam.Primary == false);
	REQUIRE(rcam.FieldOfView == Approx(73.5f));
	REQUIRE(rcam.NearClip == Approx(0.25f));
	REQUIRE(rcam.FarClip == Approx(4321.0f));
	REQUIRE(rcam.AspectRatio == Approx(2.35f));
}

TEST_CASE("ScriptComponent round-trips its class name", "[serialization][roundtrip]")
{
	Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();
	auto entity = scene->CreateEntity("Scripted");
	entity.AddComponent<Forge::ScriptComponent>().ClassName = "PlayerController";

	Forge::Ref<Forge::Scene> reloaded;
	REQUIRE(RoundTrip(scene, "script.forge", reloaded));
	auto e = reloaded->FindEntityByName("Scripted");
	REQUIRE(e);
	REQUIRE(e.GetComponent<Forge::ScriptComponent>().ClassName == "PlayerController");
}

TEST_CASE("RigidbodyComponent round-trips body type and fixed rotation", "[serialization][roundtrip]")
{
	// Every body type must survive, not just the default.
	for (auto type : { Forge::RigidbodyComponent::BodyType::Static,
	                   Forge::RigidbodyComponent::BodyType::Dynamic,
	                   Forge::RigidbodyComponent::BodyType::Kinematic })
	{
		Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();
		auto entity = scene->CreateEntity("Body");

		auto& rb = entity.AddComponent<Forge::RigidbodyComponent>();
		rb.Type = type;
		rb.FixedRotation = true;

		Forge::Ref<Forge::Scene> reloaded;
	REQUIRE(RoundTrip(scene, "rigidbody.forge", reloaded));
		auto e = reloaded->FindEntityByName("Body");
		REQUIRE(e);

		const auto& rrb = e.GetComponent<Forge::RigidbodyComponent>();
		REQUIRE(rrb.Type == type);
		REQUIRE(rrb.FixedRotation == true);
	}
}

TEST_CASE("BoxColliderComponent round-trips every field", "[serialization][roundtrip]")
{
	Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();
	auto entity = scene->CreateEntity("Box");

	auto& bc = entity.AddComponent<Forge::BoxColliderComponent>();
	bc.Offset = kDistinct;
	bc.Size = glm::vec3(2.5f, 3.75f, 5.125f);
	bc.Density = 2.75f;
	bc.Friction = 0.875f;
	bc.Restitution = 0.625f;

	Forge::Ref<Forge::Scene> reloaded;
	REQUIRE(RoundTrip(scene, "box.forge", reloaded));
	auto e = reloaded->FindEntityByName("Box");
	REQUIRE(e);

	const auto& rbc = e.GetComponent<Forge::BoxColliderComponent>();
	REQUIRE(rbc.Offset == kDistinct);
	REQUIRE(rbc.Size == glm::vec3(2.5f, 3.75f, 5.125f));
	REQUIRE(rbc.Density == Approx(2.75f));
	REQUIRE(rbc.Friction == Approx(0.875f));
	REQUIRE(rbc.Restitution == Approx(0.625f));
}

TEST_CASE("SphereColliderComponent round-trips every field", "[serialization][roundtrip]")
{
	Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();
	auto entity = scene->CreateEntity("Sphere");

	auto& sc = entity.AddComponent<Forge::SphereColliderComponent>();
	sc.Offset = kDistinct;
	sc.Radius = 7.125f;
	sc.Density = 3.5f;
	sc.Friction = 0.375f;
	sc.Restitution = 0.9375f;

	Forge::Ref<Forge::Scene> reloaded;
	REQUIRE(RoundTrip(scene, "sphere.forge", reloaded));
	auto e = reloaded->FindEntityByName("Sphere");
	REQUIRE(e);

	const auto& rsc = e.GetComponent<Forge::SphereColliderComponent>();
	REQUIRE(rsc.Offset == kDistinct);
	REQUIRE(rsc.Radius == Approx(7.125f));
	REQUIRE(rsc.Density == Approx(3.5f));
	REQUIRE(rsc.Friction == Approx(0.375f));
	REQUIRE(rsc.Restitution == Approx(0.9375f));
}

TEST_CASE("PBRMaterialComponent round-trips every field", "[serialization][roundtrip]")
{
	Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();
	auto entity = scene->CreateEntity("Material");

	auto& pbr = entity.AddComponent<Forge::PBRMaterialComponent>();
	pbr.Albedo = kDistinct;
	pbr.Metallic = 0.875f;
	pbr.Roughness = 0.125f;
	pbr.AO = 0.625f;
	pbr.Emissive = glm::vec3(-4.5f, 5.25f, 6.125f);
	pbr.EmissiveIntensity = 3.5f;

	Forge::Ref<Forge::Scene> reloaded;
	REQUIRE(RoundTrip(scene, "pbr.forge", reloaded));
	auto e = reloaded->FindEntityByName("Material");
	REQUIRE(e);

	const auto& rpbr = e.GetComponent<Forge::PBRMaterialComponent>();
	REQUIRE(rpbr.Albedo == kDistinct);
	REQUIRE(rpbr.Metallic == Approx(0.875f));
	REQUIRE(rpbr.Roughness == Approx(0.125f));
	REQUIRE(rpbr.AO == Approx(0.625f));
	REQUIRE(rpbr.Emissive == glm::vec3(-4.5f, 5.25f, 6.125f));
	REQUIRE(rpbr.EmissiveIntensity == Approx(3.5f));
}

TEST_CASE("LightComponent round-trips every field", "[serialization][roundtrip]")
{
	// Each light type carries different attenuation semantics, so all three must
	// survive the round-trip.
	for (auto type : { Forge::LightComponent::Type::Directional,
	                   Forge::LightComponent::Type::Point,
	                   Forge::LightComponent::Type::Spot })
	{
		Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();
		auto entity = scene->CreateEntity("Light");

		auto& light = entity.AddComponent<Forge::LightComponent>();
		light.Type = type;
		light.Color = kDistinct;
		light.Intensity = 2.75f;
		light.Direction = glm::vec3(0.25f, -0.5f, 0.75f);
		light.Constant = 1.25f;
		light.Linear = 0.375f;
		light.Quadratic = 0.0625f;
		light.CutOff = 0.9375f;
		light.OuterCutOff = 0.8125f;

		Forge::Ref<Forge::Scene> reloaded;
	REQUIRE(RoundTrip(scene, "light.forge", reloaded));
		auto e = reloaded->FindEntityByName("Light");
		REQUIRE(e);

		const auto& rlight = e.GetComponent<Forge::LightComponent>();
		REQUIRE(rlight.Type == type);
		REQUIRE(rlight.Color == kDistinct);
		REQUIRE(rlight.Intensity == Approx(2.75f));
		REQUIRE(rlight.Direction == glm::vec3(0.25f, -0.5f, 0.75f));
		REQUIRE(rlight.Constant == Approx(1.25f));
		REQUIRE(rlight.Linear == Approx(0.375f));
		REQUIRE(rlight.Quadratic == Approx(0.0625f));
		REQUIRE(rlight.CutOff == Approx(0.9375f));
		REQUIRE(rlight.OuterCutOff == Approx(0.8125f));
	}
}

TEST_CASE("MeshComponent round-trips path and scale", "[serialization][roundtrip]")
{
	Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();
	auto entity = scene->CreateEntity("Mesh");

	auto& mesh = entity.AddComponent<Forge::MeshComponent>();
	mesh.ModelPath = "resources/models/cube.glb";
	mesh.Scale = glm::vec3(1.5f, 2.25f, 3.125f);

	Forge::Ref<Forge::Scene> reloaded;
	REQUIRE(RoundTrip(scene, "mesh.forge", reloaded));
	auto e = reloaded->FindEntityByName("Mesh");
	REQUIRE(e);

	const auto& rmesh = e.GetComponent<Forge::MeshComponent>();
	REQUIRE(rmesh.ModelPath == "resources/models/cube.glb");
	REQUIRE(rmesh.Scale == glm::vec3(1.5f, 2.25f, 3.125f));
}

TEST_CASE("EnvironmentMapComponent round-trips its path", "[serialization][roundtrip]")
{
	Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();
	auto entity = scene->CreateEntity("Env");

	auto& env = entity.AddComponent<Forge::EnvironmentMapComponent>();
	env.Path = "resources/hdri/forest.exr";

	Forge::Ref<Forge::Scene> reloaded;
	REQUIRE(RoundTrip(scene, "env.forge", reloaded));
	auto e = reloaded->FindEntityByName("Env");
	REQUIRE(e);
	REQUIRE(e.GetComponent<Forge::EnvironmentMapComponent>().Path == "resources/hdri/forest.exr");
}

TEST_CASE("Entity UUIDs survive a round-trip", "[serialization][roundtrip]")
{
	Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();
	auto alpha = scene->CreateEntity("Alpha");
	auto beta = scene->CreateEntity("Beta");

	const Forge::UUID alphaId = alpha.GetUUID();
	const Forge::UUID betaId = beta.GetUUID();

	Forge::Ref<Forge::Scene> reloaded;
	REQUIRE(RoundTrip(scene, "uuids.forge", reloaded));

	// Looked up by the original UUID: this is how saved references and prefab
	// links stay valid across a reload.
	REQUIRE(reloaded->GetEntityByUUID(alphaId));
	REQUIRE(reloaded->GetEntityByUUID(betaId));
	REQUIRE(reloaded->GetEntityByUUID(alphaId).GetUUID() == alphaId);
	REQUIRE(reloaded->GetEntityByUUID(betaId).GetUUID() != alphaId);
}

// ---------------------------------------------------------------------------
// Structural fidelity
// ---------------------------------------------------------------------------

TEST_CASE("An entity carrying every serializable component round-trips", "[serialization][roundtrip]")
{
	Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();
	auto entity = scene->CreateEntity("Everything");

	// CreateEntity already adds a TransformComponent.
	auto& tc = entity.GetComponent<Forge::TransformComponent>();
	tc.Translation = kDistinct;
	tc.Rotation = glm::vec3(5.0f, 6.0f, 7.0f);
	tc.Scale = glm::vec3(2.0f, 2.0f, 2.0f);

	auto& camera = entity.AddComponent<Forge::CameraComponent>();
	camera.Primary = true;
	camera.FieldOfView = 55.5f;

	entity.AddComponent<Forge::ScriptComponent>().ClassName = "Mover";

	auto& rb = entity.AddComponent<Forge::RigidbodyComponent>();
	rb.Type = Forge::RigidbodyComponent::BodyType::Dynamic;
	rb.FixedRotation = true;

	auto& bc = entity.AddComponent<Forge::BoxColliderComponent>();
	bc.Size = glm::vec3(1.5f, 1.5f, 1.5f);
	bc.Density = 1.5f;

	auto& sc = entity.AddComponent<Forge::SphereColliderComponent>();
	sc.Radius = 1.25f;

	auto& light = entity.AddComponent<Forge::LightComponent>();
	light.Type = Forge::LightComponent::Type::Point;
	light.Intensity = 3.25f;

	auto& pbr = entity.AddComponent<Forge::PBRMaterialComponent>();
	pbr.Albedo = kDistinct;
	pbr.Metallic = 0.375f;

	auto& mesh = entity.AddComponent<Forge::MeshComponent>();
	mesh.ModelPath = "resources/models/sphere.glb";

	entity.AddComponent<Forge::EnvironmentMapComponent>().Path = "resources/hdri/studio.hdr";

	Forge::Ref<Forge::Scene> reloaded;
	REQUIRE(RoundTrip(scene, "everything.forge", reloaded));
	auto e = reloaded->FindEntityByName("Everything");
	REQUIRE(e);

	// Every component that went in must come back.
	REQUIRE(e.HasComponent<Forge::TransformComponent>());
	REQUIRE(e.HasComponent<Forge::CameraComponent>());
	REQUIRE(e.HasComponent<Forge::ScriptComponent>());
	REQUIRE(e.HasComponent<Forge::RigidbodyComponent>());
	REQUIRE(e.HasComponent<Forge::BoxColliderComponent>());
	REQUIRE(e.HasComponent<Forge::SphereColliderComponent>());
	REQUIRE(e.HasComponent<Forge::LightComponent>());
	REQUIRE(e.HasComponent<Forge::PBRMaterialComponent>());
	REQUIRE(e.HasComponent<Forge::MeshComponent>());
	REQUIRE(e.HasComponent<Forge::EnvironmentMapComponent>());

	REQUIRE(e.GetComponent<Forge::TransformComponent>().Translation == kDistinct);
	REQUIRE(e.GetComponent<Forge::CameraComponent>().FieldOfView == Approx(55.5f));
	REQUIRE(e.GetComponent<Forge::ScriptComponent>().ClassName == "Mover");
	REQUIRE(e.GetComponent<Forge::RigidbodyComponent>().Type == Forge::RigidbodyComponent::BodyType::Dynamic);
	REQUIRE(e.GetComponent<Forge::RigidbodyComponent>().FixedRotation == true);
	REQUIRE(e.GetComponent<Forge::BoxColliderComponent>().Density == Approx(1.5f));
	REQUIRE(e.GetComponent<Forge::SphereColliderComponent>().Radius == Approx(1.25f));
	REQUIRE(e.GetComponent<Forge::LightComponent>().Type == Forge::LightComponent::Type::Point);
	REQUIRE(e.GetComponent<Forge::LightComponent>().Intensity == Approx(3.25f));
	REQUIRE(e.GetComponent<Forge::PBRMaterialComponent>().Metallic == Approx(0.375f));
	REQUIRE(e.GetComponent<Forge::MeshComponent>().ModelPath == "resources/models/sphere.glb");
	REQUIRE(e.GetComponent<Forge::EnvironmentMapComponent>().Path == "resources/hdri/studio.hdr");
}

TEST_CASE("Entity order and count survive a round-trip", "[serialization][roundtrip]")
{
	Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();
	for (int i = 0; i < 25; i++)
	{
		auto e = scene->CreateEntity("Entity" + std::to_string(i));
		e.GetComponent<Forge::TransformComponent>().Translation = glm::vec3(static_cast<float>(i), 0.0f, 0.0f);
	}

	Forge::Ref<Forge::Scene> reloaded;
	REQUIRE(RoundTrip(scene, "many.forge", reloaded));

	REQUIRE(reloaded->GetEntitiesInOrder().size() == 25);

	// Ordering must be authoring order on both sides. enTT view iteration is
	// unspecified and changes after a reload, so anything index-based (saved
	// scene layout, "first primary camera", script update order) has to come from
	// GetEntitiesInOrder rather than a raw view.
	const auto namesOf = [](const Forge::Ref<Forge::Scene>& target)
	{
		std::vector<std::string> names;
		for (auto e : target->GetEntitiesInOrder())
			names.push_back(e.GetComponent<Forge::TagComponent>().Tag);
		return names;
	};

	const auto originalNames = namesOf(scene);
	const auto reloadedNames = namesOf(reloaded);

	REQUIRE(reloadedNames == originalNames);
	REQUIRE(originalNames.front() == "Entity0");
	REQUIRE(originalNames.back() == "Entity24");
}

TEST_CASE("An empty scene round-trips", "[serialization][roundtrip]")
{
	Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();
	Forge::Ref<Forge::Scene> reloaded;
	REQUIRE(RoundTrip(scene, "empty.forge", reloaded));

	REQUIRE(reloaded->GetEntitiesInOrder().empty());
}

TEST_CASE("Empty component fields survive as empty, not as defaults", "[serialization][roundtrip]")
{
	Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();

	// A script with an empty class name and a mesh with no path are both valid
	// authored states; loading must not substitute a default.
	auto scripted = scene->CreateEntity("EmptyScript");
	scripted.AddComponent<Forge::ScriptComponent>().ClassName = "";

	auto meshed = scene->CreateEntity("EmptyMesh");
	meshed.AddComponent<Forge::MeshComponent>().ModelPath = "";

	Forge::Ref<Forge::Scene> reloaded;
	REQUIRE(RoundTrip(scene, "empty_fields.forge", reloaded));

	REQUIRE(reloaded->FindEntityByName("EmptyScript")
		.GetComponent<Forge::ScriptComponent>().ClassName.empty());
	REQUIRE(reloaded->FindEntityByName("EmptyMesh")
		.GetComponent<Forge::MeshComponent>().ModelPath.empty());
}

TEST_CASE("Negative and fractional values round-trip exactly", "[serialization][roundtrip]")
{
	Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();
	auto entity = scene->CreateEntity("Signs");

	// CreateEntity already adds a TransformComponent.
	auto& tc = entity.GetComponent<Forge::TransformComponent>();
	tc.Translation = glm::vec3(-0.000125f, -12345.6789f, 0.1f);
	tc.Scale = glm::vec3(-1.0f, 0.333f, 1e-4f);

	auto& pbr = entity.AddComponent<Forge::PBRMaterialComponent>();
	pbr.Metallic = -0.5f;
	pbr.Roughness = 1.5f; // deliberately outside the usual [0,1] range

	Forge::Ref<Forge::Scene> reloaded;
	REQUIRE(RoundTrip(scene, "signs.forge", reloaded));
	auto e = reloaded->FindEntityByName("Signs");
	REQUIRE(e);

	const auto& rtc = e.GetComponent<Forge::TransformComponent>();
	REQUIRE(rtc.Translation == glm::vec3(-0.000125f, -12345.6789f, 0.1f));
	REQUIRE(rtc.Scale == glm::vec3(-1.0f, 0.333f, 1e-4f));

	const auto& rpbr = e.GetComponent<Forge::PBRMaterialComponent>();
	// Out-of-range values must not be silently clamped on load.
	REQUIRE(rpbr.Metallic == Approx(-0.5f));
	REQUIRE(rpbr.Roughness == Approx(1.5f));
}

TEST_CASE("Saving twice produces an identical file", "[serialization][roundtrip]")
{
	Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();
	auto entity = scene->CreateEntity("Stable");
	entity.GetComponent<Forge::TransformComponent>().Translation = kDistinct;
	entity.AddComponent<Forge::PBRMaterialComponent>().Albedo = kDistinct;
	entity.AddComponent<Forge::ScriptComponent>().ClassName = "Mover";

	std::filesystem::create_directories("test_roundtrip");

	const std::string first = TempPath("stable_first.forge");
	const std::string second = TempPath("stable_second.forge");

	{
		Forge::SceneSerializer serializer(scene);
		serializer.Serialize(first);
		serializer.Serialize(second);
	}

	// Re-saving the same scene must be byte-identical, otherwise diffs in version
	// control become noise and "did anything change?" becomes unanswerable.
	const auto readAll = [](const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	};

	REQUIRE(readAll(first) == readAll(second));
	REQUIRE_FALSE(readAll(first).empty());
}