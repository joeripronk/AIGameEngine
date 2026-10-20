#include "fgpch.h"
#include "Forge/Renderer/LOD.h"

namespace Forge {

	namespace LOD {

		size_t ChooseLevel(const std::vector<float>& switchDistances, float distance)
		{
			// The level is the last one whose switch distance the object is
			// within: the first distance says where the second level takes
			// over, the second where the third does.
			size_t level = 0;
			for (size_t i = 0; i < switchDistances.size(); ++i)
			{
				if (distance < switchDistances[i])
					break;
				level = i + 1;
			}

			return level;
		}

	}  // namespace LOD

}  // namespace Forge
