#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include "Forge/Core/UUID.h"
#include "Forge/Core/Timestep.h"
#include "Forge/Core/Time.h"
#include "Forge/Core/Timer.h"
#include "Forge/Core/FileSystem.h"
#include "Forge/Core/Input.h"
#include "Forge/Core/Log.h"
#include "Forge/Core/LayerStack.h"

#include <iterator>
#include <vector>

// The point of the key-code assertions below: Key::W is only GLFW_KEY_W
// because this header says so.
#include <GLFW/glfw3.h>

TEST_CASE("UUID generates unique values", "[core][uuid]")
{
    Forge::UUID uuid1;
    Forge::UUID uuid2;

    REQUIRE(uuid1 != uuid2);
    REQUIRE(uuid1.ToString() != uuid2.ToString());
}

TEST_CASE("UUID from uint64_t", "[core][uuid]")
{
    uint64_t value = 42;
    Forge::UUID uuid(value);

    REQUIRE((uint64_t)uuid == 42);
    REQUIRE(uuid.ToString() == "42");
}

TEST_CASE("UUID default constructor", "[core][uuid]")
{
    Forge::UUID uuid;
    // Default constructor generates a random UUID (non-zero)
    REQUIRE((uint64_t)uuid != 0);
    REQUIRE(uuid.ToString() != "0");
}

TEST_CASE("Timestep creation", "[core][timestep]")
{
    Forge::Timestep ts(0.016f);

    REQUIRE(ts.GetSeconds() == Approx(0.016f));
    REQUIRE(ts.GetMilliseconds() == Approx(16.0f));
}

TEST_CASE("Timestep implicit conversion", "[core][timestep]")
{
    Forge::Timestep ts(1.0f);
    float time = ts;

    REQUIRE(time == Approx(1.0f));
}

TEST_CASE("Log initializes without crashing", "[core][log]")
{
    // Note: Log::Init() should only be called once
    // This test verifies the class is properly structured
    REQUIRE_NOTHROW(Forge::Log::Init());
}

TEST_CASE("LayerStack push and pop", "[core][layerstack]")
{
    Forge::LayerStack stack;

    // Create layers
    auto* layer1 = new Forge::Layer("Layer1");
    auto* layer2 = new Forge::Layer("Layer2");
    auto* overlay = new Forge::Layer("Overlay");

    stack.PushLayer(layer1);
    stack.PushLayer(layer2);
    stack.PushOverlay(overlay);

    REQUIRE(stack.begin() != stack.end());
    REQUIRE(stack.rbegin() != stack.rend());

    // LayerStack owns the layers - no manual delete needed
    (void)layer1; (void)layer2; (void)overlay;
}

TEST_CASE("The frame clock is injectable and defaults to a monotonic source", "[core][time]")
{
    // The default source is the real one: it must move forward on its own.
    const double first = Forge::Time::GetTime();
    const double second = Forge::Time::GetTime();
    REQUIRE(second >= first);

    // A substituted source is what makes a timer measurable at all: a test
    // advances the clock by exactly the amount it means to advance it.
    double fake = 10.0;
    Forge::Time::SetSource([&fake]() { return fake; });
    REQUIRE(Forge::Time::GetTime() == Approx(10.0));

    fake = 12.5;
    REQUIRE(Forge::Time::GetTime() == Approx(12.5));

    // An empty source restores the real clock rather than leaving the engine
    // reading whatever the test installed.
    Forge::Time::SetSource({});
    REQUIRE(Forge::Time::GetTime() >= 0.0);
}

TEST_CASE("Timer measures the engine clock", "[core][time]")
{
    double fake = 100.0;
    Forge::Time::SetSource([&fake]() { return fake; });

    Forge::Timer timer;
    REQUIRE(timer.Elapsed() == Approx(0.0f));

    fake += 0.25;
    REQUIRE(timer.Elapsed() == Approx(0.25f));
    REQUIRE(timer.ElapsedMillis() == Approx(250.0f));

    fake += 0.75;
    REQUIRE(timer.Elapsed() == Approx(1.0f));

    timer.Reset();
    REQUIRE(timer.Elapsed() == Approx(0.0f));

    fake += 0.5;
    REQUIRE(timer.Elapsed() == Approx(0.5f));

    Forge::Time::SetSource({});
}

TEST_CASE("FileSystem reads and writes whole files", "[core][filesystem]")
{
    std::error_code ec;
    std::filesystem::remove_all("test_filesystem", ec);
    std::filesystem::create_directories("test_filesystem", ec);

    const std::filesystem::path binary = "test_filesystem/data.bin";
    const std::vector<uint8_t> bytes = { 0x00, 0x01, 0x7F, 0xFF };
    REQUIRE(Forge::FileSystem::WriteFile(binary, bytes.data(), bytes.size()));

    std::vector<uint8_t> readBack;
    REQUIRE(Forge::FileSystem::ReadFile(binary, readBack));
    REQUIRE(readBack == bytes);

    const std::filesystem::path text = "test_filesystem/script.lua";
    REQUIRE(Forge::FileSystem::WriteTextFile(text, "return {}\n"));

    std::string readText;
    REQUIRE(Forge::FileSystem::ReadTextFile(text, readText));
    REQUIRE(readText == "return {}\n");

    // An empty file is a successful read of nothing. Whether that is
    // acceptable is the caller's decision, so the result says the bytes were
    // transferred and leaves the emptiness verdict alone.
    const std::filesystem::path empty = "test_filesystem/empty.bin";
    REQUIRE(Forge::FileSystem::WriteFile(empty, nullptr, 0));
    std::vector<uint8_t> emptyRead;
    REQUIRE(Forge::FileSystem::ReadFile(empty, emptyRead));
    REQUIRE(emptyRead.empty());

    // A rewrite truncates: a shorter payload must not leave the tail of the
    // previous one behind.
    REQUIRE(Forge::FileSystem::WriteTextFile(text, "x"));
    REQUIRE(Forge::FileSystem::ReadTextFile(text, readText));
    REQUIRE(readText == "x");

    std::filesystem::remove_all("test_filesystem", ec);
}

TEST_CASE("FileSystem reports failures instead of returning empty data", "[core][filesystem]")
{
    std::vector<uint8_t> data{ 1, 2, 3 };
    std::string text = "stale";

    REQUIRE_FALSE(Forge::FileSystem::ReadFile("test_filesystem/does_not_exist.bin", data));
    REQUIRE(data.empty());

    REQUIRE_FALSE(Forge::FileSystem::ReadTextFile("test_filesystem/does_not_exist.lua", text));
    REQUIRE(text.empty());

    // A path whose directory does not exist cannot be written, and the write
    // must say so - a caller that saved a scene needs to learn it did not.
    REQUIRE_FALSE(Forge::FileSystem::WriteTextFile(
        "test_filesystem/no_such_directory/scene.forge", "Scene: X"));

    // Bytes without a buffer is a caller bug, not a successful empty write.
    REQUIRE_FALSE(Forge::FileSystem::WriteFile("test_filesystem/null.bin", nullptr, 8));
}

TEST_CASE("Key and mouse codes are the windowing library's", "[core][input]")
{
    // The enum exists so gameplay code does not name GLFW. That is only safe
    // while the values stay identical, which is what this asserts - one
    // transcription error would otherwise be a key that silently does nothing.
    REQUIRE(Forge::Key::Space == GLFW_KEY_SPACE);
    REQUIRE(Forge::Key::Apostrophe == GLFW_KEY_APOSTROPHE);
    REQUIRE(Forge::Key::Comma == GLFW_KEY_COMMA);
    REQUIRE(Forge::Key::Minus == GLFW_KEY_MINUS);
    REQUIRE(Forge::Key::Period == GLFW_KEY_PERIOD);
    REQUIRE(Forge::Key::Slash == GLFW_KEY_SLASH);
    REQUIRE(Forge::Key::Semicolon == GLFW_KEY_SEMICOLON);
    REQUIRE(Forge::Key::Equal == GLFW_KEY_EQUAL);
    REQUIRE(Forge::Key::LeftBracket == GLFW_KEY_LEFT_BRACKET);
    REQUIRE(Forge::Key::Backslash == GLFW_KEY_BACKSLASH);
    REQUIRE(Forge::Key::RightBracket == GLFW_KEY_RIGHT_BRACKET);
    REQUIRE(Forge::Key::GraveAccent == GLFW_KEY_GRAVE_ACCENT);

    REQUIRE(Forge::Key::D0 == GLFW_KEY_0);
    REQUIRE(Forge::Key::D1 == GLFW_KEY_1);
    REQUIRE(Forge::Key::D9 == GLFW_KEY_9);

    REQUIRE(Forge::Key::A == GLFW_KEY_A);
    REQUIRE(Forge::Key::M == GLFW_KEY_M);
    REQUIRE(Forge::Key::W == GLFW_KEY_W);
    REQUIRE(Forge::Key::Z == GLFW_KEY_Z);

    REQUIRE(Forge::Key::World1 == GLFW_KEY_WORLD_1);
    REQUIRE(Forge::Key::World2 == GLFW_KEY_WORLD_2);

    REQUIRE(Forge::Key::Escape == GLFW_KEY_ESCAPE);
    REQUIRE(Forge::Key::Enter == GLFW_KEY_ENTER);
    REQUIRE(Forge::Key::Tab == GLFW_KEY_TAB);
    REQUIRE(Forge::Key::Backspace == GLFW_KEY_BACKSPACE);
    REQUIRE(Forge::Key::Delete == GLFW_KEY_DELETE);
    REQUIRE(Forge::Key::Right == GLFW_KEY_RIGHT);
    REQUIRE(Forge::Key::Left == GLFW_KEY_LEFT);
    REQUIRE(Forge::Key::Down == GLFW_KEY_DOWN);
    REQUIRE(Forge::Key::Up == GLFW_KEY_UP);
    REQUIRE(Forge::Key::PageUp == GLFW_KEY_PAGE_UP);
    REQUIRE(Forge::Key::PageDown == GLFW_KEY_PAGE_DOWN);
    REQUIRE(Forge::Key::Home == GLFW_KEY_HOME);
    REQUIRE(Forge::Key::End == GLFW_KEY_END);
    REQUIRE(Forge::Key::CapsLock == GLFW_KEY_CAPS_LOCK);
    REQUIRE(Forge::Key::ScrollLock == GLFW_KEY_SCROLL_LOCK);
    REQUIRE(Forge::Key::NumLock == GLFW_KEY_NUM_LOCK);
    REQUIRE(Forge::Key::PrintScreen == GLFW_KEY_PRINT_SCREEN);
    REQUIRE(Forge::Key::Pause == GLFW_KEY_PAUSE);

    REQUIRE(Forge::Key::F1 == GLFW_KEY_F1);
    REQUIRE(Forge::Key::F12 == GLFW_KEY_F12);
    REQUIRE(Forge::Key::F25 == GLFW_KEY_F25);

    REQUIRE(Forge::Key::Kp0 == GLFW_KEY_KP_0);
    REQUIRE(Forge::Key::Kp9 == GLFW_KEY_KP_9);
    REQUIRE(Forge::Key::KpDecimal == GLFW_KEY_KP_DECIMAL);
    REQUIRE(Forge::Key::KpDivide == GLFW_KEY_KP_DIVIDE);
    REQUIRE(Forge::Key::KpMultiply == GLFW_KEY_KP_MULTIPLY);
    REQUIRE(Forge::Key::KpSubtract == GLFW_KEY_KP_SUBTRACT);
    REQUIRE(Forge::Key::KpAdd == GLFW_KEY_KP_ADD);
    REQUIRE(Forge::Key::KpEnter == GLFW_KEY_KP_ENTER);
    REQUIRE(Forge::Key::KpEqual == GLFW_KEY_KP_EQUAL);

    REQUIRE(Forge::Key::LeftShift == GLFW_KEY_LEFT_SHIFT);
    REQUIRE(Forge::Key::LeftControl == GLFW_KEY_LEFT_CONTROL);
    REQUIRE(Forge::Key::LeftAlt == GLFW_KEY_LEFT_ALT);
    REQUIRE(Forge::Key::LeftSuper == GLFW_KEY_LEFT_SUPER);
    REQUIRE(Forge::Key::RightShift == GLFW_KEY_RIGHT_SHIFT);
    REQUIRE(Forge::Key::RightControl == GLFW_KEY_RIGHT_CONTROL);
    REQUIRE(Forge::Key::RightAlt == GLFW_KEY_RIGHT_ALT);
    REQUIRE(Forge::Key::RightSuper == GLFW_KEY_RIGHT_SUPER);
    REQUIRE(Forge::Key::Menu == GLFW_KEY_MENU);

    for (int i = 0; i <= 7; ++i)
        REQUIRE(Forge::Mouse::Button0 + i == GLFW_MOUSE_BUTTON_1 + i);

    REQUIRE(Forge::Mouse::ButtonLast == GLFW_MOUSE_BUTTON_8);
    REQUIRE(Forge::Mouse::ButtonLeft == GLFW_MOUSE_BUTTON_LEFT);
    REQUIRE(Forge::Mouse::ButtonRight == GLFW_MOUSE_BUTTON_RIGHT);
    REQUIRE(Forge::Mouse::ButtonMiddle == GLFW_MOUSE_BUTTON_MIDDLE);
}

TEST_CASE("Input reports nothing when there is no window", "[core][input]")
{
    // Headless: no context has been made current. Every query must answer
    // rather than reading a null window handle.
    REQUIRE_FALSE(Forge::Input::IsKeyPressed(Forge::Key::W));
    REQUIRE_FALSE(Forge::Input::IsKeyReleased(Forge::Key::W));
    REQUIRE_FALSE(Forge::Input::IsMouseButtonPressed(Forge::Mouse::ButtonLeft));
    REQUIRE_FALSE(Forge::Input::IsMouseButtonReleased(Forge::Mouse::ButtonLeft));

    const glm::vec2 position = Forge::Input::GetMousePosition();
    REQUIRE(position.x == 0.0f);
    REQUIRE(position.y == 0.0f);
    REQUIRE(Forge::Input::GetMouseX() == 0.0f);
    REQUIRE(Forge::Input::GetMouseY() == 0.0f);
}

namespace {
    // Records the lifecycle calls the stack is obliged to make.
    class RecordingLayer : public Forge::Layer
    {
    public:
        RecordingLayer(std::string name, int& attaches, int& detaches)
            : Layer(name), m_Attaches(attaches), m_Detaches(detaches) {}

        void OnAttach() override { m_Attaches++; }
        void OnDetach() override { m_Detaches++; }

    private:
        int& m_Attaches;
        int& m_Detaches;
    };
}

TEST_CASE("LayerStack detaches a layer it removes or destroys", "[core][layerstack]")
{
    int attaches = 0, detaches = 0;
    {
        Forge::LayerStack stack;
        stack.PushLayer(new RecordingLayer("A", attaches, detaches));
        stack.PushOverlay(new RecordingLayer("Overlay", attaches, detaches));
        REQUIRE(attaches == 0);  // the stack does not attach; Application does

        // Popping a layer detaches it...
        Forge::LayerStack::iterator first = stack.begin();
        stack.PopLayer(*first);
        REQUIRE(detaches == 1);

        // ...and so does destroying one that was never popped. A layer holding
        // a GPU buffer or a lua reference must be told before it is deleted.
    }
    REQUIRE(detaches == 2);
}

TEST_CASE("LayerStack keeps layers and overlays in separate regions", "[core][layerstack]")
{
    int attaches = 0, detaches = 0;
    Forge::LayerStack stack;

    auto* layer = new RecordingLayer("L", attaches, detaches);
    auto* overlay = new RecordingLayer("O", attaches, detaches);
    stack.PushLayer(layer);
    stack.PushOverlay(overlay);

    REQUIRE(stack.begin() != stack.end());
    REQUIRE(*stack.begin() == layer);

    // PopLayer detaches the layer, and the overlay is untouched - it lives past
    // the insert index, so it is what remains.
    stack.PopLayer(layer);
    REQUIRE(detaches == 1);
    REQUIRE(*stack.begin() == overlay);

    // A layer pushed now lands above the overlay rather than at the end, and
    // popping it leaves the overlay alone: searching the whole stack is what
    // used to let PopLayer remove an overlay and drift the insert index.
    RecordingLayer extra("Insert", attaches, detaches);
    stack.PushLayer(&extra);
    REQUIRE(*stack.begin() == &extra);
    stack.PopLayer(&extra);
    REQUIRE(*stack.begin() == overlay);
    REQUIRE(detaches == 2);

    stack.PopOverlay(overlay);
    REQUIRE(stack.begin() == stack.end());
    REQUIRE(detaches == 3);

    // Popping something that is not there is a no-op, not a crash.
    stack.PopLayer(layer);
    stack.PopOverlay(overlay);
    REQUIRE(detaches == 3);
}

TEST_CASE("LayerStack orders layers before overlays", "[core][layerstack]")
{
    Forge::LayerStack stack;
    auto* first = new Forge::Layer("first");
    auto* second = new Forge::Layer("second");
    auto* overlay = new Forge::Layer("overlay");

    // Layers are inserted above the first overlay, so a later layer lands
    // before an overlay pushed earlier.
    stack.PushOverlay(overlay);
    stack.PushLayer(first);
    stack.PushLayer(second);

    REQUIRE(*stack.begin() == first);
    REQUIRE(*std::next(stack.begin()) == second);
    REQUIRE(*std::next(stack.begin(), 2) == overlay);
}

TEST_CASE("Logging before Init reports nothing instead of crashing", "[core][log]")
{
    // Shutdown puts the engine in the state a static initialiser or a headless
    // tool sees: no loggers at all. Every log macro must be inert there.
    Forge::Log::Shutdown();
    REQUIRE(Forge::Log::GetCoreLogger() == nullptr);
    REQUIRE(Forge::Log::GetClientLogger() == nullptr);

    REQUIRE_NOTHROW(FG_ERROR("no client logger: {0}", 1));
    REQUIRE_NOTHROW(FG_INFO("no client logger"));
    REQUIRE_NOTHROW(FG_WARN("no client logger"));
    REQUIRE_NOTHROW(FG_CORE_ERROR("no core logger"));
    REQUIRE_NOTHROW(FG_CORE_INFO("no core logger"));

    // And logging comes back, because a shutdown that could not be undone
    // would silence the rest of the process.
    Forge::Log::Init();
    REQUIRE(Forge::Log::GetCoreLogger() != nullptr);
    REQUIRE(Forge::Log::GetClientLogger() != nullptr);
    REQUIRE_NOTHROW(FG_ERROR("logging is back"));
}
