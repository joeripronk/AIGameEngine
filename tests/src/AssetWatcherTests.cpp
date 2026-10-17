#include <catch2/catch_test_macros.hpp>

#include "Forge/Assets/AssetWatcher.h"
#include "Forge/Renderer/Renderer.h"
#include "Forge/Renderer/Texture.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Forge::AssetWatcher;
using Forge::FileSignature;

namespace {

	std::filesystem::path ScratchDir()
	{
		return "test_hotreload";
	}

	// A fresh scratch tree per test run: the watcher tests assert on
	// Add/Modify/Remove sequences, and leftovers from a previous run would
	// show up as phantom events.
	struct ScratchCleaner
	{
		ScratchCleaner()
		{
			std::error_code ec;
			std::filesystem::remove_all(ScratchDir(), ec);
			std::filesystem::create_directories(ScratchDir(), ec);
		}
		~ScratchCleaner()
		{
			std::error_code ec;
			std::filesystem::remove_all(ScratchDir(), ec);
		}
	};

	void WriteFile(const std::filesystem::path& path, const std::string& text)
	{
		std::ofstream out(path, std::ios::binary);
		out << text;
	}

	// A minimal 24-bit BMP, so the albedo path decodes through stb with a
	// known size and the test can assert on it.
	std::vector<uint8_t> MakeBmp(uint32_t width, uint32_t height, uint8_t shade)
	{
		const uint32_t rowBytes = width * 3;
		const uint32_t rowStride = (rowBytes + 3u) & ~3u;
		const uint32_t pixelBytes = rowStride * height;
		const uint32_t fileSize = 54u + pixelBytes;

		std::vector<uint8_t> out;
		auto put16 = [&](uint16_t v)
		{
			out.push_back(static_cast<uint8_t>(v & 0xFF));
			out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
		};
		auto put32 = [&](uint32_t v)
		{
			out.push_back(static_cast<uint8_t>(v & 0xFF));
			out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
			out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
			out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
		};

		out.push_back('B'); out.push_back('M');
		put32(fileSize);
		put32(0);
		put32(54);
		put32(40);
		put32(width);
		put32(height);
		put16(1);
		put16(24);
		put32(0);
		put32(pixelBytes);
		put32(2835);
		put32(2835);
		put32(0);
		put32(0);

		for (uint32_t y = 0; y < height; ++y)
		{
			for (uint32_t x = 0; x < width; ++x)
			{
				(void)x;
				out.push_back(shade);
				out.push_back(shade);
				out.push_back(shade);
			}
			for (uint32_t pad = rowBytes; pad < rowStride; ++pad)
				out.push_back(0);
		}

		return out;
	}

	void WriteBmp(const std::filesystem::path& path, uint32_t width, uint32_t height)
	{
		const auto bytes = MakeBmp(width, height, 200);
		std::ofstream out(path, std::ios::binary);
		out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}

}  // namespace

TEST_CASE("FileSignature tracks existence, size and timestamp", "[renderer][assets]")
{
	ScratchCleaner cleaner;
	const auto present = ScratchDir() / "sig.txt";
	const auto absent = ScratchDir() / "absent.txt";

	REQUIRE_FALSE(FileSignature::ForFile(absent).Exists);

	// Absent files compare equal, so watching before the file exists works.
	REQUIRE(FileSignature::ForFile(absent) == FileSignature::ForFile(ScratchDir() / "other-absent.txt"));

	WriteFile(present, "abc");
	const FileSignature first = FileSignature::ForFile(present);
	REQUIRE(first.Exists);
	REQUIRE(first.Size == 3u);

	REQUIRE(FileSignature::ForFile(present) == first);

	WriteFile(present, "abcdef");
	const FileSignature second = FileSignature::ForFile(present);
	REQUIRE(second.Exists);
	REQUIRE(second.Size == 6u);
	REQUIRE(second != first);
}

TEST_CASE("FileSignature ignores a rewrite that restores size and timestamp", "[renderer][assets]")
{
	ScratchCleaner cleaner;
	const auto file = ScratchDir() / "stable.txt";

	WriteFile(file, "abc");
	const FileSignature before = FileSignature::ForFile(file);
	const auto stamp = before.LastWrite;

	// Same size and the timestamp wound back: indistinguishable from the
	// recorded file, so no reload is warranted - the reload trigger is the
	// signature, not the write itself.
	WriteFile(file, "xyz");
	std::error_code ec;
	std::filesystem::last_write_time(file, stamp, ec);
	REQUIRE_FALSE(ec);

	REQUIRE(FileSignature::ForFile(file) == before);
}

TEST_CASE("AssetWatcher reports added, modified and removed", "[renderer][assets]")
{
	ScratchCleaner cleaner;
	const auto file = ScratchDir() / "watched.txt";

	AssetWatcher watcher;

	// Watching a file that does not exist yet is valid: its appearance is
	// an event, not a re-arm requirement.
	watcher.Watch(file);
	REQUIRE(watcher.IsWatched(file));
	REQUIRE(watcher.GetWatchCount() == 1u);

	REQUIRE(watcher.Poll().empty());

	WriteFile(file, "one");
	{
		const auto events = watcher.Poll();
		REQUIRE(events.size() == 1u);
		REQUIRE(events[0].Kind == AssetWatcher::Change::Added);
		REQUIRE(events[0].Path == file.string());
	}

	// Polling settled the event; a quiet filesystem reports nothing.
	REQUIRE(watcher.Poll().empty());

	WriteFile(file, "one-two-three");
	{
		const auto events = watcher.Poll();
		REQUIRE(events.size() == 1u);
		REQUIRE(events[0].Kind == AssetWatcher::Change::Modified);
	}

	std::filesystem::remove(file);
	{
		const auto events = watcher.Poll();
		REQUIRE(events.size() == 1u);
		REQUIRE(events[0].Kind == AssetWatcher::Change::Removed);
	}

	// Removed -> added must be visible without re-watching.
	WriteFile(file, "again");
	{
		const auto events = watcher.Poll();
		REQUIRE(events.size() == 1u);
		REQUIRE(events[0].Kind == AssetWatcher::Change::Added);
	}

	watcher.Unwatch(file);
	REQUIRE_FALSE(watcher.IsWatched(file));
	WriteFile(file, "changed-while-unwatched");
	REQUIRE(watcher.Poll().empty());
}

TEST_CASE("AssetWatcher reports every watched file in one poll", "[renderer][assets]")
{
	ScratchCleaner cleaner;
	const auto a = ScratchDir() / "a.txt";
	const auto b = ScratchDir() / "b.txt";

	AssetWatcher watcher;
	watcher.Watch(a);
	watcher.Watch(b);
	REQUIRE(watcher.GetWatchCount() == 2u);

	WriteFile(a, "a");
	WriteFile(b, "ab");

	const auto events = watcher.Poll();
	REQUIRE(events.size() == 2u);
	for (const auto& event : events)
		REQUIRE(event.Kind == AssetWatcher::Change::Added);

	REQUIRE(watcher.Poll().empty());
}

TEST_CASE("Albedo textures hot-reload from disk", "[renderer][assets]")
{
	ScratchCleaner cleaner;
	const auto file = ScratchDir() / "albedo.bmp";
	const std::string path = file.string();

	// The file does not exist yet: the load fails, and the cache must
	// report a degenerate texture (width 0) rather than a null one.
	Forge::Ref<Forge::Texture> missing = Forge::Renderer::GetAlbedoTexture(path);
	REQUIRE(missing);
	REQUIRE(missing->GetWidth() == 0u);

	// The failed load must not be cached forever: as soon as the file
	// appears, the next call returns the decoded texture.
	WriteBmp(file, 2, 3);
	Forge::Ref<Forge::Texture> small = Forge::Renderer::GetAlbedoTexture(path);
	REQUIRE(small);
	REQUIRE(small->GetWidth() == 2u);
	REQUIRE(small->GetHeight() == 3u);

	// An unchanged file returns the identical texture, not a re-decode:
	// the per-frame stat is the cost, and the identity check proves the
	// cache held.
	Forge::Ref<Forge::Texture> same = Forge::Renderer::GetAlbedoTexture(path);
	REQUIRE(same.get() == small.get());

	// Rewriting the file re-decodes: the new dimensions are observable.
	//
	// The rewrite must change the file's *size* as well as its contents: a
	// signature is size plus last-write time, and a filesystem's timestamp
	// granularity is not fine enough to distinguish two writes microseconds
	// apart (both land in the same tick), so a same-size rewrite is
	// indistinguishable from an unchanged file by design - see
	// "FileSignature ignores a rewrite that restores size and timestamp".
	WriteBmp(file, 4, 3);
	Forge::Ref<Forge::Texture> large = Forge::Renderer::GetAlbedoTexture(path);
	REQUIRE(large);
	REQUIRE(large->GetWidth() == 4u);
	REQUIRE(large->GetHeight() == 3u);
	REQUIRE(large.get() != small.get());

	// A reload that fails (the file was deleted) keeps the texture that
	// worked - a broken file must not turn a good texture into a missing
	// one - and must not re-reload every frame.
	std::filesystem::remove(file);
	Forge::Ref<Forge::Texture> kept = Forge::Renderer::GetAlbedoTexture(path);
	REQUIRE(kept.get() == large.get());

	// And the path recovers when the asset comes back.
	WriteBmp(file, 2, 2);
	Forge::Ref<Forge::Texture> back = Forge::Renderer::GetAlbedoTexture(path);
	REQUIRE(back);
	REQUIRE(back->GetWidth() == 2u);
	REQUIRE(back->GetHeight() == 2u);
}

TEST_CASE("AssetWatcher change names are printable", "[renderer][assets]")
{
	REQUIRE(std::string(AssetWatcher::ChangeToString(AssetWatcher::Change::Added)) == "added");
	REQUIRE(std::string(AssetWatcher::ChangeToString(AssetWatcher::Change::Modified)) == "modified");
	REQUIRE(std::string(AssetWatcher::ChangeToString(AssetWatcher::Change::Removed)) == "removed");
}
