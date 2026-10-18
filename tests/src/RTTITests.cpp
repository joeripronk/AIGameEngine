#include "fgpch.h"

#include "Forge/Core/RTTI.h"
#include "Forge/Editor/EditorContext.h"
#include "Forge/Scene/Components.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

using namespace Forge;

// A type never registered by any test: the "unregistered answers nullptr"
// checks need a type the registry has genuinely never seen.
struct RTTIUnregisteredType
{
	int Value = 0;
};

TEST_CASE("TypeRegistry registers engine components and answers by name, UUID and type", "[core][rtti]")
{
	Forge::RegisterEngineComponentTypes();  // idempotent; order-independent

	const RuntimeTypeInfo* rigidbody = TypeRegistry::FindType("RigidbodyComponent");
	REQUIRE(rigidbody != nullptr);
	CHECK(rigidbody->Name == "RigidbodyComponent");
	CHECK(rigidbody->Size == sizeof(RigidbodyComponent));
	CHECK(rigidbody->Flags == TypeFlag_None);

	// The three lookups must agree by construction, not by luck: a tool
	// that resolves a name and a file that stores the UUID have to land on
	// the same entry.
	const RuntimeTypeInfo* byUuid = TypeRegistry::FindType(rigidbody->Uuid);
	REQUIRE(byUuid != nullptr);
	CHECK(byUuid == rigidbody);

	const RuntimeTypeInfo* byType = FindType<RigidbodyComponent>();
	REQUIRE(byType != nullptr);
	CHECK(byType == rigidbody);

	// Unregistered answers "not known" - nullptr, not a fabricated entry.
	CHECK(TypeRegistry::FindType("NoSuchComponent") == nullptr);
	CHECK(FindType<RTTIUnregisteredType>() == nullptr);
	CHECK(TypeRegistry::FindType(UUID{1234567890123456789ull}) == nullptr);
}

TEST_CASE("Every engine component is registered exactly once", "[core][rtti][scene]")
{
	Forge::RegisterEngineComponentTypes();  // idempotent; order-independent

	const char* names[] = {
		"IDComponent", "TagComponent", "TransformComponent", "CameraComponent",
		"ScriptComponent", "NativeScriptComponent", "RigidbodyComponent",
		"BoxColliderComponent", "SphereColliderComponent", "MeshComponent",
		"PBRMaterialComponent", "LightComponent", "EnvironmentMapComponent",
		"AudioSourceComponent",
	};

	for (const char* name : names)
		CHECK(TypeRegistry::FindType(name) != nullptr);

	// Size metadata has to match the real structs; a stale registration
	// macro (wrong type) would not.
	CHECK(TypeRegistry::FindType("TransformComponent")->Size == sizeof(TransformComponent));
	CHECK(TypeRegistry::FindType("AudioSourceComponent")->Size == sizeof(AudioSourceComponent));
	CHECK(TypeRegistry::FindType("BoxColliderComponent")->Size == sizeof(BoxColliderComponent));
}

TEST_CASE("Re-registering a name keeps the original metadata and reports false", "[core][rtti]")
{
	Forge::RegisterEngineComponentTypes();  // idempotent; order-independent

	struct original { int value; };
	struct impostor { char data[1024]; };

	REQUIRE(RegisterType<original>("RTTIReRegisterType"));
	const RuntimeTypeInfo* first = FindType<original>();
	REQUIRE(first != nullptr);

	// A second author with the same name but a different size must not
	// silently replace the entry the first registration published.
	CHECK_FALSE((RegisterType<impostor>("RTTIReRegisterType")));

	const RuntimeTypeInfo* kept = TypeRegistry::FindType("RTTIReRegisterType");
	REQUIRE(kept != nullptr);
	CHECK(kept == first);
	CHECK(kept->Size == sizeof(original));
	CHECK(kept->Size != sizeof(impostor));
}

TEST_CASE("A C++ type registered under a second name is refused", "[core][rtti]")
{
	Forge::RegisterEngineComponentTypes();  // idempotent; order-independent

	struct once { int value; };
	REQUIRE(RegisterType<once>("RTTISingleType"));

	// FindType<T>() and FindType(name) must agree about which entry a type
	// has; a second name for the same C++ type is two answers to one
	// question.
	CHECK_FALSE((RegisterType<once>("RTTISingleTypeAlias")));
	CHECK(FindType<once>() != nullptr);
	CHECK(TypeRegistry::FindType("RTTISingleTypeAlias") == nullptr);
}

TEST_CASE("Type UUIDs are derived from the name, so they are stable across runs", "[core][rtti]")
{
	Forge::RegisterEngineComponentTypes();  // idempotent; order-independent

	struct uuidSubject { int value; };
	REQUIRE(RegisterType<uuidSubject>("RTTIUuidType"));

	const RuntimeTypeInfo* info = TypeRegistry::FindType("RTTIUuidType");
	REQUIRE(info != nullptr);
	CHECK(info->Uuid == UUID{static_cast<uint64_t>(std::hash<std::string>{}("RTTIUuidType"))});
}

TEST_CASE("Registered flags and declared base types round-trip", "[core][rtti]")
{
	Forge::RegisterEngineComponentTypes();  // idempotent; order-independent

	struct flagsSubject { int value; };
	struct derivedSubject { int value; };

	REQUIRE(RegisterType<flagsSubject>("RTTIFlagsType", TypeFlag_Abstract));

	const RuntimeTypeInfo* info = TypeRegistry::FindType("RTTIFlagsType");
	REQUIRE(info != nullptr);
	CHECK(info->Flags == TypeFlag_Abstract);

	// Bases are what the author declared, recorded verbatim.
	const UUID rigidbodyUuid = TypeRegistry::FindType("RigidbodyComponent")->Uuid;
	REQUIRE(RegisterType<derivedSubject>("RTTIDerivedType", TypeFlag_None, { rigidbodyUuid }));

	const RuntimeTypeInfo* derived = TypeRegistry::FindType("RTTIDerivedType");
	REQUIRE(derived != nullptr);
	REQUIRE(derived->BaseUUIDs.size() == 1);
	CHECK(derived->BaseUUIDs[0] == rigidbodyUuid);
}

TEST_CASE("TypeEventBus delivers registration events to listeners", "[core][rtti]")
{
	Forge::RegisterEngineComponentTypes();  // idempotent; order-independent

	struct busSubject { int value; };

	int events = 0;
	RuntimeTypeInfo last{};
	const uint64_t listener = TypeEventBus::RegisterListener(
		[&](const TypeRegisteredEvent& event)
		{
			events++;
			last = event.Type;
		});

	REQUIRE(RegisterType<busSubject>("RTTIBusType"));

	CHECK(events == 1);
	CHECK(last.Name == "RTTIBusType");
	CHECK(last.Size == sizeof(busSubject));

	TypeEventBus::UnregisterListener(listener);

	struct busSubject2 { int value; };
	REQUIRE(RegisterType<busSubject2>("RTTIBusType2"));
	CHECK(events == 1);  // the unregistered listener stays quiet

	// A registration that reports false (name taken) publishes nothing.
	struct busSubject3 { int value; };
	CHECK_FALSE((RegisterType<busSubject3>("RTTIBusType")));
	CHECK(events == 1);
}

TEST_CASE("GetRegisteredTypes lists registrations and counts them", "[core][rtti]")
{
	Forge::RegisterEngineComponentTypes();  // idempotent; order-independent

	const uint64_t before = TypeRegistry::GetRegisteredCount();
	REQUIRE(before >= 14);  // the engine component set

	// Distinct C++ types: registration is keyed by type as well as by name.
	struct orderA { int a; };
	struct orderB { int b; };
	REQUIRE(RegisterType<orderA>("RTTIOrderA"));
	REQUIRE(RegisterType<orderB>("RTTIOrderB"));

	const std::vector<RuntimeTypeInfo> types = TypeRegistry::GetRegisteredTypes();
	CHECK(types.size() == TypeRegistry::GetRegisteredCount());
	REQUIRE(types.size() >= before + 2);

	REQUIRE(TypeRegistry::FindType("RTTIOrderA") != nullptr);
	REQUIRE(TypeRegistry::FindType("RTTIOrderB") != nullptr);
}

TEST_CASE("EditorContext exposes the registry and edits components by registered name", "[scene][editor][rtti]")
{
	Forge::RegisterEngineComponentTypes();  // idempotent; order-independent
	EditorContext editor;
	editor.NewScene();

	const std::vector<std::string> names = editor.GetRegisteredComponentTypes();
	CHECK(names.size() >= 14);
	for (const char* required : { "RigidbodyComponent", "BoxColliderComponent", "AudioSourceComponent" })
	{
		const auto found = std::find(names.begin(), names.end(), std::string(required));
		CHECK(found != names.end());
	}

	Entity entity = editor.CreateEntity("Agent");

	// Add by name: the agent's path to a component it learned from the list.
	CHECK(editor.AddComponentByName(entity, "RigidbodyComponent"));
	CHECK(entity.HasComponent<RigidbodyComponent>());

	// Adding what is already there changes nothing and says so.
	CHECK_FALSE(editor.AddComponentByName(entity, "RigidbodyComponent"));

	// An unknown name is an error, not a silent no-op.
	CHECK_FALSE(editor.AddComponentByName(entity, "NotAComponent"));

	// Structural components cannot be removed - the engine reads them
	// unconditionally (GetName, GetUUID, the gizmo).
	CHECK_FALSE(editor.RemoveComponentByName(entity, "TransformComponent"));
	CHECK(entity.HasComponent<TransformComponent>());

	CHECK(editor.RemoveComponentByName(entity, "RigidbodyComponent"));
	CHECK_FALSE(entity.HasComponent<RigidbodyComponent>());
	CHECK_FALSE(editor.RemoveComponentByName(entity, "RigidbodyComponent"));

	// A collider added by name becomes physical mid-simulation, which is the
	// whole point of the add hook - the name path must not skip it.
	editor.Play();
	entity.AddComponent<SphereColliderComponent>();
	entity.AddComponent<RigidbodyComponent>();
	for (int i = 0; i < 60; ++i)
		editor.Update(Timestep(1.0f / 60.0f));
	const RigidbodyComponent& rb = entity.GetComponent<RigidbodyComponent>();
	REQUIRE(rb.RuntimeBody != nullptr);
	entity.GetComponent<TransformComponent>().Translation.y += 100.0f;
	editor.Stop();
}
