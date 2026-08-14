/**
 * @file test_i18n.cpp
 * @brief Unit tests for the I18n runtime translation manager.
 *
 * Covers the English built-in fallback, the zh_CN dictionary lookup,
 * unknown-language fallback, the languageChanged() signal contract, and the
 * supported-language registry. English is restored in SetUp/TearDown so the
 * singleton's state cannot leak between tests.
 */

#include <gtest/gtest.h>

#include <QSignalSpy>
#include <QString>

#include "ui/utils/I18n.h"

using namespace neurus;

namespace {

class I18nTest : public ::testing::Test
{
protected:
	void SetUp() override { I18n::instance().setLanguage("en"); }
	void TearDown() override { I18n::instance().setLanguage("en"); }
};

TEST_F(I18nTest, EnglishIsTheBuiltinFallback)
{
	I18n& i18n = I18n::instance();
	EXPECT_EQ(i18n.language(), QStringLiteral("en"));

	// No dictionary is loaded for English — keys ARE the strings.
	EXPECT_EQ(i18n.translate("&File"), QStringLiteral("&File"));
	EXPECT_EQ(i18n.translate("Anything at all"), QStringLiteral("Anything at all"));
}

TEST_F(I18nTest, ChineseDictionaryTranslatesKnownKeys)
{
	I18n& i18n = I18n::instance();
	i18n.setLanguage("zh_CN");
	EXPECT_EQ(i18n.language(), QStringLiteral("zh_CN"));

	EXPECT_EQ(i18n.translate("&File"), QStringLiteral("文件(&F)"));
	EXPECT_EQ(i18n.translate("Viewport"), QStringLiteral("视口"));

	// Untranslated keys fall back to English verbatim.
	EXPECT_EQ(i18n.translate("Some untranslated key"),
	          QStringLiteral("Some untranslated key"));
}

TEST_F(I18nTest, UnknownLanguageFallsBackToEnglish)
{
	I18n& i18n = I18n::instance();
	i18n.setLanguage("klingon");
	EXPECT_EQ(i18n.language(), QStringLiteral("klingon"));
	EXPECT_EQ(i18n.translate("&File"), QStringLiteral("&File"));
	EXPECT_EQ(i18n.translate("Viewport"), QStringLiteral("Viewport"));
}

TEST_F(I18nTest, LanguageChangedSignalContract)
{
	I18n& i18n = I18n::instance();
	QSignalSpy spy(&i18n, &I18n::languageChanged);

	i18n.setLanguage("zh_CN");
	EXPECT_EQ(spy.count(), 1);

	// Setting the same language again is a no-op — no signal.
	i18n.setLanguage("zh_CN");
	EXPECT_EQ(spy.count(), 1);

	i18n.setLanguage("en");
	EXPECT_EQ(spy.count(), 2);
}

TEST_F(I18nTest, SupportedLanguagesContainEnglishAndChinese)
{
	const auto langs = I18n::supportedLanguages();
	ASSERT_EQ(langs.size(), 2);
	EXPECT_EQ(langs[0].code, QStringLiteral("en"));
	EXPECT_EQ(langs[1].code, QStringLiteral("zh_CN"));
	EXPECT_FALSE(langs[0].displayName.isEmpty());
	EXPECT_FALSE(langs[1].displayName.isEmpty());
}

TEST_F(I18nTest, SystemLanguageIsDetected)
{
	const QString lang = I18n::systemLanguage();
	EXPECT_TRUE(lang == QStringLiteral("en") || lang == QStringLiteral("zh_CN"));
}

TEST_F(I18nTest, AutoLanguageResolvesToSystemLanguage)
{
	// Safety net: "auto" (used by Preferences before the Application resolves
	// it) is treated as "follow the system UI language".
	I18n& i18n = I18n::instance();
	i18n.setLanguage(QStringLiteral("auto"));
	EXPECT_NE(i18n.language(), QStringLiteral("auto"));
	EXPECT_TRUE(i18n.language() == QStringLiteral("en")
	            || i18n.language() == QStringLiteral("zh_CN"));
}

} // namespace
