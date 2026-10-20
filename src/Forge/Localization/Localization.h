#pragma once

#include <map>
#include <string>
#include <vector>

namespace Forge {

	// The translation table (the Godot TranslationServer model).
	//
	// The game's text is looked up by key, and the player's language chooses
	// the table. A key that is not in the table answers with itself: a
	// missing translation shows the text the author wrote, which is a
	// sentence in the author's language rather than an empty space.
	class Localization
	{
	public:
		// Loads the tables from a text file. One table per language:
		//
		//     [en]
		//     greeting = Hello
		//     [fr]
		//     greeting = Bonjour
		//
		// The file is read whole before anything changes: a file that cannot
		// be read leaves the tables exactly as they were, because half a
		// translation table is worse than the one that was there.
		[[nodiscard]] static bool LoadFromFile(const std::string& path);

		// The text for a key in the current language, or the key itself when
		// the table does not have it.
		[[nodiscard]] static std::string Translate(const std::string& key);

		// The language the tables are read from. A language with no table
		// falls back to the key, the same as a missing translation.
		static void SetLanguage(const std::string& language);
		[[nodiscard]] static const std::string& GetLanguage();

		// The languages the tables have.
		[[nodiscard]] static std::vector<std::string> GetLanguages();

		// Forgets every table. A new project starts with none.
		static void Clear();

	private:
		// The tables, by language.
		static std::map<std::string, std::map<std::string, std::string>> s_Tables;
		static std::string s_Language;
	};

}  // namespace Forge
