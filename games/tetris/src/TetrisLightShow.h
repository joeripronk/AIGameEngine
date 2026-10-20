#pragma once

#include "Forge/Scene/Entity.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Tetris {

	// The light show: a rig of coloured lamps in front of the playfield, and
	// the show they put on.
	//
	// The lamps are the engine's point lights: real lights in the scene, not
	// a picture of lights. The show itself is a pure function of the clock,
	// so it is the same every run and it can be tested without a window.
	//
	// The lamps hang at the top of the playfield and in front of it: a lamp
	// behind the playfield lights the backs of the blocks, and the camera
	// sees the fronts.
	class LightShow
	{
	public:
		LightShow() = default;
		~LightShow();

		LightShow(const LightShow&) = delete;
		LightShow& operator=(const LightShow&) = delete;

		// How many lamps the rig has. The engine's shader takes eight, and
		// the show is worth more with six in front of the board than with
		// eight hidden behind it.
		static constexpr uint32_t kLightCount = 6;

		// The name a lamp carries. The scene may hold lamps of its own - an
		// author can place them - and the show uses those when it has them.
		static std::string LightName(uint32_t index);

		// Where a lamp hangs: across the top of the playfield, in front of
		// it.
		static glm::vec3 LampPosition(uint32_t index);

		// One lamp at one moment of the show.
		struct Sample
		{
			glm::vec3 Color = glm::vec3(1.0f);
			float Intensity = 1.0f;
			glm::vec3 Position = glm::vec3(0.0f);
		};

		// The show at a moment: a pure function of the clock and the lamp's
		// place in the rig. The same moment always gives the same show, so
		// the show is reproducible and it can be asserted on.
		static Sample SampleAt(float time, uint32_t index);

		// Attach to a scene and take hold of its lamps: the scene's own when
		// it has them, and new ones when it does not.
		bool Initialize(Forge::Scene* scene);

		// Let go of the lamps the show created. The scene's own are left
		// alone: the scene owns them.
		void Shutdown();

		// The show, one frame on.
		void Update(float dt);

		// The show on or off. The lamps stay where they are when it is off -
		// they are dark, not gone.
		void SetEnabled(bool enabled) { m_Enabled = enabled; }
		bool IsEnabled() const { return m_Enabled; }

		// A flash: every lamp goes white for a moment. The show answering
		// the game is what makes it a show and not a screensaver.
		void Flash(float strength);

	private:
		// The lamp entity, its bulb (the little glowing thing that shows
		// where the light is) and whether the show made them.
		struct Lamp
		{
			Forge::Entity Light;
			Forge::Entity Bulb;
			bool Owned = false;
		};

		Forge::Scene* m_Scene = nullptr;
		std::vector<Lamp> m_Lamps;
		float m_Time = 0.0f;
		float m_Flash = 0.0f;
		bool m_Enabled = true;
	};

}  // namespace Tetris
