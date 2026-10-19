#include "fgpch.h"
#include "Forge/Renderer/Mesh.h"

#include "Forge/Renderer/CpuMesh.h"

// The file-backed Mesh::Create factory lives in the Vulkan platform layer
// (VkMesh.cpp) because it uploads to the GPU. The procedural primitive
// factories (cube/plane/sphere) are available on every platform via
// Forge::Primitives and Mesh.h.
