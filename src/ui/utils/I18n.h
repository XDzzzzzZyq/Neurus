/**
 * @file I18n.h
 * @brief Lightweight runtime UI translation manager (dictionary-based).
 *
 * Translation keys are the English display strings themselves (the built-in
 * fallback). Optional per-language JSON dictionaries are embedded as Qt
 * resources (:/i18n/<code>.json) and loaded on demand. setLanguage() swaps
 * the active dictionary and emits languageChanged() so every widget can
 * retranslate immediately — no application restart required.
 *
 * Architecture:
 * - UI-layer singleton (QObject), no editor/renderer coupling.
 * - English is implicit: with no dictionary (or a missing key) translate()
 *   returns the key itself, so untranslated strings degrade gracefully.
 * - Values may contain %1/%2 placeholders; callers apply .arg().
 * - UIManager connects to languageChanged() and re-applies menu texts,
 *   dock titles and every panel's Retranslate() hook.
 */

#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

namespace neurus {

class I18n : public QObject
{
	Q_OBJECT

public:
	/** @brief Returns the process-wide translation manager. */
	static I18n& instance();

	/** @brief Active language code ("en", "zh_CN", ...). */
	QString language() const { return m_language; }

	/**
	 * @brief Switches the active language and emits languageChanged().
	 * @param code Language code ("en", "zh_CN"). Empty or unknown codes
	 *             fall back to the built-in English dictionary (keys).
	 */
	void setLanguage(const QString& code);

	/**
	 * @brief Translates @p key for the active language.
	 * @param key English source string (also the dictionary key).
	 * @return The translated string, or @p key itself when no translation
	 *         exists (English fallback).
	 */
	QString translate(const char* key) const;

	/** @brief One supported language: code + native display name. */
	struct LanguageInfo
	{
		QString code;        ///< e.g. "en", "zh_CN"
		QString displayName; ///< native name, e.g. "English", "简体中文"
	};

	/** @brief All languages shipped with the app (extendable). */
	static QList<LanguageInfo> supportedLanguages();

	/**
	 * @brief Detects the system UI language.
	 * @return "zh_CN" on Simplified-Chinese systems, "en" otherwise.
	 */
	static QString systemLanguage();

signals:
	/** @brief Emitted after the active dictionary was swapped. */
	void languageChanged();

private:
	I18n() = default;
	~I18n() override = default;
	I18n(const I18n&) = delete;
	I18n& operator=(const I18n&) = delete;

	/** @brief Loads (and replaces) the dictionary for @p code from resources. */
	void loadDictionary(const QString& code);

	QString m_language = QStringLiteral("en");
	QHash<QString, QString> m_dict;
};

} // namespace neurus
