#include "fgpch.h"
#include "Forge/Core/Base.h"
#include "Forge/Core/FileSystem.h"

#include <fstream>

namespace Forge {

	bool FileSystem::ReadFile(const std::filesystem::path& path, std::vector<uint8_t>& outData)
	{
		outData.clear();

		std::ifstream stream(path, std::ios::binary | std::ios::ate);
		if (!stream.is_open())
		{
			// A file that is not there is a normal answer, not a failure:
			// the pipeline cache before the first run, an optional asset, a
			// layout that was never saved. Anything else - a directory, a
			// permission problem, a broken path - is worth reporting.
			std::error_code ec;
			if (!std::filesystem::exists(path, ec))
			{
				FG_CORE_TRACE("FileSystem: '{0}' does not exist", path.string());
				return false;
			}
			FG_CORE_ERROR("FileSystem: could not open '{0}'", path.string());
			return false;
		}

		const std::streamsize size = stream.tellg();
		if (size < 0)
		{
			FG_CORE_ERROR("FileSystem: could not determine the size of '{0}'", path.string());
			return false;
		}

		stream.seekg(0, std::ios::beg);
		outData.resize(static_cast<size_t>(size));
		if (size > 0 && !stream.read(reinterpret_cast<char*>(outData.data()), size))
		{
			// A short read is a failed read: the caller must never be handed a
			// buffer that is silently shorter than the file it came from.
			FG_CORE_ERROR("FileSystem: short read on '{0}'", path.string());
			outData.clear();
			return false;
		}

		return true;
	}

	bool FileSystem::ReadTextFile(const std::filesystem::path& path, std::string& outText)
	{
		outText.clear();

		std::ifstream stream(path, std::ios::binary | std::ios::ate);
		if (!stream.is_open())
		{
			FG_CORE_ERROR("FileSystem: could not open '{0}'", path.string());
			return false;
		}

		const std::streamsize size = stream.tellg();
		if (size < 0)
		{
			FG_CORE_ERROR("FileSystem: could not determine the size of '{0}'", path.string());
			return false;
		}

		stream.seekg(0, std::ios::beg);
		outText.resize(static_cast<size_t>(size));
		if (size > 0 && !stream.read(outText.data(), size))
		{
			FG_CORE_ERROR("FileSystem: short read on '{0}'", path.string());
			outText.clear();
			return false;
		}

		return true;
	}

	bool FileSystem::WriteFile(const std::filesystem::path& path, const void* data, size_t size)
	{
		if (size > 0 && !data)
		{
			FG_CORE_ERROR("FileSystem: '{0}' was given {1} bytes with no data", path.string(), size);
			return false;
		}

		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		if (!stream.is_open())
		{
			FG_CORE_ERROR("FileSystem: could not open '{0}' for writing", path.string());
			return false;
		}

		if (size > 0)
			stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));

		stream.flush();

		// A write that failed on the way to disk reports success until the
		// stream is checked, which is exactly the failure an agent driving
		// SaveScene would never see.
		if (!stream.good())
		{
			FG_CORE_ERROR("FileSystem: failed to write '{0}'", path.string());
			return false;
		}

		return true;
	}

	bool FileSystem::WriteTextFile(const std::filesystem::path& path, const std::string& text)
	{
		return WriteFile(path, text.data(), text.size());
	}

}  // namespace Forge