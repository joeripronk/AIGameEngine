#pragma once

#include <glm/glm.hpp>

namespace Forge::Math {

	// Splits a composed T*R*S matrix back into its parts, the way Hazel's
	// EditorLayer needs it for the inspector's transform fields: the renderer
	// only ever sees the matrix, and editing "the rotation of a matrix" is
	// only meaningful once the matrix is known to be a translation, a rotation
	// and a scale.
	//
	// Returns false only for a degenerate matrix (zero w). The angles are
	// Euler angles in radians, and glm's decomposition is only well-conditioned
	// for scales close to 1 - the same caveat Hazel carries.
	bool DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::vec3& rotation, glm::vec3& scale);

	// The world-space AABB of the box [min, max] drawn under `model`.
	//
	// A rotated box is not axis-aligned, so the answer is the AABB of the
	// eight transformed corners - a box culled against that is a box that is
	// *provably* off-screen for every pixel, and a box picked against that is
	// the box that was drawn.
	//
	// One rule, two users: the renderer culls with it and the scene picks
	// with it, so the thing the author sees and the thing the author clicks
	// are the same box.
	void WorldAABB(const glm::vec3& min, const glm::vec3& max, const glm::mat4& model,
		glm::vec3& outMin, glm::vec3& outMax);

}
