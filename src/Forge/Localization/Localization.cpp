#include "fgpch.h"
#include "Forge/Localization/Localization.h"

#include "Forge/Core/FileSystem.h"

#include <sstream>

namespace Forge {

	std::map<std::string, std::map<std::string, std::string>> Localization::s_Tables;
	std::string Localization::s_Language = "en";

	namespace {

		std::string Trim(const std::string& text)
		{
			const size_t first = text.find_first_not_of(" \t\r\n");
			if (first == std::string::npos)
				return {};
			const size_t last = text.find_last_not_of(" \t\r\n");
			return text.substr(first, last - first + 1);
		}

	}  // namespace

	bool Localization::LoadFromFile(const std::string& path)
	{
		std::string text;
		if (!FileSystem::ReadTextFile(path, text))
			return false;

		// The file is read whole before anything changes: a file that cannot
		// be parsed leaves the tables exactly as they were, because half a
		// translation table is worse than the one that was there.
		std::map<std::string, std::map<std::string, std::string>> tables;

		std::istringstream in(text);
		std::string line;
		std::string language;

		while (std::getline(in, line))
		{
			const std::string trimmed = Trim(line);
			if (trimmed.empty() || trimmed[0] == '#')
				continue;

			// A table starts with its name in brackets.
			if (trimmed.front() == '[' && trimmed.back() == ']')
			{
				language = trimmed.substr(1, trimmed.size() - 2);
				tables[language];  // the table exists, even if it is empty
				continue;
			}

			const size_t equals = trimmed.find('=');
			if (equals == std::string::npos || language.empty())
				continue;

			const std::string key = Trim(trimmed.substr(0, equals));
			const std::string value = Trim(trimmed.substr(equals + 1));
			if (key.empty())
				continue;

			tables[language][key] = value;
		}

		if (tables.empty())
			return false;

		for (auto& [name, table] : tables)
			s_Tables[name] = std::move(table);

		return true;
	}

	std::string Localization::Translate(const std::string& key)
	{
		const auto language = s_Tables.find(s_Language);
		if (language != s_Tables.end())
		{
			const auto entry = language->second.find(key);
			if (entry != language->second.end())
				return entry->second;
		}

		// A missing translation shows the key: the text the author wrote,
		// in the language they wrote it in, rather than an empty space.
		return key;
	}

	void Localization::SetLanguage(const std::string& language)
	{
		s_Language = language;
	}

	const std::string& Localization::GetLanguage()
	{
		return s_Language;
	}

	std::vector<std::string> Localization::GetLanguages()
	{
		std::vector<std::string> languages;
		languages.reserve(s_Tables.size());
		for (const auto& [name, table] : s_Tables)
			languages.push_back(name);
		return languages;
	}

	void Localization::Clear()
	{
		s_Tables.clear();
		s_Language = "en";
	}

}  // namespace Forge
