#include <catch2/catch_test_macros.hpp>

#include "Forge/Core/Base.h"
#include "Forge/Assets/AssetRegistry.h"
#include "Forge/Core/FileSystem.h"
#include "Forge/Editor/EditorContext.h"
#include "Forge/Project/GameExporter.h"
#include "Forge/Project/Project.h"
#include "Forge/Project/ProjectSerializer.h"

#include <cstdio>
#include <filesystem>

using Forge::EditorContext;
using Forge::Ref;
using Forge::Project;
using Forge::ProjectConfig;

namespace {

	// The active project is engine state, and a test that leaves one behind
	// changes what the next test resolves. Every test here puts the previous
	// one back.
	struct ActiveProjectGuard
	{
		ActiveProjectGuard()
			: m_Previous(Project::GetActive())
		{
		}

		~ActiveProjectGuard()
		{
			Project::SetActive(m_Previous);
		}

	private:
		Forge::Ref<Project> m_Previous;
	};

	struct ProjectFixture
	{
		ProjectFixture()
		{
			std::error_code ec;
			std::filesystem::remove_all(kRoot, ec);
			std::filesystem::create_directories(kRoot, ec);
		}

		~ProjectFixture()
		{
			std::error_code ec;
			std::filesystem::remove_all(kRoot, ec);
		}

		static const std::filesystem::path kRoot;
		static const std::filesystem::path kProjectFile;
	};

	const std::filesystem::path ProjectFixture::kRoot = "test_project_fixtures";
	const std::filesystem::path ProjectFixture::kProjectFile = ProjectFixture::kRoot / "Demo.forgeproject";

}  // namespace

TEST_CASE("Project configuration survives a save and load", "[project]")
{
	ActiveProjectGuard guard;
	ProjectFixture fixture;

	ProjectConfig config;
	config.Name = "Demo Game";
	config.StartScene = "scenes/level.forge";
	config.EditorScene = "scenes/authoring.forge";
	config.AssetDirectory = "resources";
	config.ScriptDirectory = "scripts";

	Ref<Project> project = Project::New(config);
	REQUIRE(project);
	REQUIRE(Project::Save(project, ProjectFixture::kProjectFile));

	REQUIRE(Project::Load(ProjectFixture::kProjectFile));

	const ProjectConfig& loaded = Project::GetActive()->GetConfig();
	REQUIRE(loaded.Name == "Demo Game");
	REQUIRE(loaded.StartScene == "scenes/level.forge");
	REQUIRE(loaded.EditorScene == "scenes/authoring.forge");
	REQUIRE(loaded.AssetDirectory == "resources");
	REQUIRE(loaded.ScriptDirectory == "scripts");
}

TEST_CASE("A project resolves its assets and scenes", "[project]")
{
	ActiveProjectGuard guard;
	ProjectFixture fixture;

	ProjectConfig config;
	config.StartScene = "scenes/level.forge";
	Project::New(config);
	REQUIRE(Project::Save(Project::GetActive(), ProjectFixture::kProjectFile));

	// Saving establishes where the project lives: paths resolve relative to
	// the project file's directory, so the project can be moved.
	const std::filesystem::path projectDirectory = ProjectFixture::kRoot;
	REQUIRE(Project::GetAssetDirectory() == projectDirectory / "resources");
	REQUIRE(Project::GetStartScenePath() == projectDirectory / "resources" / "scenes" / "level.forge");

	// The editor opens its own scene when it has one...
	Project::GetActive()->GetConfig().EditorScene = "scenes/authoring.forge";
	REQUIRE(Project::GetEditorScenePath() == projectDirectory / "resources" / "scenes" / "authoring.forge");

	// ...and the start scene otherwise, so a project may name only one.
	Project::GetActive()->GetConfig().EditorScene.clear();
	REQUIRE(Project::GetEditorScenePath() == Project::GetStartScenePath());

	// An asset path is relative to the asset directory.
	REQUIRE(Project::GetAssetFileSystemPath("textures/albedo.png")
		== projectDirectory / "resources" / "textures" / "albedo.png");

	// Engine content is written as `resources/...` throughout. Joining that
	// again would resolve to resources/resources/..., which exists nowhere.
	REQUIRE(Project::GetAssetFileSystemPath("resources/textures/albedo.png")
		== projectDirectory / "resources" / "textures" / "albedo.png");

	// The same holds for an asset directory several levels deep: the comparison
	// is component-wise, not on the directory's last name.
	Project::GetActive()->GetConfig().AssetDirectory = "art/game";
	REQUIRE(Project::GetAssetFileSystemPath("art/game/textures/albedo.png")
		== projectDirectory / "art" / "game" / "textures" / "albedo.png");
	REQUIRE(Project::GetAssetFileSystemPath("textures/albedo.png")
		== projectDirectory / "art" / "game" / "textures" / "albedo.png");
	Project::GetActive()->GetConfig().AssetDirectory = "resources";

	// An absolute path is already the answer it gives: re-rooting it would
	// silently point somewhere else.
	const std::filesystem::path absolute = std::filesystem::absolute(projectDirectory / "elsewhere" / "albedo.png");
	REQUIRE(Project::GetAssetFileSystemPath(absolute) == absolute);
}

TEST_CASE("A project with no scene answers with an empty path", "[project]")
{
	ActiveProjectGuard guard;
	ProjectFixture fixture;

	Project::New();
	REQUIRE(Project::Save(Project::GetActive(), ProjectFixture::kProjectFile));

	// "No start scene" is a legitimate state for an unauthored project, and it
	// must not be turned into the project directory or the bare asset root.
	REQUIRE(Project::GetStartScenePath().empty());
	REQUIRE(Project::GetEditorScenePath().empty());
}

TEST_CASE("Loading a project reports failure and keeps the previous one", "[project]")
{
	ActiveProjectGuard guard;
	ProjectFixture fixture;

	ProjectConfig config;
	config.Name = "Good";
	Project::New(config);
	REQUIRE(Project::Save(Project::GetActive(), ProjectFixture::kProjectFile));

	REQUIRE(Project::Load(ProjectFixture::kProjectFile));

	// A path that does not exist...
	REQUIRE(Project::Load(ProjectFixture::kRoot / "Missing.forgeproject") == nullptr);
	REQUIRE(Project::GetActive() != nullptr);
	REQUIRE(Project::GetActive()->GetConfig().Name == "Good");

	// ...a file that is not YAML at all...
	REQUIRE(Forge::FileSystem::WriteTextFile(ProjectFixture::kRoot / "Bad.forgeproject", "Project: [unclosed"));
	REQUIRE(Project::Load(ProjectFixture::kRoot / "Bad.forgeproject") == nullptr);
	REQUIRE(Project::GetActive()->GetConfig().Name == "Good");

	// ...and YAML that is not a project.
	REQUIRE(Forge::FileSystem::WriteTextFile(ProjectFixture::kRoot / "Other.forgeproject", "Scene: Something\n"));
	REQUIRE(Project::Load(ProjectFixture::kRoot / "Other.forgeproject") == nullptr);

	// An absolute asset directory would make the project unmovable, which is
	// the one thing the relative paths exist to prevent.
	REQUIRE(Forge::FileSystem::WriteTextFile(ProjectFixture::kRoot / "Absolute.forgeproject",
		"Project:\n  Name: X\n  AssetDirectory: /etc\n"));
	REQUIRE(Project::Load(ProjectFixture::kRoot / "Absolute.forgeproject") == nullptr);
	REQUIRE(Project::GetActive()->GetConfig().Name == "Good");
}

TEST_CASE("A project file needs only a name", "[project]")
{
	ActiveProjectGuard guard;
	ProjectFixture fixture;

	// Every other field has a default, so a file naming nothing else is still
	// a project - and it keeps those defaults reachable instead of making
	// them dead.
	REQUIRE(Forge::FileSystem::WriteTextFile(ProjectFixture::kProjectFile,
		"Project:\n  Name: Minimal\n"));

	Ref<Project> project = Project::Load(ProjectFixture::kProjectFile);
	REQUIRE(project);

	const ProjectConfig& config = project->GetConfig();
	REQUIRE(config.Name == "Minimal");
	REQUIRE(config.AssetDirectory == "resources");
	REQUIRE(config.StartScene.empty());
}

TEST_CASE("The asset registry scans the project's asset directory", "[project]")
{
	ActiveProjectGuard guard;
	ProjectFixture fixture;

	// A scratch tree standing in for a project's resources.
	std::error_code ec;
	std::filesystem::create_directories(ProjectFixture::kRoot / "art" / "textures", ec);
	REQUIRE(Forge::FileSystem::WriteTextFile(ProjectFixture::kRoot / "art" / "textures" / "albedo.png", "x"));
	REQUIRE(Forge::FileSystem::WriteTextFile(ProjectFixture::kRoot / "art" / "scene.forge", "Scene: X"));

	ProjectConfig config;
	config.AssetDirectory = "art";
	Project::New(config);
	REQUIRE(Project::Save(Project::GetActive(), ProjectFixture::kProjectFile));

	Forge::AssetRegistry::Clear();
	Forge::AssetRegistry::Scan();

	REQUIRE(Forge::AssetRegistry::GetRoot() == (ProjectFixture::kRoot / "art").string());
	REQUIRE(Forge::AssetRegistry::GetAssetsOfType(Forge::AssetType::Texture).size() == 1u);
	REQUIRE(Forge::AssetRegistry::GetAssetsOfType(Forge::AssetType::Scene).size() == 1u);

	Forge::AssetRegistry::Clear();
}

TEST_CASE("An agent can author a project through the editor context", "[project]")
{
	ActiveProjectGuard guard;
	ProjectFixture fixture;

	EditorContext editor;
	REQUIRE(editor.GetProjectConfig() == nullptr);

	Ref<Project> project = editor.NewProject("Agent Game");
	REQUIRE(project);
	REQUIRE(Project::GetActive() == project);
	REQUIRE(editor.GetProjectConfig() != nullptr);
	REQUIRE(editor.GetProjectConfig()->Name == "Agent Game");

	// A brand new project has no directory yet, so its assets resolve against
	// the working directory - which is the same answer engine content written
	// as `resources/...` already gets. Saving the project is what anchors the
	// paths to the project file.
	REQUIRE(editor.ResolveProjectPath("scenes/level.forge") == "resources/scenes/level.forge");

	Project::GetActive()->GetConfig().StartScene = "scenes/level.forge";
	REQUIRE(editor.SaveProject(ProjectFixture::kProjectFile.string()));

	REQUIRE(editor.ResolveProjectPath("scenes/level.forge")
		== (ProjectFixture::kRoot / "resources" / "scenes" / "level.forge").string());
	REQUIRE(editor.ResolveProjectPath("textures/albedo.png")
		== (ProjectFixture::kRoot / "resources" / "textures" / "albedo.png").string());

	// An empty path is not a path to the asset root.
	REQUIRE(editor.ResolveProjectPath("").empty());
}

TEST_CASE("An agent saves and reopens a project", "[project]")
{
	ActiveProjectGuard guard;
	ProjectFixture fixture;

	EditorContext editor;
	editor.NewProject("Round Trip");
	Project::GetActive()->GetConfig().StartScene = "scenes/level.forge";

	REQUIRE(editor.SaveProject(ProjectFixture::kProjectFile.string()));

	Project::SetActive(nullptr);
	REQUIRE(editor.GetProjectConfig() == nullptr);

	REQUIRE(editor.OpenProject(ProjectFixture::kProjectFile.string()));
	REQUIRE(editor.GetProjectConfig() != nullptr);
	REQUIRE(editor.GetProjectConfig()->Name == "Round Trip");
	REQUIRE(Project::GetStartScenePath() == ProjectFixture::kRoot / "resources" / "scenes" / "level.forge");

	// And the resolved path is now anchored to the project.
	REQUIRE(editor.ResolveProjectPath("textures/albedo.png")
		== (ProjectFixture::kRoot / "resources" / "textures" / "albedo.png").string());

	// Saving with no project open is a failure, not a silent no-op.
	Project::SetActive(nullptr);
	REQUIRE_FALSE(editor.SaveProject(ProjectFixture::kProjectFile.string()));
	REQUIRE_FALSE(editor.OpenProject((ProjectFixture::kRoot / "Nope.forgeproject").string()));
}

TEST_CASE("An export takes the scene and resources from the project", "[project]")
{
	ActiveProjectGuard guard;
	ProjectFixture fixture;

	// A scene the project names as its start, and a resource tree beside it.
	std::error_code ec;
	std::filesystem::create_directories(ProjectFixture::kRoot / "resources" / "scenes", ec);
	std::filesystem::create_directories(ProjectFixture::kRoot / "resources" / "scripts", ec);
	REQUIRE(Forge::FileSystem::WriteTextFile(
		ProjectFixture::kRoot / "resources" / "scenes" / "level.forge", "Scene: Level\n"));
	REQUIRE(Forge::FileSystem::WriteTextFile(
		ProjectFixture::kRoot / "resources" / "scripts" / "main.lua", "return {}\n"));

	ProjectConfig config;
	config.StartScene = "scenes/level.forge";
	Project::New(config);
	REQUIRE(Project::Save(Project::GetActive(), ProjectFixture::kProjectFile));

	// No scene and no resource directory given: both come from the project, so
	// the export cannot ship the wrong scene because a caller forgot to
	// repeat what the project already says.
	Forge::GameExporter::Options options;
	options.OutputDirectory = (ProjectFixture::kRoot / "out").string();

	REQUIRE(Forge::GameExporter::Export(options));
	REQUIRE(std::filesystem::exists(ProjectFixture::kRoot / "out" / "level.forge"));
	REQUIRE(std::filesystem::exists(ProjectFixture::kRoot / "out" / "resources" / "scripts" / "main.lua"));

	// A start scene that does not exist is reported, not copied as nothing.
	config.StartScene = "scenes/missing.forge";
	Project::GetActive()->GetConfig().StartScene = config.StartScene;
	Forge::GameExporter::Options broken = options;
	broken.OutputDirectory = (ProjectFixture::kRoot / "out_broken").string();
	REQUIRE_FALSE(Forge::GameExporter::Export(broken));
}