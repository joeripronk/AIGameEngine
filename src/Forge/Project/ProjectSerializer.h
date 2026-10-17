#pragma once

#include "Forge/Project/Project.h"

namespace Forge {

	// Reads and writes the project file. Separate from Project so the file
	// format lives in one place and Project stays the model (the Hazel
	// ProjectSerializer split).
	class ProjectSerializer
	{
	public:
		explicit ProjectSerializer(Ref<Project> project)
			: m_Project(std::move(project))
		{
		}

		[[nodiscard]]
		bool Serialize(const std::filesystem::path& path) const;

		// Reads `path` into the held project. Every field is optional: a
		// project file that names only its assets is a valid project, and
		// rejecting it would make the file's defaults unreachable.
		[[nodiscard]]
		bool Deserialize(const std::filesystem::path& path);

	private:
		Ref<Project> m_Project;
	};

}  // namespace Forge