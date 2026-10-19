#include "fgpch.h"
#include "Forge/Renderer/Texture.h"

// The Texture factories live in the Vulkan platform layer (VkTexture.cpp):
// CpuTexture decodes the file and VkRenderer uploads the decoded pixels on
// first draw. ForgeCore is headless, so it defines no factory - a Texture
// without the Vulkan layer is not constructible here. Forge::CpuTexture is
// the device-independent decode for tests and asset pipelines.
