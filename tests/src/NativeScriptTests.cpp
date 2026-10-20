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
#include "Forge/Scripting/NativeScriptRegistry.h"
#include "Forge/Scene/SceneSerializer.h"
#include "Forge/Editor/EditorContext.h"

#include <algorithm>
#include <filesystem>

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

	// A second script, so a test can tell two registrations apart.
	class OtherScript : public Forge::ScriptableEntity
	{
	public:
		void OnCreate() override { g_Counters.Creates += 100; }
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

// A native script is named, the way a Lua script is named.
//
// The name is what makes a native script something a scene can hold: a file
// can hold a name, and a name can be looked up. Before this, the component
// carried function pointers, so a scene could not say which script it meant
// and the editor could not show one.

TEST_CASE("A native script is named and the name resolves to its type", "[scripting][native]")
{
	Forge::Log::Init();
	BindFixture reset;

	REQUIRE(Forge::NativeScriptRegistry::Register("NamedScript",
		[]() -> Forge::ScriptableEntity* { return new TestScript(); },
		[](Forge::ScriptableEntity* instance) { delete static_cast<TestScript*>(instance); }));

	Forge::Scene scene;

	auto entity = scene.CreateEntity("Named");
	entity.AddComponent<Forge::NativeScriptComponent>().ClassName = "NamedScript";

	// The name is resolved when the scene starts, and the script is made
	// there - an entity that has a script has it from the first frame.
	REQUIRE_NOTHROW(scene.OnRuntimeStart());
	REQUIRE(g_Counters.Creates == 1);
	REQUIRE(g_Counters.SawValidEntityOnCreate == true);
	REQUIRE(g_Counters.NameOnCreate == "Named");

	REQUIRE_NOTHROW(scene.OnUpdateRuntime(Forge::Timestep(0.016f)));
	REQUIRE(g_Counters.Updates == 1);

	REQUIRE_NOTHROW(scene.OnRuntimeStop());
	REQUIRE(g_Counters.Destroys == 1);
}

TEST_CASE("A native script this binary does not have is inert", "[scripting][native]")
{
	Forge::Log::Init();
	BindFixture reset;
	Forge::Scene scene;

	// The editor and a game are different binaries, so a scene can name a
	// script the editor was not built with. The entity is then inert, and
	// nothing crashes.
	auto entity = scene.CreateEntity("Foreign");
	entity.AddComponent<Forge::NativeScriptComponent>().ClassName = "SomeGamesScript";

	REQUIRE_NOTHROW(scene.OnRuntimeStart());
	REQUIRE_NOTHROW(scene.OnUpdateRuntime(Forge::Timestep(0.016f)));
	REQUIRE(g_Counters.Creates == 0);
	REQUIRE(g_Counters.Updates == 0);

	REQUIRE_NOTHROW(scene.OnRuntimeStop());
	REQUIRE(g_Counters.Destroys == 0);
}

TEST_CASE("A name that is taken keeps the type that answered for it", "[scripting][native]")
{
	Forge::Log::Init();

	REQUIRE(Forge::NativeScriptRegistry::Register("TakenName",
		[]() -> Forge::ScriptableEntity* { return new TestScript(); },
		[](Forge::ScriptableEntity* instance) { delete static_cast<TestScript*>(instance); }));

	// A second type under the same name is refused: two types under one name
	// would make the scene's name ambiguous.
	REQUIRE_FALSE(Forge::NativeScriptRegistry::Register("TakenName",
		[]() -> Forge::ScriptableEntity* { return new OtherScript(); },
		[](Forge::ScriptableEntity* instance) { delete static_cast<OtherScript*>(instance); }));

	// And the original still stands.
	REQUIRE(Forge::NativeScriptRegistry::Find("TakenName") != nullptr);
	REQUIRE(Forge::NativeScriptRegistry::FindDestroy("TakenName") != nullptr);

	// An unknown name is not known, rather than known and null.
	REQUIRE(Forge::NativeScriptRegistry::Find("NeverRegistered") == nullptr);
	REQUIRE(Forge::NativeScriptRegistry::FindDestroy("NeverRegistered") == nullptr);
}

TEST_CASE("The registered names are the ones that can be chosen", "[scripting][native]")
{
	Forge::Log::Init();

	Forge::NativeScriptRegistry::Register("ListedName",
		[]() -> Forge::ScriptableEntity* { return new TestScript(); },
		[](Forge::ScriptableEntity* instance) { delete static_cast<TestScript*>(instance); });

	const auto names = Forge::NativeScriptRegistry::GetNames();
	REQUIRE(std::find(names.begin(), names.end(), std::string("ListedName")) != names.end());
}

TEST_CASE("A native script's name survives a scene round trip", "[scripting][native][serialization]")
{
	Forge::Log::Init();

	std::error_code ec;
	std::filesystem::create_directories("test_native_roundtrip", ec);
	const std::string path = "test_native_roundtrip/native.forge";

	{
		Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();
		auto entity = scene->CreateEntity("Named");
		entity.AddComponent<Forge::NativeScriptComponent>().ClassName = "NamedScript";

		Forge::SceneSerializer serializer(scene);
		REQUIRE(serializer.Serialize(path));
	}

	Forge::Ref<Forge::Scene> reloaded = Forge::CreateRef<Forge::Scene>();
	Forge::SceneSerializer reader(reloaded);
	REQUIRE(reader.Deserialize(path));

	Forge::Entity entity = reloaded->FindEntityByName("Named");
	REQUIRE(entity);
	REQUIRE(entity.HasComponent<Forge::NativeScriptComponent>());
	REQUIRE(entity.GetComponent<Forge::NativeScriptComponent>().ClassName == "NamedScript");

	std::filesystem::remove(path);
}

TEST_CASE("An agent names a native script", "[scripting][native][editor]")
{
	Forge::Log::Init();

	Forge::EditorContext editor;
	auto entity = editor.CreateEntity("Runner");

	REQUIRE(editor.GetNativeScript(entity).empty());
	REQUIRE(editor.SetNativeScript(entity, "NamedScript"));
	REQUIRE(editor.GetNativeScript(entity) == "NamedScript");
	REQUIRE(entity.GetComponent<Forge::NativeScriptComponent>().ClassName == "NamedScript");
}

TEST_CASE("The engine's sample native script runs from a scene", "[scripting][native]")
{
	Forge::Log::Init();

	// The scene's constructor registers the engine's own scripts, the way
	// it registers the component types.
	Forge::Scene scene;

	auto entity = scene.CreateEntity("Mover");
	entity.AddComponent<Forge::NativeScriptComponent>().ClassName = "Mover";

	REQUIRE_NOTHROW(scene.OnRuntimeStart());
	REQUIRE_NOTHROW(scene.OnUpdateRuntime(Forge::Timestep(0.5f)));

	// It walked along +X, the way the Lua Mover does.
	REQUIRE(entity.GetComponent<Forge::TransformComponent>().Translation.x == Approx(0.5f));

	REQUIRE_NOTHROW(scene.OnRuntimeStop());
}

TEST_CASE("The editor's play mode runs a native script", "[scripting][native][editor]")
{
	Forge::Log::Init();

	// The editor's play mode is the scene's runtime, so a native script an
	// author put in a scene runs there exactly as it does in a game.
	Forge::EditorContext editor;
	auto entity = editor.CreateEntity("Mover");
	entity.AddComponent<Forge::NativeScriptComponent>().ClassName = "Mover";

	editor.Play();
	editor.Update(Forge::Timestep(0.5f));

	REQUIRE(entity.GetComponent<Forge::TransformComponent>().Translation.x == Approx(0.5f));

	editor.Stop();
}
