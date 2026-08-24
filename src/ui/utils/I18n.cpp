#include "ui/utils/I18n.h"

#include "core/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>

namespace neurus {

namespace
{

/**
 * @brief Unescapes a quoted PO field body (between the quotes).
 * @param line A line such as: msgid "A \"quoted\" string\n"
 * @return The decoded field content.
 */
QString ParseQuotedField(const QString& line)
{
	QString out;
	const int first = line.indexOf(QLatin1Char('"'));
	if (first < 0)
		return out;
	int i = first + 1;
	const int n = line.size();
	while (i < n && line.at(i) != QLatin1Char('"'))
	{
		if (line.at(i) == QLatin1Char('\\') && i + 1 < n)
		{
			switch (line.at(i + 1).unicode())
			{
			case 'n': out += QLatin1Char('\n'); break;
			case 't': out += QLatin1Char('\t'); break;
			case 'r': out += QLatin1Char('\r'); break;
			case '\\': out += QLatin1Char('\\'); break;
			case '"': out += QLatin1Char('"'); break;
			default:  out += line.at(i + 1); break;
			}
			i += 2;
		}
		else
		{
			out += line.at(i);
			++i;
		}
	}
	return out;
}

/**
 * @brief Minimal GNU gettext .po parser (msgctxt/msgid/msgstr + continuations).
 *
 * Stores only fully translated entries: (context, msgid) -> msgstr. Entries
 * with an empty msgstr (untranslated) or the metadata header (msgid "") are
 * skipped — translate() falls back to the English key for them.
 *
 * @param data Raw .po file bytes.
 * @param out  Hash to populate.
 * @param totalCount [out] Total number of msgid entries seen (excl. header).
 * @param translatedCount [out] Entries with a non-empty msgstr.
 */
void ParseCatalog(const QByteArray& data,
                  QHash<QPair<QString, QString>, QString>& out,
                  int& totalCount, int& translatedCount)
{
	enum class Field { None, Context, Msgid, Msgstr };
	QString context, msgid, msgstr;
	Field field = Field::None;

	auto flush = [&]() {
		if (!msgid.isEmpty())
		{
			++totalCount;
			if (!msgstr.isEmpty())
			{
				++translatedCount;
				out.insert({ context, msgid }, msgstr);
			}
		}
		context.clear();
		msgid.clear();
		msgstr.clear();
		field = Field::None;
	};

	const QStringList lines = QString::fromUtf8(data).split(QLatin1Char('\n'));
	for (const QString& rawLine : lines)
	{
		const QString line = rawLine.trimmed();
		if (line.isEmpty())
		{
			flush();
			continue;
		}
		if (line.startsWith(QLatin1Char('#')))
			continue;  // comments (incl. obsolete "#~" entries)

		if (line.startsWith(QLatin1Char('"')))  // continuation of previous field
		{
			const QString value = ParseQuotedField(line);
			if (field == Field::Context)
				context += value;
			else if (field == Field::Msgid)
				msgid += value;
			else if (field == Field::Msgstr)
				msgstr += value;
			continue;
		}

		if (line.startsWith(QLatin1String("msgctxt")))
		{
			field = Field::Context;
			context = ParseQuotedField(line);
		}
		else if (line.startsWith(QLatin1String("msgid")))
		{
			field = Field::Msgid;
			msgid = ParseQuotedField(line);
		}
		else if (line.startsWith(QLatin1String("msgstr")))
		{
			field = Field::Msgstr;
			msgstr = ParseQuotedField(line);
		}
	}
	flush();
}

/**
 * @brief Reads one field out of a catalog's PO header entry.
 * @param data Raw .po file bytes.
 * @param name Field name, e.g. "X-Language-Name".
 * @return The trimmed field value, or an empty string if absent.
 */
QString HeaderField(const QByteArray& data, QLatin1String name)
{
	// The header is the first entry (msgid ""), one "Field: value\n" per line.
	const QStringList lines = QString::fromUtf8(data).split(QLatin1Char('\n'));
	for (const QString& rawLine : lines)
	{
		const QString line = rawLine.trimmed();
		if (line.startsWith(QLatin1String("msgid")) && !line.endsWith(QLatin1String("\"\"")))
			break;  // Past the header entry — give up.
		if (!line.startsWith(QLatin1Char('"')))
			continue;
		const QString field = ParseQuotedField(line);
		if (field.startsWith(name) && field.mid(name.size()).startsWith(QLatin1Char(':')))
			return field.mid(name.size() + 1).trimmed();
	}
	return QString();
}

} // namespace

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
	loadCatalog(c);
	emit languageChanged();
}

QString I18n::translate(const char* key) const
{
	return translateCtx(key, "");
}

QString I18n::translateCtx(const char* key, const char* context) const
{
	if (!key)
		return QString();
	const auto it = m_dict.constFind({ QString::fromUtf8(context ? context : ""),
	                                   QString::fromUtf8(key) });
	if (it != m_dict.constEnd())
		return it.value();
	return QString::fromUtf8(key);  // English fallback.
}

QList<I18n::LanguageInfo> I18n::supportedLanguages()
{
	// English is implicit: the msgids ARE the English strings, so it has no
	// catalog. Every other language is discovered from the embedded catalogs,
	// making a new res/i18n/<code>.po the only edit needed to add a language.
	QList<LanguageInfo> langs{ { QStringLiteral("en"), QStringLiteral("English") } };

	const QFileInfoList files = QDir(QStringLiteral(":/i18n"))
	                                .entryInfoList({ QStringLiteral("*.po") },
	                                               QDir::Files, QDir::Name);
	for (const QFileInfo& info : files)
	{
		const QString code = info.completeBaseName();
		if (code == QLatin1String("en"))
			continue;

		QString name;
		QFile file(info.filePath());
		if (file.open(QIODevice::ReadOnly))
		{
			name = HeaderField(file.readAll(), QLatin1String("X-Language-Name"));
			file.close();
		}
		if (name.isEmpty())
			name = QLocale(code).nativeLanguageName();
		if (name.isEmpty())
			name = code;

		langs.append({ code, name });
	}
	return langs;
}

QString I18n::systemLanguage()
{
	const QLocale sys = QLocale::system();
	if (sys.script() == QLocale::SimplifiedHanScript)
		return QStringLiteral("zh_CN");
	return QStringLiteral("en");
}

void I18n::loadCatalog(const QString& code)
{
	m_dict.clear();
	if (code == QLatin1String("en"))
		return;  // English is the built-in fallback — msgids ARE the strings.

	QFile file(QStringLiteral(":/i18n/%1.po").arg(code));
	if (!file.open(QIODevice::ReadOnly))
	{
		NEURUS_LOG("[I18n] no catalog for language '"
		           << code.toStdString() << "', falling back to English");
		return;
	}
	const QByteArray data = file.readAll();
	file.close();

	int total = 0;
	int translated = 0;
	ParseCatalog(data, m_dict, total, translated);

	if (total == 0)
	{
		NEURUS_ERR("[I18n] catalog :/i18n/" << code.toStdString()
		           << ".po is empty or unparsable, falling back to English");
		return;
	}

	NEURUS_LOG("[I18n] loaded " << translated << "/" << total
	           << " strings for '" << code.toStdString() << "' ("
	           << (total - translated) << " untranslated)");
}

} // namespace neurus
