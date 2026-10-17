#include "fgpch.h"
#include "Forge/Core/Base.h"
#include "Forge/Project/Project.h"
#include "Forge/Project/ProjectSerializer.h"

namespace Forge {

	Ref<Project> Project::New(ProjectConfig config)
	{
		Ref<Project> project = CreateRef<Project>();
		project->m_Config = std::move(config);
		s_ActiveProject = project;
		return project;
	}

	Ref<Project> Project::Load(const std::filesystem::path& path)
	{
		Ref<Project> project = CreateRef<Project>();

		ProjectSerializer serializer(project);
		if (!serializer.Deserialize(path))
			return nullptr;

		project->m_ProjectDirectory = path.parent_path();
		s_ActiveProject = project;
		return project;
	}

	bool Project::Save(const Ref<Project>& project, const std::filesystem::path& path)
	{
		if (!project)
		{
			FG_CORE_ERROR("Project::Save: no project to save");
			return false;
		}

		ProjectSerializer serializer(project);
		if (!serializer.Serialize(path))
			return false;

		// Saving also establishes where the project lives: a caller that has
		// just written the file is entitled to resolve assets relative to it.
		project->m_ProjectDirectory = path.parent_path();
		if (project == s_ActiveProject)
			s_ActiveProject = project;
		return true;
	}

	std::filesystem::path Project::GetAssetDirectory()
	{
		if (!s_ActiveProject)
		{
			// No project is a legitimate state - a headless tool, a unit test,
			// a game running a bundled scene - so this reports nothing and
			// returns an empty path rather than asserting like Hazel's version.
			return {};
		}

		return s_ActiveProject->m_ProjectDirectory / s_ActiveProject->m_Config.AssetDirectory;
	}

	std::filesystem::path Project::GetAssetFileSystemPath(const std::filesystem::path& relative)
	{
		if (!s_ActiveProject)
			return relative;

		const std::filesystem::path& assetDirectory = s_ActiveProject->m_Config.AssetDirectory;
		if (relative.empty())
			return GetAssetDirectory();

		// An absolute path is already answered; re-rooting it would silently
		// point somewhere else.
		if (relative.is_absolute())
			return relative;

		// A path that already begins with the asset directory is not joined
		// twice: engine content is written as `resources/...` throughout, and
		// `resources/resources/...` resolves to nothing. Compared component by
		// component, so a nested asset directory ("assets/game") is recognised
		// in full rather than by its last name.
		const std::filesystem::path root = s_ActiveProject->m_ProjectDirectory;
		const std::filesystem::path normalisedRelative = relative.lexically_normal();
		const std::filesystem::path assetRoot = assetDirectory.lexically_normal();

		if (!assetRoot.empty())
		{
			auto relativeIt = normalisedRelative.begin();
			auto assetIt = assetRoot.begin();
			for (; assetIt != assetRoot.end() && relativeIt != normalisedRelative.end(); ++assetIt, ++relativeIt)
			{
				if (*relativeIt != *assetIt)
					break;
			}

			if (assetIt == assetRoot.end())
				return root / normalisedRelative;
		}

		return GetAssetDirectory() / normalisedRelative;
	}

	std::filesystem::path Project::GetStartScenePath()
	{
		if (!s_ActiveProject || s_ActiveProject->m_Config.StartScene.empty())
			return {};

		return GetAssetFileSystemPath(s_ActiveProject->m_Config.StartScene);
	}

	std::filesystem::path Project::GetEditorScenePath()
	{
		if (!s_ActiveProject)
			return {};

		if (!s_ActiveProject->m_Config.EditorScene.empty())
			return GetAssetFileSystemPath(s_ActiveProject->m_Config.EditorScene);

		return GetStartScenePath();
	}

}  // namespace Forge
