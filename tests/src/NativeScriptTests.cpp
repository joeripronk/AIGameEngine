// Native scripting: the C++ side of the scripting system.
//
// These matter more than their size suggests. AddComponent<NativeScriptComponent>
// was a link error for want of an add hook, so the documented native scripting
// path could not be used at all; and once it linked, the update loop called a
// null function pointer for an unbound component and handed every instance a
// null entity.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include <string>

#include "Forge/Core/Base.h"
#include "Forge/Core/Log.h"
#include "Forge/Core/Timestep.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scripting/ScriptableEntity.h"

namespace {

	// Counts every lifecycle callback, and records whether the entity was
	// reachable when each one ran.
	struct Counters
	{
		int Creates = 0;
		int Updates = 0;
		int Destroys = 0;
		bool SawValidEntityOnCreate = false;
		std::string NameOnCreate;
		glm::vec3 PositionOnUpdate = { 0.0f, 0.0f, 0.0f };
	};

	Counters g_Counters;

	class TestScript : public Forge::ScriptableEntity
	{
	public:
		void OnCreate() override
		{
			g_Counters.Creates++;
			g_Counters.SawValidEntityOnCreate = false;

			// Reaching for the entity is the whole point of a native script: it
			// is what lets the script read and change its own transform.
			if (GetEntity())
			{
				g_Counters.SawValidEntityOnCreate = true;
				g_Counters.NameOnCreate = GetEntity().GetName();
			}
		}

		void OnUpdate(Forge::Timestep ts) override
		{
			(void)ts;
			g_Counters.Updates++;
			g_Counters.PositionOnUpdate = GetEntity().GetComponent<Forge::TransformComponent>().Translation;
		}

		void OnDestroy() override
		{
			g_Counters.Destroys++;
		}
	};

	struct BindFixture
	{
		BindFixture() { g_Counters = Counters{}; }
	};

} // namespace

TEST_CASE("A native script component can be added", "[scripting][native]")
{
	Forge::Log::Init();
	Forge::Scene scene;

	auto entity = scene.CreateEntity("Native");
	entity.AddComponent<Forge::NativeScriptComponent>();

	// This is the regression: the component had no add hook, so this line was
	// an undefined reference at link time rather than a runtime failure.
	REQUIRE(entity.HasComponent<Forge::NativeScriptComponent>());
}

TEST_CASE("An unbound native script is inert rather than fatal", "[scripting][native]")
{
	Forge::Log::Init();
	BindFixture reset;
	Forge::Scene scene;

	// Default-constructed: Bind<T>() was never called, so InstantiateScript is
	// null. Calling through it would jump to a null function pointer.
	auto entity = scene.CreateEntity("Unbound");
	entity.AddComponent<Forge::NativeScriptComponent>();

	REQUIRE_NOTHROW(scene.OnRuntimeStart());
	REQUIRE_NOTHROW(scene.OnUpdateRuntime(Forge::Timestep(0.016f)));
	REQUIRE_NOTHROW(scene.OnUpdateRuntime(Forge::Timestep(0.016f)));

	// No instance was made, and nothing was destroyed either.
	REQUIRE(g_Counters.Creates == 0);
	REQUIRE(g_Counters.Updates == 0);

	REQUIRE_NOTHROW(scene.OnRuntimeStop());
}

TEST_CASE("A bound native script is created, updated and destroyed", "[scripting][native]")
{
	Forge::Log::Init();
	BindFixture reset;
	Forge::Scene scene;

	auto entity = scene.CreateEntity("Native");
	entity.GetComponent<Forge::TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };
	auto& nsc = entity.AddComponent<Forge::NativeScriptComponent>();
	nsc.Bind<TestScript>();

	REQUIRE_NOTHROW(scene.OnRuntimeStart());

	// Created on the first update, not at add time, and exactly once however
	// many updates follow.
	REQUIRE_NOTHROW(scene.OnUpdateRuntime(Forge::Timestep(0.016f)));
	REQUIRE(g_Counters.Creates == 1);

	REQUIRE_NOTHROW(scene.OnUpdateRuntime(Forge::Timestep(0.016f)));
	REQUIRE(g_Counters.Creates == 1);
	REQUIRE(g_Counters.Updates == 2);

	// The instance can reach its entity, and through it its own transform.
	// Before this was fixed it was handed nullptr, so a native script could not
	// read or move itself and the mechanism did nothing useful.
	REQUIRE(g_Counters.SawValidEntityOnCreate == true);
	REQUIRE(g_Counters.NameOnCreate == "Native");
	REQUIRE(g_Counters.PositionOnUpdate.x == Approx(1.0f));
	REQUIRE(g_Counters.PositionOnUpdate.y == Approx(2.0f));
	REQUIRE(g_Counters.PositionOnUpdate.z == Approx(3.0f));

	// Stopping runs OnDestroy, so the instance does not outlive the session.
	REQUIRE_NOTHROW(scene.OnRuntimeStop());
	REQUIRE(g_Counters.Destroys == 1);
}

TEST_CASE("A restarted runtime rebuilds native instances", "[scripting][native]")
{
	Forge::Log::Init();
	BindFixture reset;
	Forge::Scene scene;

	auto entity = scene.CreateEntity("Native");
	entity.AddComponent<Forge::NativeScriptComponent>().Bind<TestScript>();

	REQUIRE_NOTHROW(scene.OnRuntimeStart());
	REQUIRE_NOTHROW(scene.OnUpdateRuntime(Forge::Timestep(0.016f)));
	REQUIRE(g_Counters.Creates == 1);

	REQUIRE_NOTHROW(scene.OnRuntimeStop());
	REQUIRE(g_Counters.Destroys == 1);

	// The component outlives the stop/start cycle, so a stale Instance pointer
	// would be dereferenced on the second run.
	REQUIRE_NOTHROW(scene.OnRuntimeStart());
	REQUIRE_NOTHROW(scene.OnUpdateRuntime(Forge::Timestep(0.016f)));
	REQUIRE(g_Counters.Creates == 2);

	REQUIRE_NOTHROW(scene.OnUpdateRuntime(Forge::Timestep(0.016f)));
	REQUIRE_NOTHROW(scene.OnRuntimeStop());
	REQUIRE(g_Counters.Destroys == 2);
}

TEST_CASE("Destroying an entity tears down its native script", "[scripting][native]")
{
	Forge::Log::Init();
	BindFixture reset;
	Forge::Scene scene;

	auto entity = scene.CreateEntity("Doomed");
	entity.AddComponent<Forge::NativeScriptComponent>().Bind<TestScript>();

	REQUIRE_NOTHROW(scene.OnRuntimeStart());
	REQUIRE_NOTHROW(scene.OnUpdateRuntime(Forge::Timestep(0.016f)));
	REQUIRE(g_Counters.Creates == 1);

	// OnDestroy has to run while the entity handle is still valid: a script
	// reaching for its entity from OnDestroy would be handed a dangling
	// reference otherwise.
	REQUIRE_NOTHROW(scene.DestroyEntity(entity));
	REQUIRE(g_Counters.Destroys == 1);

	// And the scene keeps simulating without it.
	REQUIRE_NOTHROW(scene.OnUpdateRuntime(Forge::Timestep(0.016f)));

	REQUIRE_NOTHROW(scene.OnRuntimeStop());

	// Stopping must not destroy the same instance a second time.
	REQUIRE(g_Counters.Destroys == 1);
}
