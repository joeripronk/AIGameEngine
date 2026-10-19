#include "fgpch.h"
#include "Platform/Vulkan/Memory/AllocationInfo.h"

#include "Forge/Core/Assert.h"

namespace Forge { namespace Vulkan {

bool ValidateMapSubset(MapRegion parent, MapRegion requested, MapRegion& outResolved)
{
	outResolved = requested;

	if (requested.Offset > parent.Size)
		return false;

	const size_t remaining = parent.Size - requested.Offset;

	if (requested.Size == WholeSize)
	{
		outResolved.Size = remaining;
	}
	else if (requested.Size > remaining)
	{
		return false;
	}

	outResolved.Offset = parent.Offset + requested.Offset;
	return true;
}

}}
