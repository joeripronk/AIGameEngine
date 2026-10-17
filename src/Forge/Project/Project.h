#pragma once

#include "Forge/Core/Base.h"

#include <filesystem>
#include <string>

namespace Forge {

	// What a project file says. Every path is relative to the project
	// directory, so a project can be moved or checked out anywhere without
	// editing it - the same reason a `.forge` scene refers to its model as
	// `models/cube.obj` and not as an absolute path.
	struct ProjectConfig
	{
		std::string Name = "Untitled";

		// The scene a distributed runtime opens. Empty means "none", which is
		// a legitimate state for a project that has not been authored yet.
		std::filesystem::path StartScene;

		// The scene the editor opens; falls back to StartScene when empty.
		std::filesystem::path EditorScene;

		// Root of everything the project ships, relative to the project
		// directory. Defaults to `resources`, which is what the engine's
		// asset paths have always assumed.
		std::filesystem::path AssetDirectory = "resources";

		// Where scripts live, relative to the asset directory.
		std::filesystem::path ScriptDirectory = "scripts";
	};

	// A project file on disk plus the active one (the Hazel Project).
	//
	// This is the answer to "where are the assets": until now `resources/`
	// was a literal repeated in every subsystem that loads a file, so a game
	// whose assets lived anywhere else needed the engine edited, and the
	// editor had no notion of a project at all - no place to record the start
	// scene, so reopening the editor could not reopen the work.
	//
	// Hazel asserts when no project is active. This does not: a headless tool,
	// a unit test and a game running a bundled scene all legitimately have no
	// project file open, and aborting a tool over a missing config is the wrong
	// trade. The static helpers log once and return an empty path instead, and
	// HasActive() is there for callers that want to ask first.
	class Project
	{
	public:
		// Creates an unsaved project and makes it active.
		static Ref<Project> New(ProjectConfig config = {});

		// Reads a project file and makes it active. Returns nullptr and leaves
		// the previously active project alone when the file cannot be read -
		// a typo in a path must not silently replace a good project with an
		// empty one.
		static Ref<Project> Load(const std::filesystem::path& path);

		[[nodiscard]]
		static bool Save(const Ref<Project>& project, const std::filesystem::path& path);

		static Ref<Project> GetActive() { return s_ActiveProject; }

		static void SetActive(Ref<Project> project) { s_ActiveProject = std::move(project); }

		static bool HasActive() { return s_ActiveProject != nullptr; }

		ProjectConfig& GetConfig() { return m_Config; }
		const ProjectConfig& GetConfig() const { return m_Config; }

		// Where the project file itself lives. Empty until the project has been
		// saved or loaded.
		const std::filesystem::path& GetProjectDirectory() const { return m_ProjectDirectory; }

		[[nodiscard]]
		static std::filesystem::path GetAssetDirectory();

		// Resolves a project-relative path to something openable: the asset
		// directory for an asset path, or the project directory for anything
		// else.
		[[nodiscard]]
		static std::filesystem::path GetAssetFileSystemPath(const std::filesystem::path& relative);

		// Absolute path of the start scene, empty when there is none.
		[[nodiscard]]
		static std::filesystem::path GetStartScenePath();

		// Absolute path of the scene the editor should open: EditorScene if set,
		// otherwise StartScene.
		[[nodiscard]]
		static std::filesystem::path GetEditorScenePath();

	private:
		ProjectConfig m_Config;
		std::filesystem::path m_ProjectDirectory;

		inline static Ref<Project> s_ActiveProject;
	};

}  // namespace Forge