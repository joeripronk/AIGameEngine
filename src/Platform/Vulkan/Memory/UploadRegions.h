#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace Forge { namespace Vulkan {

// Pure CPU copy-plan builder for texture uploads (BRUTAL
// Brutal.Texture.Abstractions/Upload/UploadRegions.cs): walks the mip
// pyramid level by level, layer by layer, face by face, and produces the
// staging-buffer regions (row padding removed, data aligned to
// lcm(4, texel block)) plus the batched buffer-to-image copy regions.
//
// This is arithmetic on layout numbers only - no Vulkan, no device - so the
// mip pyramid math is settled by unit tests, not by what appears on screen.

struct UploadImageLayout
{
	uint32_t WidthInBlocks = 1;
	uint32_t HeightInBlocks = 1;
	uint32_t Depth = 1;   // slices for 3D, 1 otherwise
};

struct UploadTextureDesc
{
	enum class Mode : uint8_t
	{
		BatchUploadRegions,    // as many images per copy region as the layout allows
		UploadRegionPerImage,  // one copy region per image
	};

	uint32_t TexelBlockBytes = 4;
	uint32_t ArrayLayers = 1;
	uint32_t FaceCount = 1;    // 6 for cube maps
	bool Is3D = false;
	Mode CopyMode = Mode::BatchUploadRegions;

	// Level 0 first; the pyramid is what it is - the builder does not
	// assume halving, so a non-power-of-two chain plans correctly.
	std::span<const UploadImageLayout> Levels;
};

// One contiguous run of source rows copied into the staging buffer.
struct StagingRegion
{
	uint64_t SourceOffset = 0;
	uint64_t DestinationOffset = 0;
	uint64_t SizeInBytes = 0;
	uint32_t RowWidthInBlocks = 0;
	uint32_t BlockSizeBytes = 0;
	uint64_t RowPitch = 0;   // padded source pitch; the destination is tight
};

// One buffer-to-image copy region: starts at BaseLevel/BaseLayer and covers
// LayerCount layers (faces count as layers in the image).
struct UploadRegion
{
	uint32_t BaseLevel = 0;
	uint32_t BaseLayer = 0;
	uint32_t LayerCount = 0;
	uint64_t BufferOffset = 0;
};

struct UploadPlan
{
	std::vector<StagingRegion> StageRegions;
	std::vector<UploadRegion> CopyRegions;
	uint64_t RequiredBufferSizeBytes = 0;
};

// lcm(4, texelBlockBytes); 0 for a zero block size (no plan).
[[nodiscard]] uint32_t Lcm4AndTexelBlock(uint32_t texelBlockBytes);

// Source row pitch: the block bytes of one row, padded up to a multiple of
// lcm(4, block) - the KTX-style padding the plan removes.
[[nodiscard]] uint64_t ComputeRowPitch(uint32_t rowWidthInBlocks, uint32_t texelBlockBytes);

// The plan, or an empty plan (RequiredBufferSizeBytes 0) for a degenerate
// description - a zero block size, no levels, a zero extent.
[[nodiscard]] UploadPlan BuildUploadPlan(const UploadTextureDesc& desc);

}}
