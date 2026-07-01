/**
 * @file Project.cpp
 * @brief Project layer implementation - persistence via cereal JSON archives.
 */

#include "Project.h"

#include <cereal/archives/json.hpp>

#include <fstream>
#include <stdexcept>

#include <glm/glm.hpp>

#include "core/Log.h"
#include "scene/Environment.h"

namespace neurus::project
{

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

Project::Project()
	: proj_scene(std::make_unique<Scene>())
{
}

// ---------------------------------------------------------------------------
// Factory methods
// ---------------------------------------------------------------------------

Project Project::New()
{
	return {};
}

Project Project::Open(const std::string& path, const std::string& assetDir)
{
	std::ifstream is(path);
	if (!is.is_open())
	{
		throw std::runtime_error("Failed to open project file: " + path);
	}

	cereal::JSONInputArchive archive(is);
	Project project{};
	archive(cereal::make_nvp("project", project));
	project.proj_filePath = path;
	project.proj_dirty = false;

	// Reload mesh geometry from OBJ paths (not stored in JSON)
	for (auto& [id, mesh] : project.proj_scene->mesh_list)
	{
		mesh->ReloadMeshData(assetDir);
	}

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
	proj_filePath = path;
	proj_dirty = false;
}

void Project::Save()
{
	if (proj_filePath.empty())
	{
		throw std::runtime_error("No file path set. Use Save(path) first.");
	}
	Save(proj_filePath);
}

// ---------------------------------------------------------------------------
// CreateDefault factory
// ---------------------------------------------------------------------------

Project Project::CreateDefault(const std::string& objPath)
{
	NEURUS_LOG("[Project] Creating default project...");

	Project project;

	// --- Camera ---
	// Default constructor: FOV 60°, near 0.1, far 100, pos(0,0,0), tar(0,0,0)
	auto camera = std::make_shared<Camera>();
	camera->SetCamPos(glm::vec3(0.0f, -5.0f, 2.0f));
	camera->cam_tar = glm::vec3(0.0f, 0.0f, 0.0f);
	project.proj_scene->UseCamera(camera);

	// --- Mesh ---
	auto mesh = std::make_shared<Mesh>(objPath);
	project.proj_scene->UseMesh(mesh);

	// --- Light ---
	auto light = std::make_shared<Light>(POINTLIGHT, 10.0f, glm::vec3(1.0f));
	light->SetPosition(glm::vec3(3.0f, 3.0f, 3.0f));
	light->SetRadius(0.05f);
	project.proj_scene->UseLight(light);

	// --- Environment ---
	auto env = std::make_shared<Environment>();
	env->SetEquirectPath("tex/hdr/room.hdr");
	project.proj_scene->UseEnvironment(env);

	NEURUS_LOG("[Project] Default project created.");
	return project;
}

} // namespace neurus::project
