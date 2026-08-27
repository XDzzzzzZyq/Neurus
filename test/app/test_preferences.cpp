/**
 * @file test_preferences.cpp
 * @brief Unit tests for the Preferences persistence (cereal JSON).
 *
 * Covers the ~/.neurus path convention, missing-file behavior (defaults kept,
 * never throws), save/load roundtrip (including the "auto" language marker,
 * which the Application resolves), and parent directory creation. All file
 * I/O is confined to a std::filesystem RAII temp directory.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

#include "app/Preferences.h"

using namespace neurus;

namespace {

/**
 * @brief RAII temporary directory (Qt-free stand-in for QTemporaryDir).
 *
 * Lives under the system temp dir with a unique name and is removed
 * recursively on destruction, so tests never leak files.
 */
class TempDir
{
public:
	TempDir()
		: m_path(std::filesystem::temp_directory_path() /
		         ("neurus_prefs_" + std::to_string(NextId())))
	{
		std::filesystem::create_directories(m_path);
	}

	~TempDir()
	{
		std::error_code ec;
		std::filesystem::remove_all(m_path, ec);
	}

	TempDir(const TempDir&) = delete;
	TempDir& operator=(const TempDir&) = delete;

	/** @brief Absolute path of @p rel (may be nested; any separator style). */
	std::string filePath(const std::string& rel) const
	{
		return (m_path / rel).string();
	}

private:
	/** Unique-ish id: clock tick + counter, collision-proof across runs. */
	static uint64_t NextId()
	{
		const uint64_t tick = static_cast<uint64_t>(
			std::chrono::steady_clock::now().time_since_epoch().count());
		static std::atomic<uint64_t> counter{0};
		return tick + ++counter;
	}

	std::filesystem::path m_path;
};

TEST(PreferencesTest, DefaultPathPointsIntoDotNeurus)
{
	const std::string path = Preferences::DefaultPath();
	EXPECT_FALSE(path.empty());
	EXPECT_NE(path.find(".neurus/preferences.json"), std::string::npos);
}

TEST(PreferencesTest, MissingFileKeepsDefaults)
{
	Preferences prefs;
	prefs.language = "auto";
	prefs.targetFps = 120;

	EXPECT_FALSE(prefs.Load("/nonexistent/path/preferences.json"));

	// A failed load leaves the current values untouched.
	EXPECT_EQ(prefs.language, "auto");
	EXPECT_EQ(prefs.targetFps, 120);
}

TEST(PreferencesTest, SaveLoadRoundtrip)
{
	TempDir dir;
	const std::string path = dir.filePath("preferences.json");

	Preferences prefs;
	prefs.language = "zh_CN";
	prefs.targetFps = 30;
	EXPECT_TRUE(prefs.Save(path));

	Preferences loaded;
	EXPECT_TRUE(loaded.Load(path));
	EXPECT_EQ(loaded.language, "zh_CN");
	EXPECT_EQ(loaded.targetFps, 30);
}

TEST(PreferencesTest, RoundtripPreservesAutoLanguage)
{
	// "auto" is stored/loaded verbatim by the data type; resolving it to a
	// concrete code is the Application's responsibility (it owns I18n), so
	// the round-trip must be lossless.
	TempDir dir;
	const std::string path = dir.filePath("preferences.json");

	Preferences prefs;
	prefs.language = "auto";
	prefs.targetFps = 60;
	ASSERT_TRUE(prefs.Save(path));

	Preferences loaded;
	EXPECT_TRUE(loaded.Load(path));
	EXPECT_EQ(loaded.language, "auto");
	EXPECT_EQ(loaded.targetFps, 60);
}

TEST(PreferencesTest, SaveCreatesParentDirectory)
{
	TempDir dir;
	// A nested path whose parent directories do not exist yet.
	const std::string path = dir.filePath("a/b/c/preferences.json");

	Preferences prefs;
	prefs.language = "en";
	prefs.targetFps = 60;
	EXPECT_TRUE(prefs.Save(path));
	EXPECT_TRUE(std::filesystem::exists(path));
}

} // namespace
