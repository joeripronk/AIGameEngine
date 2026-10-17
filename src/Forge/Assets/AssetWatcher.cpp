#include "fgpch.h"
#include "Forge/Assets/AssetWatcher.h"

#include "Forge/Core/Log.h"

namespace Forge {

	FileSignature FileSignature::ForFile(const std::filesystem::path& path)
	{
		FileSignature signature;

		std::error_code ec;
		if (!std::filesystem::is_regular_file(path, ec))
			return signature;

		signature.Exists = true;
		signature.Size = std::filesystem::file_size(path, ec);
		if (ec)
			signature.Size = 0;

		signature.LastWrite = std::filesystem::last_write_time(path, ec);
		if (ec)
			signature.LastWrite = std::filesystem::file_time_type{};

		return signature;
	}

	void AssetWatcher::Watch(const std::filesystem::path& path)
	{
		m_Signatures[path.string()] = FileSignature::ForFile(path);
	}

	void AssetWatcher::Unwatch(const std::filesystem::path& path)
	{
		m_Signatures.erase(path.string());
	}

	bool AssetWatcher::IsWatched(const std::filesystem::path& path) const
	{
		return m_Signatures.contains(path.string());
	}

	std::vector<AssetWatcher::Event> AssetWatcher::Poll()
	{
		std::vector<Event> events;

		for (auto& [path, recorded] : m_Signatures)
		{
			const FileSignature current = FileSignature::ForFile(path);
			if (current == recorded)
				continue;

			if (current.Exists && !recorded.Exists)
				events.push_back({ path, Change::Added });
			else if (!current.Exists && recorded.Exists)
				events.push_back({ path, Change::Removed });
			else if (current.Exists && recorded.Exists)
				events.push_back({ path, Change::Modified });
			// Absent -> absent: nothing happened.

			recorded = current;
		}

		return events;
	}

	const char* AssetWatcher::ChangeToString(Change change)
	{
		switch (change)
		{
		case Change::Added: return "added";
		case Change::Modified: return "modified";
		case Change::Removed: return "removed";
		}
		return "unknown";
	}

}  // namespace Forge
