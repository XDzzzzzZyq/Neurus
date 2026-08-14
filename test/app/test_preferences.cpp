/**
 * @file test_preferences.cpp
 * @brief Unit tests for the Preferences persistence (cereal JSON).
 *
 * Covers the ~/.neurus path convention, missing-file behavior (defaults kept,
 * never throws), save/load roundtrip (including the "auto" language marker,
 * which the Application resolves), and parent directory creation. All file
 * I/O is confined to QTemporaryDir.
 */

#include <gtest/gtest.h>

#include <QFile>
#include <QTemporaryDir>

#include "app/Preferences.h"

using namespace neurus;

namespace {

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
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const std::string path = dir.filePath(QStringLiteral("preferences.json"))
	                             .toStdString();

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
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const std::string path = dir.filePath(QStringLiteral("preferences.json"))
	                             .toStdString();

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
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	// A nested path whose parent directories do not exist yet.
	const std::string path = dir.filePath(QStringLiteral("a/b/c/preferences.json"))
	                             .toStdString();

	Preferences prefs;
	prefs.language = "en";
	prefs.targetFps = 60;
	EXPECT_TRUE(prefs.Save(path));
	EXPECT_TRUE(QFile::exists(QString::fromStdString(path)));
}

} // namespace
