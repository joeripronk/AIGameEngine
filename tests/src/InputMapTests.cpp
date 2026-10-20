#include <catch2/catch_test_macros.hpp>

#include "Forge/Core/InputMap.h"
#include "Forge/Core/KeyNames.h"
#include "Forge/Events/KeyEvent.h"
#include "Forge/Events/MouseEvent.h"

using namespace Forge;

namespace {

	// The map is a process-wide service, so every test starts from an empty
	// one: a test that leaves an action behind is a test that decides what
	// the next one sees.
	struct InputMapFixture
	{
		InputMapFixture() { InputMap::Clear(); }
		~InputMapFixture() { InputMap::Clear(); }
	};

	void PressKey(int key)
	{
		InputMap::OnEvent(KeyPressedEvent(key, 0));
	}

	void ReleaseKey(int key)
	{
		InputMap::OnEvent(KeyReleasedEvent(key));
	}

}  // namespace

TEST_CASE("An action is a name, and a binding is what makes it fire", "[core][input]")
{
	InputMapFixture fixture;

	REQUIRE(InputMap::AddAction("move_left"));
	REQUIRE(InputMap::HasAction("move_left"));
	REQUIRE_FALSE(InputMap::HasAction("move_right"));

	// The same name twice is not two actions.
	REQUIRE_FALSE(InputMap::AddAction("move_left"));

	// A binding for an action that is not there is refused: a binding
	// nobody can ask about is a binding that cannot be reached.
	REQUIRE_FALSE(InputMap::AddKey("move_right", Key::D));
	REQUIRE(InputMap::AddKey("move_left", Key::A));

	// The same binding twice would fire twice.
	REQUIRE_FALSE(InputMap::AddKey("move_left", Key::A));
	REQUIRE(InputMap::GetBindings("move_left").size() == 1);

	// Nothing is pressed yet.
	REQUIRE_FALSE(InputMap::IsActionPressed("move_left"));
	REQUIRE_FALSE(InputMap::IsActionJustPressed("move_left"));

	PressKey(Key::A);
	REQUIRE(InputMap::IsActionPressed("move_left"));
	REQUIRE(InputMap::IsActionJustPressed("move_left"));
	REQUIRE(InputMap::GetActionStrength("move_left") == 1.0f);

	// "Just pressed" is the frame's news, and the frame ends.
	InputMap::BeginFrame();
	REQUIRE(InputMap::IsActionPressed("move_left"));
	REQUIRE_FALSE(InputMap::IsActionJustPressed("move_left"));

	ReleaseKey(Key::A);
	REQUIRE_FALSE(InputMap::IsActionPressed("move_left"));
	REQUIRE(InputMap::IsActionJustReleased("move_left"));

	InputMap::BeginFrame();
	REQUIRE_FALSE(InputMap::IsActionJustReleased("move_left"));

	// An action nobody asked about is not an error: it is simply not
	// pressed. A script that asks for an action the game never defined
	// gets false, not a crash.
	REQUIRE_FALSE(InputMap::IsActionPressed("fire"));
	REQUIRE_FALSE(InputMap::IsActionJustPressed("fire"));
	REQUIRE(InputMap::GetActionStrength("fire") == 0.0f);
	REQUIRE(InputMap::GetBindings("fire").empty());
}

TEST_CASE("A repeat is not a new press", "[core][input]")
{
	InputMapFixture fixture;

	REQUIRE(InputMap::AddAction("jump"));
	REQUIRE(InputMap::AddKey("jump", Key::Space));

	PressKey(Key::Space);
	REQUIRE(InputMap::IsActionJustPressed("jump"));

	// The frame ends and the next one begins: the key is still held, but
	// the press is not news any more.
	InputMap::BeginFrame();
	REQUIRE_FALSE(InputMap::IsActionJustPressed("jump"));

	// The operating system's auto-repeat sends the key again. The action
	// is already down, and a repeat must not make it news again: an action
	// that fires on the repeat fires many times for one press.
	InputMap::OnEvent(KeyPressedEvent(Key::Space, 1));
	REQUIRE(InputMap::IsActionPressed("jump"));
	REQUIRE_FALSE(InputMap::IsActionJustPressed("jump"));
}

TEST_CASE("Two keys on one action: either of them fires it", "[core][input]")
{
	InputMapFixture fixture;

	REQUIRE(InputMap::AddAction("move_left"));
	REQUIRE(InputMap::AddKey("move_left", Key::A));
	REQUIRE(InputMap::AddKey("move_left", Key::Left));

	PressKey(Key::Left);
	REQUIRE(InputMap::IsActionPressed("move_left"));

	ReleaseKey(Key::Left);
	PressKey(Key::A);
	REQUIRE(InputMap::IsActionPressed("move_left"));
}

TEST_CASE("A binding's modifiers must be held", "[core][input]")
{
	InputMapFixture fixture;

	REQUIRE(InputMap::AddAction("sprint"));
	REQUIRE(InputMap::AddKey("sprint", Key::LeftShift,
		static_cast<uint8_t>(InputModifier::Shift)));

	// The binding asks for shift, so it does not fire without it.
	PressKey(Key::W);
	REQUIRE_FALSE(InputMap::IsActionPressed("sprint"));

	PressKey(Key::LeftShift);
	REQUIRE(InputMap::IsActionPressed("sprint"));

	// Shift alone is not the action either - the action is shift *and* W.
	ReleaseKey(Key::LeftShift);
	REQUIRE_FALSE(InputMap::IsActionPressed("sprint"));
}

TEST_CASE("A binding that asks for no modifier fires whatever else is held", "[core][input]")
{
	InputMapFixture fixture;

	REQUIRE(InputMap::AddAction("move_left"));
	REQUIRE(InputMap::AddKey("move_left", Key::A));

	// Holding shift must not stop the movement key from working: the
	// binding asks for no modifier, and the ones that are held are none of
	// its business.
	PressKey(Key::LeftShift);
	PressKey(Key::A);
	REQUIRE(InputMap::IsActionPressed("move_left"));

	// And the action is not released by a modifier that comes down while
	// the key is still held.
	PressKey(Key::RightShift);
	REQUIRE(InputMap::IsActionPressed("move_left"));
	ReleaseKey(Key::RightShift);
	REQUIRE(InputMap::IsActionPressed("move_left"));

	ReleaseKey(Key::A);
	REQUIRE_FALSE(InputMap::IsActionPressed("move_left"));
}

TEST_CASE("A binding that asks for control fires with shift on top of it", "[core][input]")
{
	InputMapFixture fixture;

	REQUIRE(InputMap::AddAction("save"));
	REQUIRE(InputMap::AddKey("save", Key::S, static_cast<uint8_t>(InputModifier::Control)));

	// Control alone is not enough: the binding asks for control *and* S.
	PressKey(Key::LeftControl);
	REQUIRE_FALSE(InputMap::IsActionPressed("save"));

	PressKey(Key::S);
	REQUIRE(InputMap::IsActionPressed("save"));

	// Shift on top of control does not stop it: the binding asks for
	// control, and control is held.
	PressKey(Key::LeftShift);
	REQUIRE(InputMap::IsActionPressed("save"));
}

TEST_CASE("Two actions make an axis", "[core][input]")
{
	InputMapFixture fixture;

	REQUIRE(InputMap::AddAction("move_left"));
	REQUIRE(InputMap::AddAction("move_right"));
	REQUIRE(InputMap::AddKey("move_left", Key::A));
	REQUIRE(InputMap::AddKey("move_right", Key::D));

	REQUIRE(InputMap::GetAxis("move_left", "move_right") == 0.0f);

	PressKey(Key::D);
	REQUIRE(InputMap::GetAxis("move_left", "move_right") == 1.0f);

	PressKey(Key::A);
	REQUIRE(InputMap::GetAxis("move_left", "move_right") == 0.0f);

	ReleaseKey(Key::A);
	REQUIRE(InputMap::GetAxis("move_left", "move_right") == 1.0f);
}

TEST_CASE("A mouse button can be an action's binding", "[core][input]")
{
	InputMapFixture fixture;

	REQUIRE(InputMap::AddAction("fire"));
	REQUIRE(InputMap::AddMouseButton("fire", Mouse::ButtonLeft));

	InputMap::OnEvent(MouseButtonPressedEvent(Mouse::ButtonLeft));
	REQUIRE(InputMap::IsActionPressed("fire"));
	REQUIRE(InputMap::IsActionJustPressed("fire"));

	InputMap::OnEvent(MouseButtonReleasedEvent(Mouse::ButtonLeft));
	REQUIRE_FALSE(InputMap::IsActionPressed("fire"));
	REQUIRE(InputMap::IsActionJustReleased("fire"));
}

TEST_CASE("The actions keep the order they were added in", "[core][input]")
{
	InputMapFixture fixture;

	REQUIRE(InputMap::AddAction("jump"));
	REQUIRE(InputMap::AddAction("fire"));
	REQUIRE(InputMap::AddAction("crouch"));

	const std::vector<std::string> actions = InputMap::GetActions();
	REQUIRE(actions.size() == 3);
	REQUIRE(actions[0] == "jump");
	REQUIRE(actions[1] == "fire");
	REQUIRE(actions[2] == "crouch");

	// A removed action is gone from the list as well.
	REQUIRE(InputMap::RemoveAction("fire"));
	REQUIRE_FALSE(InputMap::RemoveAction("fire"));
	REQUIRE(InputMap::GetActions().size() == 2);
	REQUIRE(InputMap::GetActions()[1] == "crouch");
}

TEST_CASE("A binding can be removed", "[core][input]")
{
	InputMapFixture fixture;

	REQUIRE(InputMap::AddAction("jump"));
	REQUIRE(InputMap::AddKey("jump", Key::Space));

	PressKey(Key::Space);
	REQUIRE(InputMap::IsActionPressed("jump"));

	REQUIRE(InputMap::RemoveBinding("jump", Key::Space));
	REQUIRE(InputMap::GetBindings("jump").empty());
	REQUIRE_FALSE(InputMap::IsActionPressed("jump"));

	// Removing a binding that is not there is false, not a crash.
	REQUIRE_FALSE(InputMap::RemoveBinding("jump", Key::Space));
	REQUIRE_FALSE(InputMap::RemoveBinding("fire", Key::Space));
}

TEST_CASE("The map survives a round trip through a file", "[core][input]")
{
	InputMapFixture fixture;

	const std::string path = "test_inputmap.txt";

	REQUIRE(InputMap::AddAction("move_left"));
	REQUIRE(InputMap::AddKey("move_left", Key::A));
	REQUIRE(InputMap::AddKey("move_left", Key::Left));
	REQUIRE(InputMap::AddAction("jump"));
	REQUIRE(InputMap::AddKey("jump", Key::Space));
	REQUIRE(InputMap::AddAction("fire"));
	REQUIRE(InputMap::AddMouseButton("fire", Mouse::ButtonLeft));
	REQUIRE(InputMap::AddAction("zoom"));
	REQUIRE(InputMap::AddKey("zoom", Key::Equal,
		static_cast<uint8_t>(InputModifier::Control)));

	REQUIRE(InputMap::SaveToFile(path));

	InputMap::Clear();
	REQUIRE_FALSE(InputMap::HasAction("jump"));

	REQUIRE(InputMap::LoadFromFile(path));

	REQUIRE(InputMap::HasAction("move_left"));
	REQUIRE(InputMap::HasAction("jump"));
	REQUIRE(InputMap::HasAction("fire"));
	REQUIRE(InputMap::HasAction("zoom"));
	REQUIRE(InputMap::GetBindings("move_left").size() == 2);

	// The modifiers survive the round trip: a binding that needs control
	// and one that does not are not the same binding.
	REQUIRE(InputMap::GetBindings("zoom").size() == 1);
	REQUIRE(InputMap::GetBindings("zoom")[0].modifiers ==
		static_cast<uint8_t>(InputModifier::Control));

	// And the bindings still fire.
	PressKey(Key::Space);
	REQUIRE(InputMap::IsActionPressed("jump"));
	PressKey(Key::Equal);
	REQUIRE_FALSE(InputMap::IsActionPressed("zoom"));

	std::remove(path.c_str());
}

TEST_CASE("A file that cannot be read leaves the map alone", "[core][input]")
{
	InputMapFixture fixture;

	REQUIRE(InputMap::AddAction("jump"));
	REQUIRE(InputMap::AddKey("jump", Key::Space));

	REQUIRE_FALSE(InputMap::LoadFromFile("test_inputmap_does_not_exist.txt"));

	// Half a map is worse than the one that was there.
	REQUIRE(InputMap::HasAction("jump"));
	REQUIRE(InputMap::GetBindings("jump").size() == 1);
}

TEST_CASE("The key names round trip", "[core][input]")
{
	KeyCode key = 0;
	REQUIRE(KeyFromName("A", key));
	REQUIRE(key == Key::A);
	REQUIRE(KeyFromName("Left", key));
	REQUIRE(key == Key::Left);
	REQUIRE(KeyFromName("Space", key));
	REQUIRE(key == Key::Space);

	// A name that is not a key is not a key.
	REQUIRE_FALSE(KeyFromName("NotAKey", key));

	// The name a code answers with is the name it was found under.
	REQUIRE(std::string(KeyName(Key::Left)) == "Left");
	REQUIRE(KeyName(12345) == nullptr);

	MouseCode button = 0;
	REQUIRE(MouseButtonFromName("MouseLeft", button));
	REQUIRE(button == Mouse::ButtonLeft);
	REQUIRE_FALSE(MouseButtonFromName("NotAButton", button));
	REQUIRE(std::string(MouseButtonName(Mouse::ButtonRight)) == "MouseRight");
}
