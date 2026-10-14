#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include "Forge/Core/UUID.h"
#include "Forge/Core/Timestep.h"
#include "Forge/Core/Log.h"
#include "Forge/Core/LayerStack.h"

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
