#include "fgpch.h"
#include "Forge/Scripting/NativeScriptRegistry.h"

#include <map>
#include <mutex>

namespace Forge {

	namespace {

		struct NativeScriptType
		{
			NativeScriptRegistry::InstantiateFn instantiate = nullptr;
			NativeScriptRegistry::DestroyFn destroy = nullptr;
		};

		// The registry is written when a binary starts and read while it
		// runs, so it is guarded: a scene loading on one thread must not see
		// a half-registered type.
		std::mutex& GetMutex()
		{
			static std::mutex mutex;
			return mutex;
		}

		std::map<std::string, NativeScriptType>& GetTypes()
		{
			static std::map<std::string, NativeScriptType> types;
			return types;
		}

		// The registration order, kept beside the map because a map has none
		// of its own and a menu of types should not shuffle between frames.
		std::vector<std::string>& GetOrder()
		{
			static std::vector<std::string> order;
			return order;
		}

	}

	bool NativeScriptRegistry::Register(const std::string& name, InstantiateFn instantiate, DestroyFn destroy)
	{
		if (name.empty() || !instantiate || !destroy)
			return false;

		std::lock_guard<std::mutex> lock(GetMutex());

		if (GetTypes().find(name) != GetTypes().end())
			return false;

		GetTypes()[name] = { instantiate, destroy };
		GetOrder().push_back(name);

		return true;
	}

	NativeScriptRegistry::InstantiateFn NativeScriptRegistry::Find(const std::string& name)
	{
		std::lock_guard<std::mutex> lock(GetMutex());

		auto it = GetTypes().find(name);
		return it != GetTypes().end() ? it->second.instantiate : nullptr;
	}

	NativeScriptRegistry::DestroyFn NativeScriptRegistry::FindDestroy(const std::string& name)
	{
		std::lock_guard<std::mutex> lock(GetMutex());

		auto it = GetTypes().find(name);
		return it != GetTypes().end() ? it->second.destroy : nullptr;
	}

	std::vector<std::string> NativeScriptRegistry::GetNames()
	{
		std::lock_guard<std::mutex> lock(GetMutex());
		return GetOrder();
	}

}
