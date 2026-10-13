#include "fgpch.h"
#include "Forge/Renderer/Mesh.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace Forge {

	Ref<Mesh> Mesh::Create(const std::string& filepath)
	{
		// TODO: Implement mesh loading from glTF/GLB
		// Use assimp to load the model
		// Parse scene and create vertex/index buffers
		return nullptr;
	}

}
