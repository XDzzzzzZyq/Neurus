#include "ui/utils/Preferences.h"

#include "core/Log.h"
#include "ui/utils/I18n.h"

#include <QDir>

#include <cereal/archives/json.hpp>

#include <fstream>

namespace neurus {

std::string Preferences::DefaultPath()
{
	return (QDir::homePath() + QStringLiteral("/.neurus/preferences.json"))
		.toStdString();
}

bool Preferences::Load(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open())
		return false;  // Missing file: keep defaults.

	try
	{
		cereal::JSONInputArchive ar(in);
		ar(cereal::make_nvp("language", language),
		   cereal::make_nvp("target_fps", targetFps));
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("[Preferences] failed to load " << path << ": " << e.what());
		language = "auto";
		targetFps = 60;
		return false;
	}

	// "auto" (or an empty value) means "follow the system language".
	if (language.empty() || language == "auto")
		language = I18n::systemLanguage().toStdString();

	return true;
}

bool Preferences::Save(const std::string& path) const
{
	// "auto" never reaches disk: resolve to the detected system language so
	// the persisted file always carries an explicit, loadable code.
	const std::string effectiveLanguage =
		(language.empty() || language == "auto")
			? I18n::systemLanguage().toStdString()
			: language;

	// Ensure the ~/.neurus/ directory exists before writing.
	const std::string::size_type sep = path.find_last_of("/\\");
	if (sep != std::string::npos && sep > 0)
		QDir().mkpath(QString::fromStdString(path.substr(0, sep)));

	std::ofstream out(path, std::ios::binary);
	if (!out.is_open())
	{
		NEURUS_ERR("[Preferences] failed to create " << path);
		return false;
	}

	try
	{
		cereal::JSONOutputArchive ar(out);
		ar(cereal::make_nvp("language", effectiveLanguage),
		   cereal::make_nvp("target_fps", targetFps));
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("[Preferences] failed to save " << path << ": " << e.what());
		return false;
	}

	NEURUS_LOG("[Preferences] saved " << path << " (language=" << effectiveLanguage
	           << ", target_fps=" << targetFps << ")");
	return true;
}

} // namespace neurus
