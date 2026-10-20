#include "fgpch.h"
#include "Forge/Renderer/Mesh.h"

#include "Forge/Renderer/CpuMesh.h"

// The file-backed Mesh::Create factory lives in the Vulkan platform layer
// (VkMesh.cpp) because it uploads to the GPU. The procedural primitive
// factories (cube/plane/sphere) are available on every platform via
// Forge::Primitives and Mesh.h.

namespace Forge {

	InstanceData InstanceData::FromMatrix(const glm::mat4& matrix)
	{
		InstanceData data;
		data.Row0 = glm::vec4(matrix[0]);
		data.Row1 = glm::vec4(matrix[1]);
		data.Row2 = glm::vec4(matrix[2]);
		data.Row3 = glm::vec4(matrix[3]);
		return data;
	}

} // namespace Forge
