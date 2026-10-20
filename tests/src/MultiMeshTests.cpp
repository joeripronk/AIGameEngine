#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Forge/Renderer/Mesh.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/SceneSerializer.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cstdio>

using namespace Forge;

TEST_CASE("An instance's data is the model matrix, as four rows", "[renderer][instance]")
{
	const glm::mat4 place = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));

	const InstanceData data = InstanceData::FromMatrix(place);

	// The rows are the matrix's columns: the shader builds the matrix back
	// from them, and a row that arrives in the wrong place is a mesh in the
	// wrong place.
	REQUIRE(data.Row0 == glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
	REQUIRE(data.Row1 == glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
	REQUIRE(data.Row2 == glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
	REQUIRE(data.Row3 == glm::vec4(1.0f, 2.0f, 3.0f, 1.0f));
}

TEST_CASE("A mesh in many places survives a round trip through a scene file", "[scene][instance]")
{
	const std::string path = "test_multimesh.forge";

	{
		Ref<Scene> scene = CreateRef<Scene>();
		Entity entity = scene->CreateEntity("Row");
		entity.GetComponent<TransformComponent>().Translation = { 0.0f, 0.5f, 0.0f };

		auto& multi = entity.AddComponent<MultiMeshComponent>();
		multi.ModelPath = "resources/models/cube.glb";
		multi.Transforms.push_back(glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
		multi.Transforms.push_back(glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f)));

		SceneSerializer serializer(scene);
		REQUIRE(serializer.Serialize(path));
	}

	{
		Ref<Scene> scene = CreateRef<Scene>();
		SceneSerializer serializer(scene);
		REQUIRE(serializer.Deserialize(path));

		Entity entity = scene->FindEntityByName("Row");
		REQUIRE(entity);
		REQUIRE(entity.HasComponent<MultiMeshComponent>());

		const MultiMeshComponent& multi = entity.GetComponent<MultiMeshComponent>();
		REQUIRE(multi.ModelPath == "resources/models/cube.glb");
		REQUIRE(multi.Transforms.size() == 2);
		REQUIRE(multi.Transforms[0][3][0] == Catch::Approx(1.0f));
		REQUIRE(multi.Transforms[1][3][0] == Catch::Approx(3.0f));
	}

	std::remove(path.c_str());
}

TEST_CASE("A mesh in many places survives the runtime format too", "[scene][instance]")
{
	const std::string path = "test_multimesh.rt";

	{
		Ref<Scene> scene = CreateRef<Scene>();
		Entity entity = scene->CreateEntity("Row");

		auto& multi = entity.AddComponent<MultiMeshComponent>();
		multi.ModelPath = "resources/models/cube.glb";
		multi.Transforms.push_back(glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
		multi.Transforms.push_back(glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f)));

		SceneSerializer serializer(scene);
		serializer.SerializeRuntime(path);
	}

	{
		Ref<Scene> scene = CreateRef<Scene>();
		SceneSerializer serializer(scene);
		REQUIRE(serializer.DeserializeRuntime(path));

		Entity entity = scene->FindEntityByName("Row");
		REQUIRE(entity);
		REQUIRE(entity.HasComponent<MultiMeshComponent>());

		const MultiMeshComponent& multi = entity.GetComponent<MultiMeshComponent>();
		REQUIRE(multi.ModelPath == "resources/models/cube.glb");
		REQUIRE(multi.Transforms.size() == 2);
		REQUIRE(multi.Transforms[0][3][0] == Catch::Approx(1.0f));
		REQUIRE(multi.Transforms[1][3][0] == Catch::Approx(3.0f));
	}

	std::remove(path.c_str());
}

TEST_CASE("The instanced scene loads with its instances", "[scene][instance]")
{
	Ref<Scene> scene = CreateRef<Scene>();
	SceneSerializer serializer(scene);
	REQUIRE(serializer.Deserialize("resources/scenes/Instanced.forge"));

	Entity entity = scene->FindEntityByName("CubeRow");
	REQUIRE(entity);
	REQUIRE(entity.HasComponent<MultiMeshComponent>());

	const MultiMeshComponent& multi = entity.GetComponent<MultiMeshComponent>();
	REQUIRE(multi.ModelPath == "resources/models/cube.glb");
	REQUIRE(multi.Transforms.size() == 12);

	// The instances are where the scene says they are.
	REQUIRE(multi.Transforms[0][3][0] == Catch::Approx(-6.6f));
	REQUIRE(multi.Transforms[11][3][0] == Catch::Approx(6.6f));
}
