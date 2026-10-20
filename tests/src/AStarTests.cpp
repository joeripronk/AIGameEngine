#include <catch2/catch_test_macros.hpp>

#include "Forge/Navigation/AStar.h"

using namespace Forge;

namespace {

	// A grid with a wall down the middle, with a gap at the bottom.
	NavigationGrid MakeGridWithWall()
	{
		NavigationGrid grid(5, 5);
		for (int y = 0; y < 4; ++y)
			grid.Block(2, y);
		return grid;
	}

}  // namespace

TEST_CASE("A path is found on an open grid", "[navigation][astar]")
{
	NavigationGrid grid(5, 5);

	const std::vector<GridPoint> path = AStar::FindPath(grid, { 0, 0 }, { 4, 4 });

	REQUIRE_FALSE(path.empty());
	REQUIRE(path.front() == GridPoint{ 0, 0 });
	REQUIRE(path.back() == GridPoint{ 4, 4 });

	// Every step of the path is to a cell next to the one before it.
	for (size_t i = 1; i < path.size(); ++i)
	{
		const int dx = std::abs(path[i].x - path[i - 1].x);
		const int dy = std::abs(path[i].y - path[i - 1].y);
		REQUIRE(dx <= 1);
		REQUIRE(dy <= 1);
		REQUIRE(dx + dy > 0);
	}

	// And no cell of the path is blocked.
	for (const GridPoint& point : path)
		REQUIRE_FALSE(grid.IsBlocked(point.x, point.y));
}

TEST_CASE("A path goes around a wall", "[navigation][astar]")
{
	const NavigationGrid grid = MakeGridWithWall();

	const std::vector<GridPoint> path = AStar::FindPath(grid, { 0, 2 }, { 4, 2 });

	REQUIRE_FALSE(path.empty());
	REQUIRE(path.front() == GridPoint{ 0, 2 });
	REQUIRE(path.back() == GridPoint{ 4, 2 });

	// The path did not walk through the wall.
	for (const GridPoint& point : path)
		REQUIRE_FALSE(grid.IsBlocked(point.x, point.y));
}

TEST_CASE("A diagonal step does not cut a corner", "[navigation][astar]")
{
	NavigationGrid grid(3, 3);
	// The cell between the start and the diagonal neighbour is blocked, so
	// the diagonal move is not allowed.
	grid.Block(1, 0);

	const std::vector<GridPoint> path = AStar::FindPath(grid, { 0, 0 }, { 1, 1 });

	REQUIRE_FALSE(path.empty());
	for (size_t i = 1; i < path.size(); ++i)
	{
		const int dx = path[i].x - path[i - 1].x;
		const int dy = path[i].y - path[i - 1].y;
		// Either a straight step or a diagonal one, and never the diagonal
		// one that the blocked cell forbids.
		const bool straight = dx * dx + dy * dy == 1;
		const bool diagonal = dx != 0 && dy != 0;
		REQUIRE((straight || diagonal));
		if (dx != 0 && dy != 0)
		{
			REQUIRE_FALSE(grid.IsBlocked(path[i - 1].x + dx, path[i - 1].y));
			REQUIRE_FALSE(grid.IsBlocked(path[i - 1].x, path[i - 1].y + dy));
		}
	}
}

TEST_CASE("There is no path when the goal cannot be reached", "[navigation][astar]")
{
	NavigationGrid grid(3, 3);
	// The goal is walled in.
	grid.Block(1, 0);
	grid.Block(0, 1);
	grid.Block(1, 2);

	const std::vector<GridPoint> path = AStar::FindPath(grid, { 0, 0 }, { 1, 1 });
	REQUIRE(path.empty());
}

TEST_CASE("A path to where one already is is one cell", "[navigation][astar]")
{
	NavigationGrid grid(3, 3);
	const std::vector<GridPoint> path = AStar::FindPath(grid, { 1, 1 }, { 1, 1 });
	REQUIRE(path.size() == 1);
	REQUIRE(path.front() == GridPoint{ 1, 1 });
}

TEST_CASE("A path that starts or ends in a wall does not exist", "[navigation][astar]")
{
	NavigationGrid grid(3, 3);
	grid.Block(0, 0);
	grid.Block(2, 2);

	REQUIRE(AStar::FindPath(grid, { 0, 0 }, { 1, 1 }).empty());
	REQUIRE(AStar::FindPath(grid, { 1, 1 }, { 2, 2 }).empty());
}

TEST_CASE("A smoothed path does not zigzag", "[navigation][astar]")
{
	NavigationGrid grid(5, 5);

	// A path that goes right, up, right, up - the smoothed one goes
	// diagonally.
	std::vector<GridPoint> path = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 2, 1 }, { 2, 2 } };
	const std::vector<GridPoint> smoothed = AStar::SmoothPath(grid, path);

	REQUIRE(smoothed.size() < path.size());
	REQUIRE(smoothed.front() == GridPoint{ 0, 0 });
	REQUIRE(smoothed.back() == GridPoint{ 2, 2 });
}

TEST_CASE("A smoothed path keeps the corners of a wall", "[navigation][astar]")
{
	const NavigationGrid grid = MakeGridWithWall();

	// The wall covers x = 2 from y = 0 to y = 3, so the path goes under it
	// through the gap at y = 4.
	std::vector<GridPoint> path = { { 0, 2 }, { 1, 2 }, { 1, 3 }, { 1, 4 }, { 2, 4 }, { 3, 4 }, { 3, 3 }, { 3, 2 }, { 4, 2 } };
	const std::vector<GridPoint> smoothed = AStar::SmoothPath(grid, path);

	// The path still goes around the wall: every cell of it is free.
	for (const GridPoint& point : smoothed)
		REQUIRE_FALSE(grid.IsBlocked(point.x, point.y));

	REQUIRE(smoothed.front() == GridPoint{ 0, 2 });
	REQUIRE(smoothed.back() == GridPoint{ 4, 2 });
}

TEST_CASE("The line of sight is what it says", "[navigation][astar]")
{
	NavigationGrid grid(3, 3);

	REQUIRE(AStar::HasLineOfSight(grid, { 0, 0 }, { 2, 2 }));
	REQUIRE(AStar::HasLineOfSight(grid, { 0, 0 }, { 0, 0 }));

	grid.Block(1, 1);
	REQUIRE_FALSE(AStar::HasLineOfSight(grid, { 0, 0 }, { 2, 2 }));
}
