#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Forge {

	// Whole-file IO. Every reader in the engine used to open the file itself,
	// which is how four copies of the same read-whole-buffer dance ended up in
	// CpuTexture, the shader loader, the runtime serializer and the script
	// loader - and how they came to disagree about what a failed read means.
	//
	// `bool` follows the engine rule: it is true only if the bytes were
	// actually transferred. A zero-byte file reads successfully and yields an
	// empty buffer; whether an empty file is acceptable is the caller's
	// decision, not this function's, and callers that cannot use an empty
	// buffer say so at their own call site.
	class FileSystem
	{
	public:
		[[nodiscard]]
		static bool ReadFile(const std::filesystem::path& path, std::vector<uint8_t>& outData);

		[[nodiscard]]
		static bool ReadTextFile(const std::filesystem::path& path, std::string& outText);

		[[nodiscard]]
		static bool WriteFile(const std::filesystem::path& path, const void* data, size_t size);

		[[nodiscard]]
		static bool WriteTextFile(const std::filesystem::path& path, const std::string& text);
	};

}  // namespace Forge