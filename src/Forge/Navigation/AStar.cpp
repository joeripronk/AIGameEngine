#include "fgpch.h"
#include "Forge/Navigation/AStar.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace Forge {

	namespace AStar {

		namespace {

			// The cell a point is on, as an index into the grid's cells.
			size_t Index(const NavigationGrid& grid, int x, int y)
			{
				return static_cast<size_t>(y) * grid.width + static_cast<size_t>(x);
			}

			// The cost of a step: a straight step is one, a diagonal one is
			// a root of two - the same distance, as the crow flies.
			float StepCost(bool diagonal)
			{
				return diagonal ? std::sqrt(2.0f) : 1.0f;
			}

			// The distance from a cell to the goal, ignoring the walls: the
			// part of the search that is a guess.
			float Heuristic(int x, int y, const GridPoint& goal, bool allowDiagonal)
			{
				const float dx = static_cast<float>(std::abs(x - goal.x));
				const float dy = static_cast<float>(std::abs(y - goal.y));

				if (!allowDiagonal)
					return dx + dy;

				// The straight-line distance, which is the smallest one
				// possible and so never overestimates.
				return std::sqrt(dx * dx + dy * dy);
			}

			struct OpenCell
			{
				float score = 0.0f;
				int x = 0;
				int y = 0;

				// The open cells are a priority queue, and the queue takes
				// the *largest* score first, so the comparison is reversed.
				bool operator>(const OpenCell& other) const { return score > other.score; }
			};

		}  // namespace

		std::vector<GridPoint> FindPath(const NavigationGrid& grid, GridPoint start, GridPoint goal,
			bool allowDiagonal)
		{
			std::vector<GridPoint> path;

			if (grid.width == 0 || grid.height == 0)
				return path;

			if (!grid.IsInside(start.x, start.y) || !grid.IsInside(goal.x, goal.y))
				return path;

			if (grid.IsBlocked(start.x, start.y) || grid.IsBlocked(goal.x, goal.y))
				return path;

			if (start == goal)
			{
				path.push_back(start);
				return path;
			}

			const size_t cellCount = static_cast<size_t>(grid.width) * grid.height;

			// The cost of the cheapest way to each cell, and where it came
			// from. A cell that has not been reached has no cost yet.
			std::vector<float> cost(cellCount, std::numeric_limits<float>::infinity());
			std::vector<int> cameFrom(cellCount, -1);
			std::vector<uint8_t> closed(cellCount, 0);

			std::priority_queue<OpenCell, std::vector<OpenCell>, std::greater<OpenCell>> open;

			cost[Index(grid, start.x, start.y)] = 0.0f;
			open.push({ Heuristic(start.x, start.y, goal, allowDiagonal), start.x, start.y });

			static constexpr int kDirections[8][2] = {
				{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
				{ 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 },
			};

			bool found = false;
			while (!open.empty())
			{
				const OpenCell current = open.top();
				open.pop();

				const size_t currentIndex = Index(grid, current.x, current.y);
				if (closed[currentIndex])
					continue;  // already expanded: the cheapest way to it is known
				closed[currentIndex] = 1;

				if (current.x == goal.x && current.y == goal.y)
				{
					found = true;
					break;
				}

				for (int direction = 0; direction < 8; ++direction)
				{
					const bool diagonal = direction >= 4;
					if (diagonal && !allowDiagonal)
						continue;

					const int nx = current.x + kDirections[direction][0];
					const int ny = current.y + kDirections[direction][1];

					if (!grid.IsInside(nx, ny) || grid.IsBlocked(nx, ny))
						continue;

					// A diagonal move passes between two cells, and both of
					// them have to be free: a character that walks through
					// the corner of a wall is not walking.
					if (diagonal)
					{
						if (grid.IsBlocked(current.x + kDirections[direction][0], current.y) ||
							grid.IsBlocked(current.x, current.y + kDirections[direction][1]))
						{
							continue;
						}
					}

					const float newCost = cost[currentIndex] + StepCost(diagonal);
					const size_t neighbourIndex = Index(grid, nx, ny);
					if (newCost >= cost[neighbourIndex])
						continue;

					cost[neighbourIndex] = newCost;
					cameFrom[neighbourIndex] = static_cast<int>(currentIndex);
					open.push({ newCost + Heuristic(nx, ny, goal, allowDiagonal), nx, ny });
				}
			}

			if (!found)
				return path;

			// The path is walked back from the goal and then reversed.
			size_t index = Index(grid, goal.x, goal.y);
			while (index != static_cast<size_t>(-1))
			{
				path.push_back({ static_cast<int>(index % grid.width), static_cast<int>(index / grid.width) });

				const int previous = cameFrom[index];
				if (previous < 0)
					break;
				index = static_cast<size_t>(previous);
			}

			std::reverse(path.begin(), path.end());
			return path;
		}

		bool HasLineOfSight(const NavigationGrid& grid, GridPoint from, GridPoint to)
		{
			// Bresenham's line: the cells the straight line between the two
			// passes through, and whether any of them is blocked.
			int x = from.x;
			int y = from.y;
			const int dx = std::abs(to.x - from.x);
			const int dy = -std::abs(to.y - from.y);
			const int stepX = from.x < to.x ? 1 : -1;
			const int stepY = from.y < to.y ? 1 : -1;
			int error = dx + dy;

			while (true)
			{
				if (x != from.x || y != from.y)
				{
					if (grid.IsBlocked(x, y))
						return false;
				}

				if (x == to.x && y == to.y)
					return true;

				const int doubled = 2 * error;
				if (doubled >= dy)
				{
					error += dy;
					x += stepX;
				}
				if (doubled <= dx)
				{
					error += dx;
					y += stepY;
				}
			}
		}

		std::vector<GridPoint> SmoothPath(const NavigationGrid& grid, const std::vector<GridPoint>& path)
		{
			if (path.size() < 3)
				return path;

			std::vector<GridPoint> smoothed;
			smoothed.push_back(path.front());

			size_t anchor = 0;
			for (size_t i = 2; i < path.size(); ++i)
			{
				// The path goes in a straight line as far as it can: the
				// cells in between are not needed.
				if (!HasLineOfSight(grid, path[anchor], path[i]))
				{
					smoothed.push_back(path[i - 1]);
					anchor = i - 1;
				}
			}

			smoothed.push_back(path.back());
			return smoothed;
		}

	}  // namespace AStar

}  // namespace Forge
