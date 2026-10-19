#include <catch2/catch_test_macros.hpp>

#include "Forge/Core/FileSystem.h"
#include "Forge/Core/Log.h"
#include "Forge/Editor/EditorContext.h"
#include "Forge/Scene/Components.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Startup-problem integration tests. These run headless and fast, and target
// the failure class that produces a window that never appears: what the
// renderer loads at startup is a fixed list of SPIR-V files resolved from the
// working directory. A missing module is an editor that opens and shows
// nothing; a corrupt one is an editor that dies in device init. Both were
// real incidents in this repo (the bloom shaders shipped unregistered, and
// uncommitted .spv files), so "the file exists and is SPIR-V" is a test.

namespace {

	const std::string kShaderDir = std::string(FORGE_SOURCE_DIR) + "/resources/shaders/";

	// The list Renderer::Init loads, mirrored deliberately: if the renderer
	// gains a shader and this list is not updated, the new shader has no
	// startup test. Keep it in sync with Renderer.cpp.
	struct ShaderLoad
	{
		const char* Name;
		const char* Vertex;
		const char* Fragment;
	};

	const std::vector<ShaderLoad>& RendererShaderLoads()
	{
		static const std::vector<ShaderLoad> loads = {
			{ "PBR", "pbr.vert.spv", "pbr.frag.spv" },
			{ "SSAO", "fullscreen.vert.spv", "ssao.frag.spv" },
			{ "HDR", "fullscreen.vert.spv", "hdr.frag.spv" },
			{ "Final", "fullscreen.vert.spv", "final.frag.spv" },
			{ "Bloom", "fullscreen.vert.spv", "bloom.frag.spv" },
			{ "BloomBlur", "fullscreen.vert.spv", "blur.frag.spv" },
			{ "Shadow", "shadow.vert.spv", "shadow.frag.spv" },
			{ "Irradiance", "fullscreen.vert.spv", "irradiance.frag.spv" },
			{ "Prefilter", "fullscreen.vert.spv", "prefilter.frag.spv" },
		};
		return loads;
	}

	bool IsSpirvModule(const std::string& path)
	{
		std::vector<uint8_t> bytes;
		if (!Forge::FileSystem::ReadFile(path, bytes))
			return false;

		// The SPIR-V binary header: magic, version, generator, bound, reserved.
		if (bytes.size() < 20 || bytes.size() % 4 != 0)
			return false;

		// word(n) reads the header word n - four bytes starting at 4n.
		const auto word = [&](size_t wordIndex) {
			const size_t index = wordIndex * 4;
			return static_cast<uint32_t>(bytes[index]) |
			       (static_cast<uint32_t>(bytes[index + 1]) << 8) |
			       (static_cast<uint32_t>(bytes[index + 2]) << 16) |
			       (static_cast<uint32_t>(bytes[index + 3]) << 24);
		};

		if (word(0) != 0x07230203u)
			return false;

		// Version must be SPIR-V 1.x; a module the loader cannot parse kills
		// device init on every machine, not just this one.
		if (word(1) >> 16 != 1)
			return false;

		return word(4) == 0u;
	}

}

TEST_CASE("Every shader the renderer loads at startup exists as SPIR-V", "[startup][integration]")
{
	Forge::Log::Init();

	// The exact shader list VkRenderer builds its pipelines from, checked
	// as a loader would: readable, right magic, sane header.
	for (const ShaderLoad& load : RendererShaderLoads())
	{
		INFO("shader: " << load.Name);
		REQUIRE(IsSpirvModule(kShaderDir + load.Vertex));
		REQUIRE(IsSpirvModule(kShaderDir + load.Fragment));
	}
}

TEST_CASE("Every GLSL source has a compiled SPIR-V module next to it", "[startup][integration]")
{
	// The renderer loads only .spv; a .frag without its .spv is a shader that
	// silently does not exist at startup. This catches the gap at commit time
	// rather than at launch: run tools/compile_shaders.sh.
	int checked = 0;
	for (const auto& entry : std::filesystem::directory_iterator(kShaderDir))
	{
		const std::string name = entry.path().filename().string();
		if (name.size() > 4 && (name.ends_with(".vert") || name.ends_with(".frag")))
		{
			INFO("source: " << name);
			const std::string spv = kShaderDir + name + ".spv";
			REQUIRE(std::filesystem::exists(spv));
			REQUIRE(IsSpirvModule(spv));
			checked++;
		}
	}

	// The directory is not empty: an empty shader directory means the tests
	// above checked nothing, which is exactly how a startup bug survives.
	REQUIRE(checked >= 10);
}

TEST_CASE("The editor boots an editable scene through the same path the app uses", "[startup][integration]")
{
	// EditorApp::NewScene -> EditorContext::NewScene, the first thing the
	// editor does on launch. It must answer with a scene an agent can edit
	// (the launch state is legitimately empty of entities) and a camera whose
	// projection is real - a zero matrix renders nothing and looks identical
	// to a window that never drew.
	Forge::Log::Init();

	Forge::EditorContext editor;
	editor.NewScene();

	REQUIRE(editor.GetScene() != nullptr);
	REQUIRE(editor.GetViewProjection() != glm::mat4(0.0f));

	editor.CreateEntity("Startup");
	REQUIRE(editor.GetAllEntities().size() == 1);
}

TEST_CASE("The committed test scene opens on the startup path", "[startup][integration]")
{
	// resources/scenes/EngineTest.forge is what the editor and exporter open
	// in tests and demos; a scene file that no longer loads breaks the editor
	// the same way a missing shader does - a window with nothing in it.
	Forge::Log::Init();

	Forge::EditorContext editor;
	REQUIRE(editor.OpenScene(std::string(FORGE_SOURCE_DIR) + "/resources/scenes/EngineTest.forge"));
	REQUIRE(editor.GetAllEntities().size() > 0);
}
