/**
 * @file Project.cpp
 * @brief Project layer implementation — persistence via cereal JSON archives.
 */

#include "Project.h"

#include <cereal/archives/json.hpp>

#include <fstream>
#include <stdexcept>

namespace neurus::project
{

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

Project::Project()
	: m_scene(std::make_unique<Scene>())
{
}

// ---------------------------------------------------------------------------
// Factory methods
// ---------------------------------------------------------------------------

Project Project::New()
{
	return {};
}

Project Project::Open(const std::string& path)
{
	std::ifstream is(path);
	if (!is.is_open())
	{
		throw std::runtime_error("Failed to open project file: " + path);
	}

	cereal::JSONInputArchive archive(is);
	Project project{};
	archive(cereal::make_nvp("project", project));
	project.m_filePath = path;
	project.m_dirty = false;
	return project;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void Project::Save(const std::string& path)
{
	std::ofstream os(path);
	if (!os.is_open())
	{
		throw std::runtime_error("Failed to create project file: " + path);
	}

	cereal::JSONOutputArchive archive(os);
	archive(cereal::make_nvp("project", *this));
	m_filePath = path;
	m_dirty = false;
}

void Project::Save()
{
	if (m_filePath.empty())
	{
		throw std::runtime_error("No file path set. Use Save(path) first.");
	}
	Save(m_filePath);
}

} // namespace neurus::project
