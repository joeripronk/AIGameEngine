#pragma once

#include <glm/glm.hpp>

#include <vector>

namespace Forge {

	// The character controller (the Godot CharacterBody3D model, on the
	// engine's box geometry).
	//
	// A character is not a dynamic body: it is moved by the game, and the
	// world is moved out of its way. That is what makes it different from
	// the engine's rigid bodies, and it is what lets a character walk up a
	// slope, climb a step and slide along a wall without any of that
	// turning into momentum.
	//
	// The movement itself is pure: where the character is, where it wants
	// to be, and the boxes in between. The engine's rigid bodies are boxes,
	// so the character is a box too - which means it has corners rather
	// than a capsule's round ones, and slides along a wall a little less
	// smoothly than a capsule would.

	// The character's shape: a box of the given radius and height, standing
	// on the point it is at.
	struct CharacterShape
	{
		float Radius = 0.4f;
		float Height = 1.8f;
	};

	// The rules the character walks by.
	struct CharacterSettings
	{
		// The height it climbs without jumping. A kerb is a step; a wall is
		// a wall.
		float StepHeight = 0.4f;

		// How far it may sink into the floor before it is pushed out. A
		// character that is standing exactly on a floor overlaps it by
		// nothing, and one that is a hair too low overlaps it by that hair.
		float FloorTolerance = 0.05f;
	};

	// One box the character walks into.
	struct CharacterObstacle
	{
		glm::vec3 Min = glm::vec3(0.0f);
		glm::vec3 Max = glm::vec3(0.0f);
	};

	// What a move did.
	struct CharacterMoveResult
	{
		// Where the character ended up.
		glm::vec3 Position = glm::vec3(0.0f);

		// Whether it is standing on something, whether its head hit
		// something, and whether it was stopped by a wall.
		bool OnFloor = false;
		bool OnCeiling = false;
		bool OnWall = false;

		// The wall it hit, and the direction out of it.
		glm::vec3 WallNormal = glm::vec3(0.0f);
	};

	namespace Character {

		// Moves the character by the given displacement and resolves it
		// against the obstacles.
		//
		// The horizontal axes are moved one at a time, so a character that
		// is stopped on one of them still moves on the other: that is what
		// sliding along a wall is. The vertical move is last, so the
		// character lands on what is under it.
		[[nodiscard]] CharacterMoveResult Move(const glm::vec3& position, const glm::vec3& displacement,
			const CharacterShape& shape, const CharacterSettings& settings,
			const std::vector<CharacterObstacle>& obstacles);

		// Whether the character is standing on something: a check under its
		// feet, within the floor's tolerance.
		[[nodiscard]] bool IsOnFloor(const glm::vec3& position, const CharacterShape& shape,
			const CharacterSettings& settings, const std::vector<CharacterObstacle>& obstacles);

		// The boxes of the world's static geometry, as the character sees
		// them.
		[[nodiscard]] std::vector<CharacterObstacle> CollectObstacles(const std::vector<glm::vec3>& boxMinima,
			const std::vector<glm::vec3>& boxMaxima);

	}  // namespace Character

}  // namespace Forge
