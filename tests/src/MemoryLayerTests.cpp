#include <catch2/catch_test_macros.hpp>

#include "Platform/Vulkan/Memory/AllocationInfo.h"
#include "Platform/Vulkan/Memory/StagingPool.h"
#include "Platform/Vulkan/Memory/UploadRegions.h"

#include <array>

using namespace Forge::Vulkan;

TEST_CASE("Upload regions: lcm(4, texel block) and row pitch with KTX-style padding", "[memory]")
{
	REQUIRE(Lcm4AndTexelBlock(4) == 4);
	REQUIRE(Lcm4AndTexelBlock(8) == 8);
	REQUIRE(Lcm4AndTexelBlock(1) == 4);
	REQUIRE(Lcm4AndTexelBlock(3) == 12);
	REQUIRE(Lcm4AndTexelBlock(6) == 12);
	REQUIRE(Lcm4AndTexelBlock(0) == 0);

	// 5 blocks of 3 bytes = 15, padded to the next multiple of 12.
	REQUIRE(ComputeRowPitch(5, 3) == 24);
	// 4 blocks of 4 = 16, already aligned.
	REQUIRE(ComputeRowPitch(4, 4) == 16);
	// 3 blocks of 4 = 12, aligned to 4 is 12.
	REQUIRE(ComputeRowPitch(3, 4) == 12);
	// One 1-byte block still rounds to the 4-byte minimum.
	REQUIRE(ComputeRowPitch(1, 1) == 4);
}

TEST_CASE("Upload regions: single-level plan is one tight staging region", "[memory]")
{
	const std::array<UploadImageLayout, 1> levels{ UploadImageLayout{ 8, 8, 1 } };
	const UploadTextureDesc desc{ 4, 1, 1, false, UploadTextureDesc::Mode::BatchUploadRegions, std::span<const UploadImageLayout>(levels.data(), levels.size()) };

	const UploadPlan plan = BuildUploadPlan(desc);

	REQUIRE(plan.StageRegions.size() == 1);
	REQUIRE(plan.CopyRegions.size() == 1);

	// 8 blocks x 4 bytes = 32 pitch, 8 rows = 256, no padding.
	REQUIRE(plan.StageRegions[0].RowPitch == 32);
	REQUIRE(plan.StageRegions[0].SizeInBytes == 256);
	REQUIRE(plan.StageRegions[0].SourceOffset == 0);
	REQUIRE(plan.StageRegions[0].DestinationOffset == 0);
	REQUIRE(plan.CopyRegions[0].BaseLevel == 0);
	REQUIRE(plan.CopyRegions[0].BaseLayer == 0);
	REQUIRE(plan.CopyRegions[0].LayerCount == 1);
	REQUIRE(plan.RequiredBufferSizeBytes == 256);
}

TEST_CASE("Upload regions: mip pyramid batches into contiguous staging regions", "[memory]")
{
	// 8x8 -> 4x4 -> 2x2 -> 1x1, RGBA.
	const std::array<UploadImageLayout, 4> levels{
		UploadImageLayout{ 8, 8, 1 },
		UploadImageLayout{ 4, 4, 1 },
		UploadImageLayout{ 2, 2, 1 },
		UploadImageLayout{ 1, 1, 1 },
	};
	const UploadTextureDesc desc{ 4, 1, 1, false, UploadTextureDesc::Mode::BatchUploadRegions, std::span<const UploadImageLayout>(levels.data(), levels.size()) };

	const UploadPlan plan = BuildUploadPlan(desc);

	// One copy region per level - the batch rule splits at the level change.
	REQUIRE(plan.CopyRegions.size() == 4);
	REQUIRE(plan.CopyRegions[0].BufferOffset == 0);
	REQUIRE(plan.CopyRegions[1].BufferOffset == 256);
	REQUIRE(plan.CopyRegions[2].BufferOffset == 320);
	REQUIRE(plan.CopyRegions[3].BufferOffset == 336);

	// The mips are contiguous in source AND destination, so all four merge
	// into a single staging region: 256 + 64 + 16 + 4.
	REQUIRE(plan.StageRegions.size() == 1);
	REQUIRE(plan.StageRegions[0].SizeInBytes == 340);
	REQUIRE(plan.RequiredBufferSizeBytes == 340);
}

TEST_CASE("Upload regions: row padding removed, destination advances past it", "[memory]")
{
	// 5x5 blocks of 3 bytes: pitch 24 (15 padded to 24), image 120.
	const std::array<UploadImageLayout, 1> levels{ UploadImageLayout{ 5, 5, 1 } };
	const UploadTextureDesc desc{ 3, 1, 1, false, UploadTextureDesc::Mode::BatchUploadRegions, std::span<const UploadImageLayout>(levels.data(), levels.size()) };

	const UploadPlan plan = BuildUploadPlan(desc);

	REQUIRE(plan.StageRegions.size() == 1);
	REQUIRE(plan.StageRegions[0].RowPitch == 24);
	REQUIRE(plan.StageRegions[0].RowWidthInBlocks == 5);
	// The staging region carries the padded source length; the copy strips
	// the 9 bytes per row, and the next region starts on an aligned offset.
	REQUIRE(plan.StageRegions[0].SizeInBytes == 120);
	REQUIRE(plan.RequiredBufferSizeBytes == 120);

	// A second level (2 blocks wide: 6 bytes padded to 12, 2 rows = 24)
	// starts aligned to lcm(4,3)=12 after 120 -> 120 is already aligned.
	const std::array<UploadImageLayout, 2> pyramid{ UploadImageLayout{ 5, 5, 1 }, UploadImageLayout{ 2, 2, 1 } };
	const UploadTextureDesc twoLevelDesc{ 3, 1, 1, false, UploadTextureDesc::Mode::BatchUploadRegions, std::span<const UploadImageLayout>(pyramid.data(), pyramid.size()) };

	const UploadPlan twoPlan = BuildUploadPlan(twoLevelDesc);
	REQUIRE(twoPlan.CopyRegions.size() == 2);
	REQUIRE(twoPlan.CopyRegions[1].BufferOffset == 120);
	// The source gap (padding) is not copied: the second image is contiguous
	// with the first in both source and destination, so the staging regions
	// merge and the padded source lengths carry the plan.
	REQUIRE(twoPlan.StageRegions.size() == 1);
	REQUIRE(twoPlan.StageRegions[0].SourceOffset == 0);
	REQUIRE(twoPlan.StageRegions[0].SizeInBytes == 144);
	REQUIRE(twoPlan.RequiredBufferSizeBytes == 144);
}

TEST_CASE("Upload regions: cube faces are layers; per-image mode splits every region", "[memory]")
{
	const std::array<UploadImageLayout, 1> levels{ UploadImageLayout{ 4, 4, 1 } };

	const UploadTextureDesc batched{ 4, 1, 6, false, UploadTextureDesc::Mode::BatchUploadRegions, std::span<const UploadImageLayout>(levels.data(), levels.size()) };
	const UploadPlan batchPlan = BuildUploadPlan(batched);

	// One copy region covering all six faces as layers, one contiguous
	// staging region of 6 * 64.
	REQUIRE(batchPlan.CopyRegions.size() == 1);
	REQUIRE(batchPlan.CopyRegions[0].LayerCount == 6);
	REQUIRE(batchPlan.StageRegions.size() == 1);
	REQUIRE(batchPlan.StageRegions[0].SizeInBytes == 384);

	const UploadTextureDesc perImage{ 4, 1, 6, false, UploadTextureDesc::Mode::UploadRegionPerImage, std::span<const UploadImageLayout>(levels.data(), levels.size()) };
	const UploadPlan perImagePlan = BuildUploadPlan(perImage);

	REQUIRE(perImagePlan.CopyRegions.size() == 6);
	for (uint32_t face = 0; face < 6; ++face)
	{
		REQUIRE(perImagePlan.CopyRegions[face].BaseLayer == face);
		REQUIRE(perImagePlan.CopyRegions[face].LayerCount == 1);
		REQUIRE(perImagePlan.CopyRegions[face].BufferOffset == face * 64);
	}
	// The mode splits copy regions, not staging regions: contiguous source
	// and destination still merge.
	REQUIRE(perImagePlan.StageRegions.size() == 1);
	REQUIRE(perImagePlan.RequiredBufferSizeBytes == 384);
}

TEST_CASE("Upload regions: 3D slices stay inside one image", "[memory]")
{
	const std::array<UploadImageLayout, 1> levels{ UploadImageLayout{ 4, 4, 3 } };
	const UploadTextureDesc desc{ 4, 1, 1, true, UploadTextureDesc::Mode::BatchUploadRegions, std::span<const UploadImageLayout>(levels.data(), levels.size()) };

	const UploadPlan plan = BuildUploadPlan(desc);

	// Three slices of 4x4 RGBA = 3 * 64, one image, one layer, merged.
	REQUIRE(plan.CopyRegions.size() == 1);
	REQUIRE(plan.CopyRegions[0].LayerCount == 1);
	REQUIRE(plan.StageRegions.size() == 1);
	REQUIRE(plan.StageRegions[0].SizeInBytes == 192);
	REQUIRE(plan.RequiredBufferSizeBytes == 192);
}

TEST_CASE("Upload regions: degenerate descriptions answer an empty plan", "[memory]")
{
	const UploadTextureDesc noBlock{ 0, 1, 1, false, UploadTextureDesc::Mode::BatchUploadRegions, {} };
	REQUIRE(BuildUploadPlan(noBlock).RequiredBufferSizeBytes == 0);

	const UploadImageLayout zeroWidth{ 0, 4, 1 };
	const UploadTextureDesc degenerate{ 4, 1, 1, false, UploadTextureDesc::Mode::BatchUploadRegions, std::span<const UploadImageLayout>(&zeroWidth, 1) };
	const UploadPlan plan = BuildUploadPlan(degenerate);
	REQUIRE(plan.StageRegions.empty());
	REQUIRE(plan.CopyRegions.empty());
	REQUIRE(plan.RequiredBufferSizeBytes == 0);
}

TEST_CASE("Staging pool state: record, submit, reclaim, rewind", "[memory]")
{
	StagingPoolState state{};

	// Nothing recorded: nothing to submit, nothing to reclaim, rewinding is
	// trivially allowed (0 == 0).
	REQUIRE(state.PendingCommandBuffers() == 0);
	REQUIRE_FALSE(state.Submit());
	REQUIRE_FALSE(state.Reclaim());
	REQUIRE(state.CanRewind());

	// Record 3 command buffers, stage 2 buffers.
	state.CommandBuffersRecorded = 3;
	state.StagedBuffers = 2;

	REQUIRE(state.PendingCommandBuffers() == 3);
	REQUIRE(state.Submit());
	REQUIRE(state.PendingCommandBuffers() == 0);
	REQUIRE(state.Submitted == 3);
	REQUIRE(state.PendingFences == 1);
	REQUIRE(state.SubmittedStagedBuffers == 2);
	REQUIRE(state.CanRewind());

	// Record 2 more and stage 1 more, submit that batch, then the fences
	// signal: only the submitted work's buffers are reclaimed - the buffer
	// staged after the last submit stays in the caller's hands.
	state.CommandBuffersRecorded = 5;
	state.StagedBuffers = 3;
	REQUIRE(state.Submit());
	REQUIRE(state.SubmittedStagedBuffers == 3);
	REQUIRE(state.Reclaim());
	REQUIRE(state.StagedBuffers == 0);
	REQUIRE(state.PendingFences == 0);
	REQUIRE(state.CanRewind());

	// Rewind resets the cursors for the next batch.
	state.Rewind();
	REQUIRE(state.CommandBuffersRecorded == 0);
	REQUIRE(state.Submitted == 0);
}

TEST_CASE("Mapped memory: subset views validate against the parent region", "[memory]")
{
	MapRegion resolved{};

	// A plain interior subset.
	REQUIRE(ValidateMapSubset(MapRegion{ 0, 256 }, MapRegion{ 64, 128 }, resolved));
	REQUIRE(resolved.Offset == 64);
	REQUIRE(resolved.Size == 128);

	// WHOLE_SIZE resolves against what is left of the parent.
	REQUIRE(ValidateMapSubset(MapRegion{ 0, 256 }, MapRegion{ 64, WholeSize }, resolved));
	REQUIRE(resolved.Size == 192);

	// The resolved offset is absolute: views nest against the allocation.
	REQUIRE(ValidateMapSubset(MapRegion{ 16, 256 }, MapRegion{ 64, 128 }, resolved));
	REQUIRE(resolved.Offset == 80);
	REQUIRE(resolved.Size == 128);

	// Past the end answers false, and the caller keeps its original view.
	REQUIRE_FALSE(ValidateMapSubset(MapRegion{ 0, 256 }, MapRegion{ 300, WholeSize }, resolved));
	REQUIRE_FALSE(ValidateMapSubset(MapRegion{ 0, 256 }, MapRegion{ 200, 100 }, resolved));

	// An exact fit at the boundary is allowed.
	REQUIRE(ValidateMapSubset(MapRegion{ 0, 256 }, MapRegion{ 256, WholeSize }, resolved));
	REQUIRE(resolved.Size == 0);
}
