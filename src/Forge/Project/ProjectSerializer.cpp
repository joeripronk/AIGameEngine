#include "fgpch.h"
#include "Forge/Core/Base.h"
#include "Forge/Project/ProjectSerializer.h"

#include "Forge/Core/FileSystem.h"

#include <yaml-cpp/yaml.h>

namespace Forge {

	bool ProjectSerializer::Serialize(const std::filesystem::path& path) const
	{
		if (!m_Project)
		{
			FG_CORE_ERROR("ProjectSerializer: no project to serialize");
			return false;
		}

		const ProjectConfig& config = m_Project->GetConfig();

		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Project" << YAML::Value;
		out << YAML::BeginMap;
		out << YAML::Key << "Name" << YAML::Value << config.Name;
		out << YAML::Key << "StartScene" << YAML::Value << config.StartScene.string();
		out << YAML::Key << "EditorScene" << YAML::Value << config.EditorScene.string();
		out << YAML::Key << "AssetDirectory" << YAML::Value << config.AssetDirectory.string();
		out << YAML::Key << "ScriptDirectory" << YAML::Value << config.ScriptDirectory.string();
		out << YAML::EndMap;
		out << YAML::EndMap;

		// Create the directory first: a project saved into a directory that
		// does not exist yet is a normal thing to ask for, and the failure it
		// produces otherwise ("could not open for writing") names the wrong
		// problem.
		const std::filesystem::path directory = path.parent_path();
		if (!directory.empty())
		{
			std::error_code error;
			std::filesystem::create_directories(directory, error);
			if (error)
			{
				FG_CORE_ERROR("ProjectSerializer: could not create '{0}': {1}", directory.string(), error.message());
				return false;
			}
		}

		if (!FileSystem::WriteTextFile(path, out.c_str()))
		{
			FG_CORE_ERROR("ProjectSerializer: could not write '{0}'", path.string());
			return false;
		}

		return true;
	}

	bool ProjectSerializer::Deserialize(const std::filesystem::path& path)
	{
		if (!m_Project)
		{
			FG_CORE_ERROR("ProjectSerializer: no project to deserialize into");
			return false;
		}

		YAML::Node root;
		try
		{
			root = YAML::LoadFile(path.string());
		}
		catch (const YAML::ParserException& e)
		{
			FG_CORE_ERROR("ProjectSerializer: '{0}' is not valid YAML: {1}", path.string(), e.what());
			return false;
		}
		catch (const YAML::BadFile&)
		{
			FG_CORE_ERROR("ProjectSerializer: could not read '{0}'", path.string());
			return false;
		}

		YAML::Node node = root["Project"];
		if (!node || !node.IsMap())
		{
			FG_CORE_ERROR("ProjectSerializer: '{0}' has no Project map", path.string());
			return false;
		}

		// Every field is optional and falls back to the default, so a project
		// file may name nothing but its name. `as<std::string>()` on an
		// absent key throws, which is why each read is guarded.
		auto readPath = [&node](const char* key, std::filesystem::path& out)
		{
			if (YAML::Node value = node[key]; value && value.IsScalar())
				out = value.as<std::string>();
		};

		ProjectConfig& config = m_Project->GetConfig();
		if (YAML::Node name = node["Name"]; name && name.IsScalar())
			config.Name = name.as<std::string>();
		readPath("StartScene", config.StartScene);
		readPath("EditorScene", config.EditorScene);
		readPath("AssetDirectory", config.AssetDirectory);
		readPath("ScriptDirectory", config.ScriptDirectory);

		// A relative asset directory is the point; an absolute one would make
		// the project unmovable, which is the failure this file format exists
		// to prevent.
		if (config.AssetDirectory.empty())
		{
			FG_CORE_ERROR("ProjectSerializer: '{0}' has an empty AssetDirectory", path.string());
			return false;
		}

		if (config.AssetDirectory.is_absolute())
		{
			FG_CORE_ERROR("ProjectSerializer: '{0}' has an absolute AssetDirectory", path.string());
			return false;
		}

		return true;
	}

}  // namespace Forge