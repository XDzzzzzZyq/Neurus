/**
 * @file DefaultScene.cpp
 * @brief Implementation of CreateDefaultScene factory function.
 *
 * Constructs a pre-configured Scene with camera, mesh, and light
 * following the OpenGL SceneConfigs factory pattern.
 */

#include "scene/DefaultScene.h"

#include "scene/Camera.h"
#include "scene/Light.h"
#include "scene/Mesh.h"
#include "core/Log.h"

#include <glm/glm.hpp>
#include <memory>

namespace neurus {

std::shared_ptr<Scene> CreateDefaultScene(const std::string& objPath)
{
	NEURUS_LOG("[DefaultScene] Creating default scene...");

	auto scene = std::make_shared<Scene>();

	// --- Camera ---
	// Default constructor: FOV 60°, near 0.1, far 100, pos(0,0,0), tar(0,0,0)
	NEURUS_LOG("[DefaultScene] Creating camera...");
	auto camera = std::make_shared<Camera>();
	camera->SetCamPos(glm::vec3(0.0f, 2.0f, 5.0f));
	camera->cam_tar = glm::vec3(0.0f, 0.0f, 0.0f);
	scene->UseCamera(camera);

	// --- Mesh ---
	NEURUS_LOG("[DefaultScene] Loading mesh: " << objPath);
	auto mesh = std::make_shared<Mesh>(objPath);
	scene->UseMesh(mesh);

	// --- Light ---
	NEURUS_LOG("[DefaultScene] Creating point light...");
	auto light = std::make_shared<Light>(POINTLIGHT, 10.0f, glm::vec3(1.0f));
	light->SetPosition(glm::vec3(3.0f, 3.0f, 3.0f));
	light->SetRadius(0.05f);
	scene->UseLight(light);

	NEURUS_LOG("[DefaultScene] Default scene created.");
	return scene;
}

} // namespace neurus
