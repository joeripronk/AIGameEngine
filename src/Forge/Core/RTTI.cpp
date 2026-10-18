#include "fgpch.h"
#include "Forge/Core/RTTI.h"

#include <memory>
#include <unordered_map>
#include <utility>

namespace Forge {

	namespace {

		// Entries are heap-allocated so pointers handed out by FindType stay
		// valid while the registry grows: a vector of values hands out
		// pointers into storage that the next registration reallocates.
		struct RegistryState
		{
			std::mutex Mutex;
			std::vector<std::unique_ptr<RuntimeTypeInfo>> Order;
			std::unordered_map<std::string, const RuntimeTypeInfo*> ByName;
			std::unordered_map<uint64_t, const RuntimeTypeInfo*> ByUuid;
			std::unordered_map<std::type_index, const RuntimeTypeInfo*> ByType;
		};

		RegistryState& State()
		{
			// Function-local static: safe against static-init order between
			// translation units, which is exactly the order FG_REGISTER_TYPE
			// registrations arrive in.
			static RegistryState s_State;
			return s_State;
		}

		struct EventBusState
		{
			std::mutex Mutex;
			uint64_t NextListenerID = 1;
			std::vector<std::pair<uint64_t, TypeEventBus::ListenerFn>> Listeners;
		};

		EventBusState& Bus()
		{
			static EventBusState s_Bus;
			return s_Bus;
		}

	}  // namespace

	uint64_t TypeEventBus::RegisterListener(ListenerFn listener)
	{
		std::lock_guard<std::mutex> lock(Bus().Mutex);
		const uint64_t id = Bus().NextListenerID++;
		Bus().Listeners.emplace_back(id, std::move(listener));
		return id;
	}

	void TypeEventBus::UnregisterListener(uint64_t listenerID)
	{
		std::lock_guard<std::mutex> lock(Bus().Mutex);
		auto& listeners = Bus().Listeners;
		listeners.erase(
			std::remove_if(listeners.begin(), listeners.end(),
				[listenerID](const std::pair<uint64_t, ListenerFn>& entry)
				{ return entry.first == listenerID; }),
			listeners.end());
	}

	void TypeEventBus::Publish(const TypeRegisteredEvent& event)
	{
		// The listener list is copied so a listener may register, unregister
		// or publish from inside the callback without touching the container
		// being iterated.
		std::vector<ListenerFn> snapshot;
		{
			std::lock_guard<std::mutex> lock(Bus().Mutex);
			snapshot.reserve(Bus().Listeners.size());
			for (const auto& entry : Bus().Listeners)
				snapshot.push_back(entry.second);
		}

		for (const auto& listener : snapshot)
			listener(event);
	}

	bool TypeRegistry::RegisterType(RuntimeTypeInfo info, const std::type_info* typeInfo)
	{
		RegistryState& state = State();
		const std::string name = info.Name;

		{
			std::lock_guard<std::mutex> lock(state.Mutex);

			if (state.ByName.find(name) != state.ByName.end())
			{
				FG_CORE_WARN("TypeRegistry: type '{0}' is already registered; keeping the original metadata",
					name);
				return false;
			}

			// Keyed by C++ type as well as by name: one C++ type registered
			// under two names would make FindType<T>() and FindType(name)
			// disagree about which entry the type has.
			if (typeInfo && state.ByType.find(std::type_index(*typeInfo)) != state.ByType.end())
			{
				FG_CORE_WARN("TypeRegistry: C++ type of '{0}' is already registered under another name; keeping the original metadata",
					name);
				return false;
			}

			auto entry = std::make_unique<RuntimeTypeInfo>(std::move(info));
			const RuntimeTypeInfo* stored = entry.get();
			state.Order.push_back(std::move(entry));
			state.ByName.emplace(stored->Name, stored);
			state.ByUuid.emplace(static_cast<uint64_t>(stored->Uuid), stored);

			if (typeInfo)
				state.ByType.emplace(std::type_index(*typeInfo), stored);
		}

		// Published after the lock is released: a listener that queries the
		// registry (the common case) must not deadlock on its own event.
		if (const RuntimeTypeInfo* stored = FindType(name))
			TypeEventBus::Publish(TypeRegisteredEvent{ *stored });

		return true;
	}

	const RuntimeTypeInfo* TypeRegistry::FindType(std::string_view name)
	{
		RegistryState& state = State();
		std::lock_guard<std::mutex> lock(state.Mutex);
		const auto it = state.ByName.find(std::string(name));
		return it == state.ByName.end() ? nullptr : it->second;
	}

	const RuntimeTypeInfo* TypeRegistry::FindType(UUID uuid)
	{
		RegistryState& state = State();
		std::lock_guard<std::mutex> lock(state.Mutex);
		const auto it = state.ByUuid.find(static_cast<uint64_t>(uuid));
		return it == state.ByUuid.end() ? nullptr : it->second;
	}

	const RuntimeTypeInfo* TypeRegistry::FindType(std::type_index index)
	{
		RegistryState& state = State();
		std::lock_guard<std::mutex> lock(state.Mutex);
		const auto it = state.ByType.find(index);
		return it == state.ByType.end() ? nullptr : it->second;
	}

	std::vector<RuntimeTypeInfo> TypeRegistry::GetRegisteredTypes()
	{
		RegistryState& state = State();
		std::lock_guard<std::mutex> lock(state.Mutex);
		std::vector<RuntimeTypeInfo> types;
		types.reserve(state.Order.size());
		for (const auto& entry : state.Order)
			types.push_back(*entry);
		return types;
	}

	uint64_t TypeRegistry::GetRegisteredCount()
	{
		RegistryState& state = State();
		std::lock_guard<std::mutex> lock(state.Mutex);
		return state.Order.size();
	}

}  // namespace Forge
