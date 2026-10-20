// Authoring the scene the Tetris game ships.
//
// The scene is the game's *content*: its camera, its light and its board.
// Content belongs in a file the editor can open, so the game ships one and
// the editor edits it.
//
// The scene is built with the editor's own model - the same EditorContext
// the editor's UI and an AI agent drive - so what this writes is exactly
// what the editor would write, and the game loads what the editor saves.
//
//     ./TetrisSceneAuthor [path-to-scene]

#include "fgpch.h"

#include "Forge/Core/Log.h"
#include "Forge/Editor/EditorContext.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/Entity.h"
#include "TetrisBoardView.h"
#include "TetrisGameScript.h"
#include "TetrisLightShow.h"

#include <cstdio>
#include <string>

using namespace Forge;

namespace {

	constexpr const char* kDefaultScene = "resources/scenes/Tetris.forge";
	constexpr const char* kBlockMesh = "resources/models/cube.glb";

	// A slab of the board: the backdrop behind the playfield and the four
	// borders around it.
	Entity AddSlab(EditorContext& editor, const char* name, const glm::vec3& offset,
		const glm::vec3& scale, const glm::vec3& albedo, float roughness)
	{
		Entity entity = editor.CreateEntity(name);
		entity.GetComponent<TransformComponent>().Translation = Tetris::BoardView::BoardCentre() + offset;
		entity.GetComponent<TransformComponent>().Scale = scale;
		entity.AddComponent<MeshComponent>().ModelPath = kBlockMesh;

		auto& material = entity.AddComponent<PBRMaterialComponent>();
		material.Albedo = albedo;
		material.Roughness = roughness;
		return entity;
	}

}  // namespace

int main(int argc, char** argv)
{
	Log::Init();

	const std::string path = argc > 1 ? argv[1] : kDefaultScene;

	EditorContext editor;
	editor.NewScene();

	// --- The camera ---
	// Framed on the playfield. The game places it for the mode it is in and
	// for the shape of the window, so the position here is where the editor
	// looks from: the editor's camera follows the scene's, and a scene whose
	// camera is at the origin is a scene the editor shows the inside of.
	{
		Entity camera = editor.CreateEntity("MainCamera");
		camera.GetComponent<TransformComponent>().Translation =
			Tetris::BoardView::BoardCentre() + glm::vec3(0.0f, 0.0f, 21.65f);

		auto& component = camera.AddComponent<CameraComponent>();
		component.Primary = true;
		component.FieldOfView = 60.0f;
		component.AspectRatio = 16.0f / 9.0f;
	}

	// --- The light ---
	// The key comes from the front and only grazes the sides, so the front
	// of a block is the brightest face and the sides fall into shade.
	{
		Entity light = editor.CreateEntity("DirectionalLight");
		auto& component = light.AddComponent<LightComponent>();
		component.Type = LightComponent::Type::Directional;
		component.Direction = glm::normalize(glm::vec3(0.20f, 0.20f, 0.95f));
		component.Color = glm::vec3(1.0f, 0.95f, 0.9f);
		component.Intensity = 1.25f;
	}

	// --- The game's script ---
	//
	// The scene names the game's C++ the way it names a Lua script: by the
	// name the game registered it under. The editor is a different binary
	// and does not have the game, so the entity is inert there - the same
	// rule every native script follows.
	{
		Entity game = editor.CreateEntity("GameController");
		game.AddComponent<NativeScriptComponent>().ClassName = Tetris::TetrisGameScript::kClassName;
	}

	// --- The board ---
	// The backdrop behind the playfield and the four borders around it. The
	// board's size comes from the game itself, so the scene and the game
	// cannot disagree about how big the playfield is.
	{
		const glm::vec3 boardSize = Tetris::BoardView::BoardSize();
		const float halfW = boardSize.x * 0.5f;
		const float halfH = boardSize.y * 0.5f;
		const float border = 0.20f;

		AddSlab(editor, "BoardBackdrop", glm::vec3(0.0f, 0.0f, -4.2f),
			glm::vec3(boardSize.x + 1.4f, boardSize.y + 1.4f, 0.4f), { 0.018f, 0.024f, 0.042f }, 0.95f);

		const glm::vec3 borderColour = { 0.30f, 0.32f, 0.38f };
		AddSlab(editor, "BoardBorderLeft", glm::vec3(-halfW - border, 0.0f, -0.2f),
			glm::vec3(border, boardSize.y + border * 2.0f, 0.3f), borderColour, 0.6f);
		AddSlab(editor, "BoardBorderRight", glm::vec3(halfW + border, 0.0f, -0.2f),
			glm::vec3(border, boardSize.y + border * 2.0f, 0.3f), borderColour, 0.6f);
		AddSlab(editor, "BoardBorderTop", glm::vec3(0.0f, halfH + border, -0.2f),
			glm::vec3(boardSize.x + border * 2.0f, border, 0.3f), borderColour, 0.6f);
		AddSlab(editor, "BoardBorderFloor", glm::vec3(0.0f, -halfH - border, -0.2f),
			glm::vec3(boardSize.x + border * 2.0f, border, 0.3f), borderColour, 0.6f);
	}

	// --- The light show's lamps ---
	//
	// The lamps are the scene's: where they hang is content, like the board
	// and the camera, and the editor shows them like anything else. The show
	// itself - the colours, the movement - is the game's, and it drives these
	// lamps when the game runs.
	{
		for (uint32_t index = 0; index < Tetris::LightShow::kLightCount; ++index)
		{
			Entity lamp = editor.CreateEntity(Tetris::LightShow::LightName(index));
			lamp.GetComponent<TransformComponent>().Translation = Tetris::LightShow::LampPosition(index);

			auto& component = lamp.AddComponent<LightComponent>();
			component.Type = LightComponent::Type::Point;
			component.Color = glm::vec3(1.0f);
			component.Intensity = 1.0f;
		}
	}

	if (!editor.SaveScene(path))
	{
		FG_ERROR("Could not write the scene to '{0}'", path);
		return 1;
	}

	FG_INFO("Wrote the scene to '{0}'", path);
	printf("Wrote %s\n", path.c_str());
	return 0;
}
