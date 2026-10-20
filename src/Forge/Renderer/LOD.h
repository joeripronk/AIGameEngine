#pragma once

#include <cstddef>
#include <vector>

namespace Forge {

	// The level of detail a mesh is drawn at (the Godot LODMesh model).
	//
	// A mesh that is far away is drawn with fewer triangles: the difference
	// between the levels cannot be seen at that distance, and the triangles
	// that are not drawn are not paid for.
	namespace LOD {

		// The level to draw.
		//
		// The levels are ordered from the most detailed to the least, and
		// the distances say where each one takes over: the first distance is
		// the point at which the second level is used, the second is the
		// point at which the third is, and so on.
		//
		// A mesh that is closer than the first distance is drawn in full
		// detail. A mesh with no distances has one level, and it is the one
		// that is drawn.
		[[nodiscard]] size_t ChooseLevel(const std::vector<float>& switchDistances, float distance);

	}  // namespace LOD

}  // namespace Forge
