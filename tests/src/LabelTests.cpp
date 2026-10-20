#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Forge/Scene/Components.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/SceneSerializer.h"
#include "Forge/UI/Label.h"

using namespace Forge;

TEST_CASE("A text is built into a mesh", "[ui][label]")
{
	// The text is quads: a label is a mesh, so it is drawn by the same
	// path as any other geometry.
	Ref<CpuMesh> mesh = BuildTextMesh("Hi", 0.01f);
	REQUIRE(mesh);
	REQUIRE(mesh->GetVertices().size() > 0);
	REQUIRE(mesh->GetIndexCount() > 0);

	// The quads face the camera: the normal is +Z, so a label that turns
	// towards the camera is lit from the front.
	for (const Vertex& vertex : mesh->GetVertices())
		REQUIRE(vertex.Normal == glm::vec3(0.0f, 0.0f, 1.0f));

	// The text grows with the size, and the width with it.
	const Ref<CpuMesh> small = BuildTextMesh("Hello", 0.01f);
	const Ref<CpuMesh> large = BuildTextMesh("Hello", 0.02f);
	REQUIRE(small);
	REQUIRE(large);
	REQUIRE(small->GetVertices().size() == large->GetVertices().size());

	REQUIRE(MeasureTextWidth("Hello", 0.01f) > 0.0f);
	REQUIRE(MeasureTextWidth("Hello", 0.02f) ==
		Catch::Approx(MeasureTextWidth("Hello", 0.01f) * 2.0f));
	REQUIRE(MeasureTextHeight("Hello", 0.01f) > 0.0f);

	// An empty text is nothing to draw.
	REQUIRE(BuildTextMesh("", 0.01f) == nullptr);
	REQUIRE(MeasureTextWidth("", 0.01f) == 0.0f);
}

TEST_CASE("A label follows its text", "[ui][label]")
{
	Scene scene;
	Entity entity = scene.CreateEntity("Score");
	entity.AddComponent<MeshComponent>();

	auto& label = entity.AddComponent<LabelComponent>();
	label.Text = "Score: 10";
	label.Size = 0.02f;

	// The label builds its mesh into the entity's mesh, so the renderer
	// draws it like any other geometry.
	scene.UpdateLabels();

	REQUIRE(entity.GetComponent<MeshComponent>().CpuMeshData);
	REQUIRE(label.BuiltText == "Score: 10");
	REQUIRE(label.MeshData);

	// The text changes: the mesh is built again.
	const size_t before = entity.GetComponent<MeshComponent>().CpuMeshData->GetVertices().size();
	label.Text = "Score: 1000";
	scene.UpdateLabels();

	REQUIRE(label.BuiltText == "Score: 1000");
	REQUIRE(entity.GetComponent<MeshComponent>().CpuMeshData->GetVertices().size() != before);
}

TEST_CASE("A label survives a round trip through a scene file", "[scene][label]")
{
	const std::string path = "test_label.forge";

	{
		Ref<Scene> scene = CreateRef<Scene>();
		Entity entity = scene->CreateEntity("Score");
		entity.AddComponent<MeshComponent>();

		auto& label = entity.AddComponent<LabelComponent>();
		label.Text = "Hello";
		label.Size = 0.05f;
		label.Billboard = false;

		SceneSerializer serializer(scene);
		REQUIRE(serializer.Serialize(path));
	}

	{
		Ref<Scene> scene = CreateRef<Scene>();
		SceneSerializer serializer(scene);
		REQUIRE(serializer.Deserialize(path));

		Entity entity = scene->FindEntityByName("Score");
		REQUIRE(entity);
		REQUIRE(entity.HasComponent<LabelComponent>());

		const LabelComponent& label = entity.GetComponent<LabelComponent>();
		REQUIRE(label.Text == "Hello");
		REQUIRE(label.Size == Catch::Approx(0.05f));
		REQUIRE_FALSE(label.Billboard);
	}

	std::remove(path.c_str());
}
