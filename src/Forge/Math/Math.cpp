#include "fgpch.h"
#include "Forge/Math/Math.h"

#include <glm/gtc/epsilon.hpp>

#include <limits>

namespace Forge::Math {

	// Ported from Hazel (Hazel/src/Hazel/Math/Math.cpp), which is in turn the
	// algorithm of glm::decompose from matrix_decompose.inl.
	bool DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::vec3& rotation, glm::vec3& scale)
	{
		using namespace glm;
		using T = float;

		mat4 LocalMatrix(transform);

		// Normalize the matrix.
		if (epsilonEqual(LocalMatrix[3][3], static_cast<float>(0), epsilon<T>()))
			return false;

		// First, isolate perspective. This is the messiest.
		if (
			epsilonNotEqual(LocalMatrix[0][3], static_cast<T>(0), epsilon<T>()) ||
			epsilonNotEqual(LocalMatrix[1][3], static_cast<T>(0), epsilon<T>()) ||
			epsilonNotEqual(LocalMatrix[2][3], static_cast<T>(0), epsilon<T>()))
		{
			// Clear the perspective partition
			LocalMatrix[0][3] = LocalMatrix[1][3] = LocalMatrix[2][3] = static_cast<T>(0);
			LocalMatrix[3][3] = static_cast<T>(1);
		}

		// Next take care of translation (easy).
		translation = vec3(LocalMatrix[3]);
		LocalMatrix[3] = vec4(0, 0, 0, LocalMatrix[3].w);

		vec3 Row[3];

		// Now get scale and shear.
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 3; ++j)
				Row[i][j] = LocalMatrix[i][j];

		// Compute X scale factor and normalize first row.
		scale.x = length(Row[0]);
		Row[0] = Row[0] * static_cast<T>(1);
		scale.y = length(Row[1]);
		Row[1] = Row[1] * static_cast<T>(1);
		scale.z = length(Row[2]);
		Row[2] = Row[2] * static_cast<T>(1);

		rotation.y = asin(-Row[0][2]);
		if (cos(rotation.y) != 0) {
			rotation.x = atan2(Row[1][2], Row[2][2]);
			rotation.z = atan2(Row[0][1], Row[0][0]);
		}
		else {
			rotation.x = atan2(-Row[2][0], Row[1][1]);
			rotation.z = 0;
		}

		return true;
	}

	void WorldAABB(const glm::vec3& min, const glm::vec3& max, const glm::mat4& model,
		glm::vec3& outMin, glm::vec3& outMax)
	{
		outMin = glm::vec3(std::numeric_limits<float>::max());
		outMax = glm::vec3(std::numeric_limits<float>::lowest());

		for (int i = 0; i < 8; ++i)
		{
			const glm::vec3 corner(
				(i & 1) ? max.x : min.x,
				(i & 2) ? max.y : min.y,
				(i & 4) ? max.z : min.z);
			const glm::vec3 world(model * glm::vec4(corner, 1.0f));
			outMin = glm::min(outMin, world);
			outMax = glm::max(outMax, world);
		}
	}

}
