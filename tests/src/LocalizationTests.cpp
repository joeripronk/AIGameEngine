#include <catch2/catch_test_macros.hpp>

#include "Forge/Localization/Localization.h"

#include <cstdio>
#include <fstream>

using namespace Forge;

namespace {

	// A table file with two languages.
	const char* WriteTable()
	{
		const char* path = "test_localization.txt";
		std::ofstream out(path);
		out << "# The game's text\n";
		out << "[en]\n";
		out << "greeting = Hello\n";
		out << "play = Play\n";
		out << "\n";
		out << "[fr]\n";
		out << "greeting = Bonjour\n";
		out << "play = Jouer\n";
		out.close();
		return path;
	}

}  // namespace

TEST_CASE("A translation is looked up by key", "[core][localization]")
{
	Localization::Clear();

	const char* path = WriteTable();
	REQUIRE(Localization::LoadFromFile(path));

	Localization::SetLanguage("en");
	REQUIRE(Localization::Translate("greeting") == "Hello");
	REQUIRE(Localization::Translate("play") == "Play");

	// And in another language.
	Localization::SetLanguage("fr");
	REQUIRE(Localization::Translate("greeting") == "Bonjour");
	REQUIRE(Localization::Translate("play") == "Jouer");

	// The languages the tables have.
	REQUIRE(Localization::GetLanguages().size() == 2);

	Localization::Clear();
	std::remove(path);
}

TEST_CASE("A key that is not translated answers with itself", "[core][localization]")
{
	Localization::Clear();

	const char* path = WriteTable();
	REQUIRE(Localization::LoadFromFile(path));

	Localization::SetLanguage("en");

	// A missing translation shows the text the author wrote, rather than an
	// empty space.
	REQUIRE(Localization::Translate("not_in_the_table") == "not_in_the_table");

	// And so does a language with no table at all.
	Localization::SetLanguage("de");
	REQUIRE(Localization::Translate("greeting") == "greeting");

	Localization::Clear();
	std::remove(path);
}

TEST_CASE("A file that cannot be read leaves the tables alone", "[core][localization]")
{
	Localization::Clear();

	const char* path = WriteTable();
	REQUIRE(Localization::LoadFromFile(path));
	Localization::SetLanguage("en");

	REQUIRE_FALSE(Localization::LoadFromFile("test_localization_does_not_exist.txt"));

	// The table that was there is still there.
	REQUIRE(Localization::Translate("greeting") == "Hello");

	Localization::Clear();
	std::remove(path);
}
