#include "ui/utils/I18n.h"

#include "core/Log.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>

namespace neurus {

I18n& I18n::instance()
{
	static I18n s_instance;
	return s_instance;
}

void I18n::setLanguage(const QString& code)
{
	QString c = code.isEmpty() ? QStringLiteral("en") : code;
	// "auto" means "follow the system UI language".
	if (c == QLatin1String("auto"))
		c = systemLanguage();
	if (c == m_language)
		return;
	m_language = c;
	loadDictionary(c);
	emit languageChanged();
}

QString I18n::translate(const char* key) const
{
	if (!key)
		return QString();
	return m_dict.value(QString::fromUtf8(key), QString::fromUtf8(key));
}

QList<I18n::LanguageInfo> I18n::supportedLanguages()
{
	return {
		{ QStringLiteral("en"),    QStringLiteral("English") },
		{ QStringLiteral("zh_CN"), QStringLiteral("简体中文") },
	};
}

QString I18n::systemLanguage()
{
	const QLocale sys = QLocale::system();
	if (sys.script() == QLocale::SimplifiedHanScript)
		return QStringLiteral("zh_CN");
	return QStringLiteral("en");
}

void I18n::loadDictionary(const QString& code)
{
	m_dict.clear();
	if (code == QLatin1String("en"))
		return;  // English is the built-in fallback — keys ARE the strings.

	QFile file(QStringLiteral(":/i18n/%1.json").arg(code));
	if (!file.open(QIODevice::ReadOnly))
	{
		NEURUS_LOG("[I18n] no dictionary for language '"
		           << code.toStdString() << "', falling back to English");
		return;
	}

	QJsonParseError err{};
	const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
	file.close();

	if (err.error != QJsonParseError::NoError || !doc.isObject())
	{
		NEURUS_ERR("[I18n] failed to parse :/i18n/" << code.toStdString()
		           << ".json: " << err.errorString().toStdString());
		return;
	}

	const QJsonObject obj = doc.object();
	for (auto it = obj.begin(); it != obj.end(); ++it)
		m_dict.insert(it.key(), it.value().toString());

	NEURUS_LOG("[I18n] loaded " << m_dict.size() << " strings for '"
	           << code.toStdString() << "'");
}

} // namespace neurus
