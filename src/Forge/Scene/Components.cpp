#include "fgpch.h"
#include "Forge/Scene/Components.h"

#include "Forge/Core/RTTI.h"

namespace Forge {

	// The component set, in one list. This is the answer to "what components
	// exist" for agents, tools and the event bus - the alternative is a
	// hand-maintained list per consumer, which is how the entity-copy lists
	// drifted before CopyComponentsTo.
	//
	// Structural components are registered too: they are what every entity
	// has, and a tool that enumerates "all component types" gets a complete
	// answer.
	//
	// Deliberately not FG_REGISTER_TYPE static instances: ForgeCore is a
	// static library, an object file with no referenced symbol is never
	// pulled out of it, and static-init registration in such a file simply
	// does not run - the registry would silently be empty. This function is
	// called by Scene's constructor (every consumer of components builds a
	// scene), so the registration is *used* and therefore linked.
	void RegisterEngineComponentTypes()
	{
		static const bool s_Registered = []()
		{
#define FG_REGISTER_ENGINE_COMPONENT(Comp) void(RegisterType<Comp>(#Comp));

			FG_REGISTER_ENGINE_COMPONENT(IDComponent)
			FG_REGISTER_ENGINE_COMPONENT(TagComponent)
			FG_REGISTER_ENGINE_COMPONENT(TransformComponent)
			FG_REGISTER_ENGINE_COMPONENT(CameraComponent)
			FG_REGISTER_ENGINE_COMPONENT(ScriptComponent)
			FG_REGISTER_ENGINE_COMPONENT(NativeScriptComponent)
			FG_REGISTER_ENGINE_COMPONENT(RigidbodyComponent)
			FG_REGISTER_ENGINE_COMPONENT(BoxColliderComponent)
			FG_REGISTER_ENGINE_COMPONENT(SphereColliderComponent)
			FG_REGISTER_ENGINE_COMPONENT(MeshComponent)
			FG_REGISTER_ENGINE_COMPONENT(PBRMaterialComponent)
			FG_REGISTER_ENGINE_COMPONENT(LightComponent)
			FG_REGISTER_ENGINE_COMPONENT(EnvironmentMapComponent)
			FG_REGISTER_ENGINE_COMPONENT(AudioSourceComponent)
			FG_REGISTER_ENGINE_COMPONENT(AnimationComponent)
			FG_REGISTER_ENGINE_COMPONENT(CharacterControllerComponent)

#undef FG_REGISTER_ENGINE_COMPONENT
			return true;
		}();
		(void)s_Registered;
	}

}  // namespace Forge
