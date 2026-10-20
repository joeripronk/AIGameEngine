#include "fgpch.h"

#include "TetrisLightShow.h"

#include "TetrisBoardView.h"

#include "Forge/Core/Log.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Scene.h"

#include <cmath>
#include <string>

namespace Tetris {

	namespace {

		// The show's tempo. The hue walks the spectrum once every
		// kHueSeconds, the brightness breathes twice a second, and the lamps
		// sway once every kSwaySeconds.
		constexpr float kHueSeconds = 6.0f;
		constexpr float kPulseSpeed = 2.0f;
		constexpr float kSwaySpeed = 0.7f;

		// How far a lamp drifts from its place, in world units. The drift in
		// depth is the smaller of the two: a lamp that comes towards the
		// camera grows, and one that grows too much is cut in half by the
		// top of the frame.
		constexpr float kSwayAmount = 0.35f;
		constexpr float kSwayDepth = 0.2f;

		// The lamps are stage lights: they throw across the stage, so their
		// falloff is gentler than a lamp's would be. A lamp with the
		// textbook falloff reaches the bottom of the board at a fraction of
		// its brightness, and the show would be a stripe at the top.
		constexpr float kConstant = 1.0f;
		constexpr float kLinear = 0.07f;
		constexpr float kQuadratic = 0.010f;

		// How bright the rig is, and how bright a lamp gets at the top of
		// its breath.
		constexpr float kBaseIntensity = 0.8f;

		// How long a flash lasts, in seconds.
		constexpr float kFlashSeconds = 0.35f;

		// The bulb: the little glowing thing that shows where the light is.
		constexpr float kBulbSize = 0.42f;
		constexpr float kBulbEmissive = 6.0f;

		// The mesh the bulbs are drawn with. The game's blocks use the same
		// one, and a light has no shape of its own.
		constexpr const char* kBlockMesh = "resources/models/cube.glb";

		constexpr float kTwoPi = 6.28318530717958647692f;

		// A colour of the spectrum: the hue is a fraction of a turn, and the
		// colour is that hue at full saturation and full value.
		glm::vec3 HueToRgb(float hue)
		{
			hue = hue - std::floor(hue);

			// The wheel in thirds: red to yellow, yellow to green, green to
			// blue. One third of the wheel at a time, and the ramp is the
			// fraction into it.
			const float sector = hue * 3.0f;
			if (sector < 1.0f)
				return glm::vec3(1.0f, sector, 0.0f);
			if (sector < 2.0f)
				return glm::vec3(2.0f - sector, 1.0f, 0.0f);
			return glm::vec3(0.0f, 1.0f, sector - 2.0f);
		}

	}  // namespace

	LightShow::~LightShow()
	{
		Shutdown();
	}

	std::string LightShow::LightName(uint32_t index)
	{
		return "ShowLight_" + std::to_string(index);
	}

	glm::vec3 LightShow::LampPosition(uint32_t index)
	{
		const glm::vec3 centre = BoardView::BoardCentre();
		const glm::vec3 size = BoardView::BoardSize();

		// The rig spans the width of the playfield, a little inside its
		// edges, and hangs at the top of it - in front, so the light reaches
		// the faces the camera sees.
		const float span = size.x * 0.82f;
		const float spacing = kLightCount > 1 ? span / static_cast<float>(kLightCount - 1) : 0.0f;
		const float x = centre.x - span * 0.5f + spacing * static_cast<float>(index);

		return glm::vec3(x, centre.y + size.y * 0.5f - 1.2f, centre.z + 3.2f);
	}

	LightShow::Sample LightShow::SampleAt(float time, uint32_t index)
	{
		Sample sample;

		// The hue walks the spectrum, and each lamp is a step ahead of the
		// one before it: the rig is a rainbow that moves.
		const float phase = static_cast<float>(index) / static_cast<float>(kLightCount);
		const float hue = std::fmod(time / kHueSeconds + phase, 1.0f);
		sample.Color = HueToRgb(hue);

		// The brightness breathes, and the lamps are out of step with each
		// other: a row of lamps all at the same brightness is a strip light.
		const float pulse = 0.5f + 0.5f * std::sin(time * kPulseSpeed * kTwoPi * 0.25f + phase * kTwoPi);
		sample.Intensity = kBaseIntensity * (0.55f + 0.45f * pulse);

		// The lamp drifts: a slow sway along the rig and a little in and out
		// of the playfield, so the pools of light move.
		sample.Position = LampPosition(index) + glm::vec3(
			std::sin(time * kSwaySpeed * kTwoPi * 0.25f + phase * kTwoPi) * kSwayAmount,
			0.0f,
			std::cos(time * kSwaySpeed * kTwoPi * 0.17f + phase * kTwoPi) * kSwayDepth);

		return sample;
	}

	bool LightShow::Initialize(Forge::Scene* scene)
	{
		Shutdown();

		if (!scene)
			return false;

		m_Scene = scene;

		// The scene's own lamps are used when it has them: the show is
		// presentation, and the scene is where the game keeps what the
		// author can change.
		for (uint32_t index = 0; index < kLightCount; ++index)
		{
			Forge::Entity light = scene->FindEntityByName(LightName(index));

			Lamp lamp;
			lamp.Light = light;
			lamp.Owned = false;

			if (!light)
			{
				// The scene has no lamp here, so the show makes one.
				light = scene->CreateEntity(LightName(index));
				lamp.Light = light;
				lamp.Owned = true;

				auto& component = light.AddComponent<Forge::LightComponent>();
				component.Type = Forge::LightComponent::Type::Point;
				component.Constant = kConstant;
				component.Linear = kLinear;
				component.Quadratic = kQuadratic;

				light.GetComponent<Forge::TransformComponent>().Translation = LampPosition(index);
			}
			else
			{
				// The scene's lamp: the show drives it, so it is a point
				// light with the show's falloff.
				auto& component = light.GetComponent<Forge::LightComponent>();
				component.Type = Forge::LightComponent::Type::Point;
				component.Constant = kConstant;
				component.Linear = kLinear;
				component.Quadratic = kQuadratic;
			}

			// The bulb: the visible source. It is the show's own, so it is
			// made and destroyed with the show.
			Forge::Entity bulb = scene->CreateEntity(LightName(index) + "_Bulb");
			bulb.GetComponent<Forge::TransformComponent>().Translation = LampPosition(index);
			bulb.GetComponent<Forge::TransformComponent>().Scale = glm::vec3(kBulbSize);
			bulb.AddComponent<Forge::MeshComponent>().ModelPath = kBlockMesh;

			auto& material = bulb.AddComponent<Forge::PBRMaterialComponent>();
			material.Albedo = glm::vec3(0.0f);
			material.Roughness = 0.4f;
			material.Metallic = 0.0f;
			material.Emissive = glm::vec3(1.0f);
			material.EmissiveIntensity = kBulbEmissive;

			lamp.Bulb = bulb;
			m_Lamps.push_back(lamp);
		}

		return true;
	}

	void LightShow::Shutdown()
	{
		if (m_Scene)
		{
			for (Lamp& lamp : m_Lamps)
			{
				// The bulb is always the show's own.
				if (lamp.Bulb)
					m_Scene->DestroyEntity(lamp.Bulb);

				// The lamp is the scene's when the scene had one, and the
				// show's when the show made it.
				if (lamp.Owned && lamp.Light)
					m_Scene->DestroyEntity(lamp.Light);
			}
		}

		m_Lamps.clear();
		m_Scene = nullptr;
	}

	void LightShow::Flash(float strength)
	{
		m_Flash = std::max(m_Flash, strength);
	}

	void LightShow::Update(float dt)
	{
		if (m_Lamps.empty())
			return;

		m_Time += dt;

		// The flash fades: a moment of white, then the show again.
		m_Flash = std::max(0.0f, m_Flash - dt / kFlashSeconds);

		for (uint32_t index = 0; index < m_Lamps.size(); ++index)
		{
			Lamp& lamp = m_Lamps[index];
			if (!lamp.Light)
				continue;

			const Sample sample = SampleAt(m_Time, index);

			// The flash washes the colour towards white and lifts the
			// brightness: a line cleared is a line that deserves a flash.
			const glm::vec3 color = glm::mix(sample.Color, glm::vec3(1.0f), m_Flash);
			const float intensity = sample.Intensity * (1.0f + 3.0f * m_Flash);

			auto& component = lamp.Light.GetComponent<Forge::LightComponent>();
			component.Color = m_Enabled ? color : glm::vec3(0.0f);
			component.Intensity = m_Enabled ? intensity : 0.0f;

			lamp.Light.GetComponent<Forge::TransformComponent>().Translation = sample.Position;

			if (lamp.Bulb)
			{
				lamp.Bulb.GetComponent<Forge::TransformComponent>().Translation = sample.Position;

				auto& material = lamp.Bulb.GetComponent<Forge::PBRMaterialComponent>();
				material.Emissive = color;
				material.EmissiveIntensity = m_Enabled ? kBulbEmissive * (1.0f + 2.0f * m_Flash) : 0.0f;
			}
		}
	}

}  // namespace Tetris
