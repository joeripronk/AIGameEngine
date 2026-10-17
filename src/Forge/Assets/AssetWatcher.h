#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Forge {

	// Identity of a file's content as far as reloads are concerned: existence,
	// size and last write time. Two signatures differ when a reload is
	// warranted: a new file, a rewritten file, a removed file, and a file that
	// came back after removal.
	//
	// A path that does not exist yields an Exists=false signature; signatures
	// for absent files compare equal, so watching an asset before it is
	// written is valid and reports Added when it appears.
	struct FileSignature
	{
		bool Exists = false;
		uintmax_t Size = 0;
		std::filesystem::file_time_type LastWrite{};

		[[nodiscard]]
		static FileSignature ForFile(const std::filesystem::path& path);

		bool operator==(const FileSignature& other) const
		{
			return Exists == other.Exists && Size == other.Size && LastWrite == other.LastWrite;
		}
		bool operator!=(const FileSignature& other) const { return !(*this == other); }
	};

	// Watches a set of files and reports what happened to them since the last
	// poll (the Hazel AssetManager pattern: nothing is push-based, the editor
	// asks). Polling a watched file costs one filesystem stat.
	//
	// Watch a path that does not exist yet: its first appearance reports
	// Added, so a tool or script writing an asset into a watched directory is
	// noticed without re-arming the watcher.
	class AssetWatcher
	{
	public:
		enum class Change
		{
			Added,
			Modified,
			Removed
		};

		struct Event
		{
			std::string Path;
			Change Kind = Change::Added;
		};

		// Records the file's current signature. Re-watching a watched path
		// re-records it (drops undelivered events for that path).
		void Watch(const std::filesystem::path& path);
		void Unwatch(const std::filesystem::path& path);
		bool IsWatched(const std::filesystem::path& path) const;
		size_t GetWatchCount() const { return m_Signatures.size(); }
		void Clear() { m_Signatures.clear(); }

		// Re-stats every watched path, updates the recorded signatures, and
		// returns the changes found. Empty when nothing changed; polling every
		// frame is therefore cheap in the common case and log-silent.
		std::vector<Event> Poll();

		static const char* ChangeToString(Change change);

	private:
		std::unordered_map<std::string, FileSignature> m_Signatures;
	};

}  // namespace Forge
