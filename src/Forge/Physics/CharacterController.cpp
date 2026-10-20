#include "fgpch.h"
#include "Forge/Physics/CharacterController.h"

#include <algorithm>
#include <cmath>

namespace Forge {

	namespace Character {

		namespace {

			// The overlap of two ranges: positive when they overlap.
			float RangeOverlap(float aMin, float aMax, float bMin, float bMax)
			{
				return std::min(aMax, bMax) - std::max(aMin, bMin);
			}

			// The character's box, standing on the point it is at.
			void BoxOf(const glm::vec3& position, const CharacterShape& shape, glm::vec3& outMin, glm::vec3& outMax)
			{
				outMin = position - glm::vec3(shape.Radius, 0.0f, shape.Radius);
				outMax = position + glm::vec3(shape.Radius, shape.Height, shape.Radius);
			}

			// The box the character passes through on its way from one point
			// to another.
			//
			// The move is swept, not sampled: a character that walks five
			// metres in one frame ends up past a wall, and a test of the
			// position it ends up in finds nothing to collide with. The wall
			// is in the way the whole way, and the swept box is where the
			// character was going to be.
			void SweptBoxOf(const glm::vec3& from, const glm::vec3& to, const CharacterShape& shape,
				glm::vec3& outMin, glm::vec3& outMax)
			{
				BoxOf(from, shape, outMin, outMax);
				glm::vec3 toMin;
				glm::vec3 toMax;
				BoxOf(to, shape, toMin, toMax);
				outMin = glm::min(outMin, toMin);
				outMax = glm::max(outMax, toMax);
			}

			bool Overlaps(const glm::vec3& min, const glm::vec3& max, const CharacterObstacle& obstacle)
			{
				return RangeOverlap(min.x, max.x, obstacle.Min.x, obstacle.Max.x) > 0.0f
					&& RangeOverlap(min.y, max.y, obstacle.Min.y, obstacle.Max.y) > 0.0f
					&& RangeOverlap(min.z, max.z, obstacle.Min.z, obstacle.Max.z) > 0.0f;
			}

		}  // namespace

		CharacterMoveResult Move(const glm::vec3& position, const glm::vec3& displacement,
			const CharacterShape& shape, const CharacterSettings& settings,
			const std::vector<CharacterObstacle>& obstacles)
		{
			CharacterMoveResult result;
			result.Position = position;

			// The horizontal axes are moved one at a time. A character that
			// is stopped on one of them still moves on the other, which is
			// what sliding along a wall is.
			for (int axis : { 0, 2 })
			{
				const float amount = displacement[axis];
				if (amount == 0.0f)
					continue;

				bool blocked = false;

				// A staircase is a series of steps, so the move is retried
				// after each climb. A few is enough: a staircase taller than
				// that is a wall.
				for (int attempt = 0; attempt < 4; ++attempt)
				{
					glm::vec3 to = result.Position;
					to[axis] += amount;

					glm::vec3 min;
					glm::vec3 max;
					SweptBoxOf(result.Position, to, shape, min, max);

					// The obstacles in the way, the highest one that is low
					// enough to climb, and how far the character can go
					// before it touches one of them.
					bool somethingInTheWay = false;
					float climbable = 0.0f;
					float allowed = amount;
					for (const CharacterObstacle& obstacle : obstacles)
					{
						if (!Overlaps(min, max, obstacle))
							continue;

						somethingInTheWay = true;

						const float stepUp = obstacle.Max.y - result.Position.y;
						if (stepUp > 0.0f && stepUp <= settings.StepHeight)
							climbable = std::max(climbable, obstacle.Max.y);

						// The distance to the obstacle: the character's
						// leading edge to the obstacle's near edge. The
						// character goes as far as that and no further, so
						// it ends up touching the wall rather than where it
						// started.
						const float direction = amount > 0.0f ? 1.0f : -1.0f;
						const float leadingEdge = result.Position[axis] + direction * shape.Radius;
						const float nearEdge = direction > 0.0f ? obstacle.Min[axis] : obstacle.Max[axis];
						const float gap = (nearEdge - leadingEdge) * direction;
						allowed = std::min(allowed, gap);
					}

					if (!somethingInTheWay)
					{
						result.Position = to;
						break;
					}

					if (climbable > 0.0f)
					{
						// A kerb is a step; the character goes up and the move
						// goes on.
						result.Position.y = climbable;
						continue;
					}

					// A wall is a wall: the character stops against it, and
					// the movement along this axis is lost.
					result.Position[axis] += allowed;
					blocked = true;
					result.OnWall = true;
					result.WallNormal = glm::vec3(0.0f);
					result.WallNormal[axis] = amount > 0.0f ? -1.0f : 1.0f;
					break;
				}

				if (blocked)
					continue;
			}

			// The vertical movement last, so the character lands on what is
			// under it and its head hits what is above it.
			if (displacement.y != 0.0f)
			{
				glm::vec3 to = result.Position;
				to.y += displacement.y;

				glm::vec3 min;
				glm::vec3 max;
				SweptBoxOf(result.Position, to, shape, min, max);

				// The obstacle the character meets first: the highest top
				// among the ones it falls onto, and the lowest bottom among
				// the ones it hits with its head.
				float landing = 0.0f;
				float ceiling = 0.0f;
				bool landingFound = false;
				bool ceilingFound = false;

				for (const CharacterObstacle& obstacle : obstacles)
				{
					if (!Overlaps(min, max, obstacle))
						continue;

					if (displacement.y < 0.0f)
					{
						// The character is falling: it lands on the obstacle
						// whose top is highest.
						if (!landingFound || obstacle.Max.y > landing)
						{
							landing = obstacle.Max.y;
							landingFound = true;
						}
					}
					else
					{
						// The character is rising: its head hits the obstacle
						// whose bottom is lowest.
						if (!ceilingFound || obstacle.Min.y < ceiling)
						{
							ceiling = obstacle.Min.y;
							ceilingFound = true;
						}
					}
				}

				if (landingFound)
				{
					result.Position.y = landing;
					result.OnFloor = true;
				}
				else if (ceilingFound)
				{
					result.Position.y = ceiling - shape.Height;
					result.OnCeiling = true;
				}
				else
				{
					result.Position = to;
				}
			}

			// A character that is standing on something when it stops moving
			// is still on the floor: the floor is what it is standing on, not
			// what it last moved into.
			if (!result.OnFloor && displacement.y <= 0.0f)
				result.OnFloor = IsOnFloor(result.Position, shape, settings, obstacles);

			return result;
		}

		bool IsOnFloor(const glm::vec3& position, const CharacterShape& shape,
			const CharacterSettings& settings, const std::vector<CharacterObstacle>& obstacles)
		{
			// A check just under the character's feet: the floor is within
			// the tolerance, so a character that is standing exactly on it
			// counts as standing on it.
			glm::vec3 min;
			glm::vec3 max;
			BoxOf(position, shape, min, max);

			for (const CharacterObstacle& obstacle : obstacles)
			{
				// The obstacle is under the character: its top is at or below
				// the character's feet, and the two overlap sideways.
				const float sidewaysX = RangeOverlap(min.x, max.x, obstacle.Min.x, obstacle.Max.x);
				const float sidewaysZ = RangeOverlap(min.z, max.z, obstacle.Min.z, obstacle.Max.z);
				if (sidewaysX <= 0.0f || sidewaysZ <= 0.0f)
					continue;

				if (obstacle.Max.y <= position.y + settings.FloorTolerance &&
					obstacle.Max.y >= position.y - settings.FloorTolerance)
				{
					return true;
				}
			}

			return false;
		}

		std::vector<CharacterObstacle> CollectObstacles(const std::vector<glm::vec3>& boxMinima,
			const std::vector<glm::vec3>& boxMaxima)
		{
			std::vector<CharacterObstacle> obstacles;
			const size_t count = std::min(boxMinima.size(), boxMaxima.size());
			obstacles.reserve(count);

			for (size_t i = 0; i < count; ++i)
			{
				CharacterObstacle obstacle;
				obstacle.Min = boxMinima[i];
				obstacle.Max = boxMaxima[i];
				obstacles.push_back(obstacle);
			}

			return obstacles;
		}

	}  // namespace Character

}  // namespace Forge
