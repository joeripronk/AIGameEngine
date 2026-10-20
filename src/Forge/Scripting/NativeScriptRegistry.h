#pragma once

#include <string>
#include <vector>

namespace Forge {

	class ScriptableEntity;

	// The native scripts this binary knows by name.
	//
	// A Lua script is a file: a scene names one and the file is found when
	// the scene runs. A native script is compiled in, so a scene can only
	// name one that is in the binary running it - the registry is where a
	// name and a type meet.
	//
	// The registry belongs to the engine; a game registers its own types in
	// it, the way it registers a component type.
	class NativeScriptRegistry
	{
	public:
		using InstantiateFn = ScriptableEntity* (*)();
		using DestroyFn = void (*)(ScriptableEntity*);

		// Registers a type under a name.
		//
		// A name that is taken is refused and the original stays: two types
		// under one name would make the scene's name ambiguous, and the
		// first registration already answered for it.
		static bool Register(const std::string& name, InstantiateFn instantiate, DestroyFn destroy);

		// The type a name stands for, or nullptr when the name is unknown.
		static InstantiateFn Find(const std::string& name);

		// The type's destructor, or nullptr.
		static DestroyFn FindDestroy(const std::string& name);

		// The registered names, in the order they were registered.
		static std::vector<std::string> GetNames();
	};

	// Registers a native script type under a name.
	//
	// A game calls this when it starts, because a registration in a static
	// initialiser of a static library is never run: the linker leaves an
	// object file out when nothing in it is referenced.
	template<typename T>
	bool RegisterNativeScript(const std::string& name)
	{
		return NativeScriptRegistry::Register(name,
			[]() -> ScriptableEntity* { return new T(); },
			[](ScriptableEntity* instance) { delete static_cast<T*>(instance); });
	}

}
