#pragma once

#include <cstdint>
#include <vector>

namespace Forge {

	// A cell on a navigation grid.
	struct GridPoint
	{
		int x = 0;
		int y = 0;

		bool operator==(const GridPoint& other) const { return x == other.x && y == other.y; }
	};

	// A grid a path is found on.
	//
	// The world's walkable area as a grid: a cell is either free or it is
	// not. It is the shape a level is authored in - a top-down map, a
	// tilemap, a navigation mesh's cells - and it is the shape the search
	// runs on.
	struct NavigationGrid
	{
		uint32_t width = 0;
		uint32_t height = 0;

		// One byte per cell: 1 is blocked, 0 is free. A byte and not a bit,
		// because a grid of a million cells is a megabyte and a bit-packed
		// one is a bug waiting for a shift.
		std::vector<uint8_t> blocked;

		NavigationGrid() = default;
		NavigationGrid(uint32_t width, uint32_t height)
			: width(width), height(height), blocked(static_cast<size_t>(width) * height, 0) {}

		[[nodiscard]] bool IsInside(int x, int y) const
		{
			return x >= 0 && y >= 0 && static_cast<uint32_t>(x) < width && static_cast<uint32_t>(y) < height;
		}

		[[nodiscard]] bool IsBlocked(int x, int y) const
		{
			if (!IsInside(x, y))
				return true;  // outside the grid is not walkable
			return blocked[static_cast<size_t>(y) * width + static_cast<size_t>(x)] != 0;
		}

		void Block(int x, int y, bool value = true)
		{
			if (!IsInside(x, y))
				return;
			blocked[static_cast<size_t>(y) * width + static_cast<size_t>(x)] = value ? 1 : 0;
		}
	};

	namespace AStar {

		// The shortest path from one cell to another, or an empty list when
		// there is none.
		//
		// The path starts at the start cell and ends at the goal cell. A
		// diagonal move is only made when both of the cells it passes
		// between are free: a character that walks through the corner of a
		// wall is not walking, it is clipping.
		[[nodiscard]] std::vector<GridPoint> FindPath(const NavigationGrid& grid, GridPoint start,
			GridPoint goal, bool allowDiagonal = true);

		// The path without the zigzag: the cells that are not needed are
		// taken out, so the path goes in a straight line where it can.
		[[nodiscard]] std::vector<GridPoint> SmoothPath(const NavigationGrid& grid, const std::vector<GridPoint>& path);

		// Whether the straight line between two cells is free.
		[[nodiscard]] bool HasLineOfSight(const NavigationGrid& grid, GridPoint from, GridPoint to);

	}  // namespace AStar

}  // namespace Forge
