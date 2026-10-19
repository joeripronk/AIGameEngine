#include "fgpch.h"
#include "Platform/Vulkan/Memory/UploadRegions.h"

#include <algorithm>

namespace Forge { namespace Vulkan {

uint32_t Lcm4AndTexelBlock(uint32_t texelBlockBytes)
{
	if (texelBlockBytes == 0)
		return 0;

	// Euclid by subtraction, the way BRUTAL does it; the values are tiny.
	uint32_t a = 4;
	uint32_t b = texelBlockBytes;
	while (a != b)
	{
		if (a > b)
			a -= b;
		else
			b -= a;
	}
	return 4u * texelBlockBytes / a;
}

uint64_t ComputeRowPitch(uint32_t rowWidthInBlocks, uint32_t texelBlockBytes)
{
	const uint32_t lcm = Lcm4AndTexelBlock(texelBlockBytes);
	if (lcm == 0)
		return 0;

	const uint64_t rowBytes = static_cast<uint64_t>(rowWidthInBlocks) * texelBlockBytes;
	return (rowBytes + lcm - 1) / lcm * lcm;
}

UploadPlan BuildUploadPlan(const UploadTextureDesc& desc)
{
	UploadPlan plan{};

	const uint32_t lcm = Lcm4AndTexelBlock(desc.TexelBlockBytes);
	if (lcm == 0 || desc.Levels.empty() || desc.ArrayLayers == 0 || desc.FaceCount == 0)
		return plan;

	// Indices, not pointers: the vectors grow while the plan is built, and
	// a pointer into a vector that later reallocates is a use-after-free
	// dressed up as bookkeeping.
	int64_t currentStage = -1;
	int64_t currentUpload = -1;

	// The source is assumed contiguous in iteration order (level, layer,
	// face, slice) with each row padded to the row pitch - the KTX-style
	// layout whose padding this plan removes.
	uint64_t sourceOffset = 0;
	uint64_t destinationOffset = 0;

	for (uint32_t level = 0; level < desc.Levels.size(); ++level)
	{
		const UploadImageLayout& layout = desc.Levels[level];
		if (layout.WidthInBlocks == 0 || layout.HeightInBlocks == 0)
			return UploadPlan{};

		const uint64_t rowPitch = ComputeRowPitch(layout.WidthInBlocks, desc.TexelBlockBytes);
		const uint32_t depthSlices = desc.Is3D ? std::max(layout.Depth, 1u) : 1u;
		const uint64_t sliceBytes = rowPitch * layout.HeightInBlocks;

		for (uint32_t layer = 0; layer < desc.ArrayLayers; ++layer)
		{
			for (uint32_t face = 0; face < desc.FaceCount; ++face)
			{
				const uint32_t baseLayer = layer * desc.FaceCount + face;

				const bool startNewRegion =
					currentUpload < 0 ||
					desc.CopyMode == UploadTextureDesc::Mode::UploadRegionPerImage ||
					level != plan.CopyRegions[currentUpload].BaseLevel;

				if (startNewRegion)
				{
					destinationOffset = (destinationOffset + lcm - 1) / lcm * lcm;

					plan.CopyRegions.push_back(UploadRegion{ level, baseLayer, 0, destinationOffset });
					currentUpload = static_cast<int64_t>(plan.CopyRegions.size()) - 1;
				}

				for (uint32_t slice = 0; slice < depthSlices; ++slice)
				{
					const bool continues = currentStage >= 0 &&
						sourceOffset == plan.StageRegions[currentStage].SourceOffset + plan.StageRegions[currentStage].SizeInBytes &&
						destinationOffset == plan.StageRegions[currentStage].DestinationOffset + plan.StageRegions[currentStage].SizeInBytes;

					if (!continues)
					{
						plan.StageRegions.push_back(StagingRegion{
							sourceOffset,
							destinationOffset,
							0,
							layout.WidthInBlocks,
							desc.TexelBlockBytes,
							rowPitch,
						});
						currentStage = static_cast<int64_t>(plan.StageRegions.size()) - 1;
					}

					plan.StageRegions[currentStage].SizeInBytes += sliceBytes;
					destinationOffset += sliceBytes;
					sourceOffset += sliceBytes;
				}

				++plan.CopyRegions[currentUpload].LayerCount;
			}
		}
	}

	if (!plan.StageRegions.empty())
	{
		const StagingRegion& last = plan.StageRegions.back();
		plan.RequiredBufferSizeBytes = last.DestinationOffset + last.SizeInBytes;
	}

	return plan;
}

}}
