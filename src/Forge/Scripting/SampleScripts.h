#pragma once

#include "Forge/Scripting/ScriptableEntity.h"

namespace Forge {

	// The sample native scripts the engine ships.
	//
	// A native script is C++ inside the binary, so the engine has to hold
	// one for the mechanism to be more than a name in a file. This is the
	// same Mover the sample Lua scripts are - it walks an entity along +X -
	// so an author can watch a native script run in the editor's play mode
	// and compare it with the Lua one beside it.
	class SampleMover : public ScriptableEntity
	{
	public:
		void OnCreate() override;
		void OnUpdate(Timestep ts) override;
	};

	// The native script types the engine ships, under the names a scene uses.
	//
	// Called from Scene's constructor, for the same reason the component
	// types are registered there: a static library's static initialiser is
	// never run, because the linker leaves the object file out when nothing
	// in it is referenced.
	void RegisterEngineNativeScripts();

}
