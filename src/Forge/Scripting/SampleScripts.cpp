#include "fgpch.h"
#include "Forge/Scripting/SampleScripts.h"
#include "Forge/Scripting/NativeScriptRegistry.h"

#include "Forge/Scene/Components.h"
#include "Forge/Scene/Entity.h"

namespace Forge {

	void SampleMover::OnCreate()
	{
		// The entity's own transform is where the script writes, and the
		// scene is what it writes into - so the script needs nothing but the
		// entity it was given.
		if (GetEntity())
			GetEntity().GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 0.0f };
	}

	void SampleMover::OnUpdate(Timestep ts)
	{
		if (!GetEntity())
			return;

		auto& transform = GetEntity().GetComponent<TransformComponent>();
		transform.Translation.x += ts.GetSeconds();
	}

	void RegisterEngineNativeScripts()
	{
		static const bool s_Registered = []()
		{
			RegisterNativeScript<SampleMover>("Mover");
			return true;
		}();
		(void)s_Registered;
	}

}  // namespace Forge
