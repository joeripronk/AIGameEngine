#pragma once

// The type registry, following the RTTI design Cherno presents in his
// Toy Engine / Workshop series. Note on provenance: the *public* Hazel
// repository does not ship RTTI - every public branch was checked before
// this claim was made - so this follows the documented pattern, not a
// verbatim port.
//
// What it gives the engine: one runtime answer to "what component types
// exist and what are they called", keyed by name, UUID and C++ type, so
// agents, tools and scripts can enumerate and address components by name
// without a hand-maintained list drifting from the component set.
//
// Registration is explicit (the FG_REGISTER_TYPE macro), so a type is in
// the registry only when the author says so. FindType on an unregistered
// type returns nullptr rather than inventing metadata.

#include "Forge/Core/Base.h"
#include "Forge/Core/UUID.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

namespace Forge {

	enum TypeFlags : uint32_t
	{
		TypeFlag_None     = 0,
		TypeFlag_Abstract = 1 << 0,
	};

	struct RuntimeTypeInfo
	{
		std::string Name;
		UUID Uuid;
		uint32_t Size = 0;
		uint32_t Flags = TypeFlag_None;

		// UUIDs of the types this one was registered under as a derivation.
		// The registry records what the author declared; it does not invent
		// a C++ inheritance graph.
		std::vector<UUID> BaseUUIDs;
	};

	// Published by the TypeRegistry after a type has been added, so tools
	// (an asset browser listing component types, an agent watching the set
	// change) observe registrations rather than polling.
	struct TypeRegisteredEvent
	{
		RuntimeTypeInfo Type;
	};

	// Listeners are keyed by a handle so a UI panel can unsubscribe on
	// detach; a listener that outlives the panel that installed it would
	// write into freed state.
	class TypeEventBus final
	{
	public:
		using ListenerFn = std::function<void(const TypeRegisteredEvent&)>;

		static uint64_t RegisterListener(ListenerFn listener);
		static void UnregisterListener(uint64_t listenerID);

		// Not fallible: an event with no listener is normal, not an error.
		static void Publish(const TypeRegisteredEvent& event);
	};

	class TypeRegistry final
	{
	public:
		// Idempotent by name: re-registering a known name keeps the original
		// metadata, logs a warning and reports false - a second registration
		// under the same name means two authors disagree, and silently
		// overwriting hides which one the files reference.
		// Publishes a TypeRegisteredEvent only when the type was added.
		[[nodiscard]]
		static bool RegisterType(RuntimeTypeInfo info, const std::type_info* typeInfo = nullptr);

		[[nodiscard]]
		static const RuntimeTypeInfo* FindType(std::string_view name);

		[[nodiscard]]
		static const RuntimeTypeInfo* FindType(UUID uuid);

		[[nodiscard]]
		static const RuntimeTypeInfo* FindType(std::type_index index);

		// Registration order, for stable enumeration in UI and tools.
		static std::vector<RuntimeTypeInfo> GetRegisteredTypes();

		static uint64_t GetRegisteredCount();
	};

	// Registers T under `name`. `baseTypes` are the UUIDs this type declares
	// itself derived from. Returns false (and logs) when the name was taken.
	// The UUID is derived from the name, so it is the same value in every
	// run - a handle that changes when the program restarts references
	// nothing.
	template<typename T>
	[[nodiscard]]
	bool RegisterType(const std::string& name,
		uint32_t flags = TypeFlag_None,
		const std::vector<UUID>& baseUUIDs = {})
	{
		RuntimeTypeInfo info;
		info.Name = name;
		info.Uuid = UUID{static_cast<uint64_t>(std::hash<std::string>{}(name))};
		info.Size = static_cast<uint32_t>(sizeof(T));
		info.Flags = flags;
		info.BaseUUIDs = baseUUIDs;
		return TypeRegistry::RegisterType(std::move(info), &typeid(T));
	}

	// nullptr for a type that was never registered - the answer is
	// "not known", not "known and zero".
	template<typename T>
	[[nodiscard]]
	const RuntimeTypeInfo* FindType()
	{
		return TypeRegistry::FindType(std::type_index(typeid(T)));
	}

}  // namespace Forge

#define FG_RTTI_TYPE_CONCAT_INNER(a, b) a##b
#define FG_RTTI_TYPE_CONCAT(a, b) FG_RTTI_TYPE_CONCAT_INNER(a, b)

// Registers the named type at static-initialisation time. Place it next to
// the type declaration; the registry internals are function-local statics,
// so static-init order between translation units cannot break it.
#define FG_REGISTER_TYPE(T) \
	struct FG_RTTI_TYPE_CONCAT(T, _AutoRegistration) \
	{ \
		FG_RTTI_TYPE_CONCAT(T, _AutoRegistration)() \
		{ \
			void(Forge::RegisterType<T>(#T)); \
		} \
	}; \
	static FG_RTTI_TYPE_CONCAT(T, _AutoRegistration) FG_RTTI_TYPE_CONCAT(s_, FG_RTTI_TYPE_CONCAT(T, _AutoRegistration))
