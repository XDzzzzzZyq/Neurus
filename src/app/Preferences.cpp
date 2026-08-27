#include "app/Preferences.h"

#include "core/Log.h"
#include "platform/PlatformPaths.h"

#include <cereal/archives/json.hpp>

#include <filesystem>
#include <fstream>
#include <system_error>

namespace neurus {

std::string Preferences::DefaultPath()
{
	return (HomeDirectory() / ".neurus" / "preferences.json").generic_string();
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

	// Note: "auto" language is intentionally left as-is here. Resolving it to
	// a concrete code is the Application's responsibility (it owns I18n), so
	// this data type stays free of any UI-layer dependency.
	return true;
}

bool Preferences::Save(const std::string& path) const
{
	// Ensure the ~/.neurus/ directory exists before writing.
	const std::filesystem::path parent =
		std::filesystem::path(path).parent_path();
	if (!parent.empty())
	{
		std::error_code ec;
		std::filesystem::create_directories(parent, ec);
		if (ec)
		{
			NEURUS_ERR("[Preferences] failed to create parent directory for "
			           << path << ": " << ec.message());
			return false;
		}
	}

	std::ofstream out(path, std::ios::binary);
	if (!out.is_open())
	{
		NEURUS_ERR("[Preferences] failed to create " << path);
		return false;
	}

	try
	{
		cereal::JSONOutputArchive ar(out);
		ar(cereal::make_nvp("language", language),
		   cereal::make_nvp("target_fps", targetFps));
	}
	catch (const std::exception& e)
	{
		NEURUS_ERR("[Preferences] failed to save " << path << ": " << e.what());
		return false;
	}

	NEURUS_LOG("[Preferences] saved " << path << " (language=" << language
	           << ", target_fps=" << targetFps << ")");
	return true;
}

} // namespace neurus
