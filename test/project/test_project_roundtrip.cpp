/**
 * @file test_project_roundtrip.cpp
 * @brief Roundtrip serialization tests for Project Save → Load.
 *
 * Verifies that Project::Save() followed by Project::Open() preserves
 * all scene data (cameras, meshes, lights) with exact parameter matching.
 *
 * TDD: RED (test written first) → GREEN (implementation verified).
 * All tests are pure CPU — no GPU required.
 */

#include <gtest/gtest.h>

#include <cstdio>   // std::remove

#include "project/Project.h"
#include "scene/Camera.h"
#include "scene/Light.h"
#include "scene/Mesh.h"

using namespace neurus;

// -----------------------------------------------------------------------
// RAII temporary file cleaner
// -----------------------------------------------------------------------

/**
 * @brief RAII wrapper that removes the temporary file on destruction.
 *
 * Ensures test-generated .neurus.json files are cleaned up even if
 * the test fails with an exception.
 */
struct TempFile
{
	std::string path;
	explicit TempFile(std::string p) : path(std::move(p)) {}
	~TempFile() { std::remove(path.c_str()); }
};

// -----------------------------------------------------------------------
// Roundtrip: Empty Scene
// -----------------------------------------------------------------------

/**
 * @test Save an empty project, load it back — all pools still empty.
 */
TEST(ProjectRoundtrip, EmptyScene)
{
	TempFile tmp("test_roundtrip_empty.neurus.json");

	auto project = project::Project::New();
	project.Save(tmp.path);

	auto loaded = project::Project::Open(tmp.path);
	auto& scene = loaded.GetScene();

	EXPECT_TRUE(scene.cam_list.empty());
	EXPECT_TRUE(scene.mesh_list.empty());
	EXPECT_TRUE(scene.light_list.empty());
	EXPECT_TRUE(scene.sprite_list.empty());
	EXPECT_TRUE(scene.dLine_list.empty());
	EXPECT_TRUE(scene.dPoints_list.empty());
}

// -----------------------------------------------------------------------
// Roundtrip: Camera Only
// -----------------------------------------------------------------------

/**
 * @test Create scene with a camera, save, load — camera data matches.
 */
TEST(ProjectRoundtrip, CameraOnly)
{
	TempFile tmp("test_roundtrip_camera.neurus.json");

	auto project = project::Project::New();
	auto& scene = project.GetScene();

	auto camera = std::make_shared<Camera>();
	camera->cam_pers = 60.0f;
	camera->cam_near = 0.1f;
	camera->cam_far = 100.0f;
	camera->cam_tar = glm::vec3(0.0f, 1.0f, 0.0f);
	camera->SetCamPos(glm::vec3(0.0f, 2.0f, 5.0f));
	scene.UseCamera(camera);

	project.Save(tmp.path);

	auto loaded = project::Project::Open(tmp.path);
	auto& loadedScene = loaded.GetScene();

	ASSERT_EQ(loadedScene.cam_list.size(), 1u);

	auto* loadedCam = loadedScene.cam_list.begin()->second.get();
	ASSERT_NE(loadedCam, nullptr);
	EXPECT_FLOAT_EQ(loadedCam->cam_pers, 60.0f);
	EXPECT_FLOAT_EQ(loadedCam->cam_near, 0.1f);
	EXPECT_FLOAT_EQ(loadedCam->cam_far, 100.0f);
	EXPECT_EQ(loadedCam->cam_tar, glm::vec3(0.0f, 1.0f, 0.0f));
	EXPECT_EQ(loadedCam->GetPosition(), glm::vec3(0.0f, 2.0f, 5.0f));
	EXPECT_EQ(loadedCam->o_type, ObjectID::GOType::GO_CAM);
}

// -----------------------------------------------------------------------
// Roundtrip: Mesh with OBJ path
// -----------------------------------------------------------------------

/**
 * @test Create scene with a mesh, save, load — o_meshPath and flags match.
 */
TEST(ProjectRoundtrip, MeshWithOBJ)
{
	TempFile tmp("test_roundtrip_mesh.neurus.json");

	auto project = project::Project::New();
	auto& scene = project.GetScene();

	auto mesh = std::make_shared<Mesh>("obj/sphere.obj");
	mesh->using_shadow = false;
	mesh->using_sdf = false;
	mesh->o_name = "TestSphere";
	scene.UseMesh(mesh);

	project.Save(tmp.path);

	auto loaded = project::Project::Open(tmp.path);
	auto& loadedScene = loaded.GetScene();

	ASSERT_EQ(loadedScene.mesh_list.size(), 1u);

	auto* loadedMesh = loadedScene.mesh_list.begin()->second.get();
	ASSERT_NE(loadedMesh, nullptr);
	EXPECT_EQ(loadedMesh->o_meshPath, std::string("obj/sphere.obj"));
	EXPECT_EQ(loadedMesh->using_shadow, false);
	EXPECT_EQ(loadedMesh->using_sdf, false);
	EXPECT_EQ(loadedMesh->o_name, std::string("TestSphere"));
	EXPECT_EQ(loadedMesh->o_type, ObjectID::GOType::GO_MESH);
}

// -----------------------------------------------------------------------
// Roundtrip: Point Light
// -----------------------------------------------------------------------

/**
 * @test Create scene with a point light, save, load — light params match.
 */
TEST(ProjectRoundtrip, LightPoint)
{
	TempFile tmp("test_roundtrip_light.neurus.json");

	auto project = project::Project::New();
	auto& scene = project.GetScene();

	auto light = std::make_shared<Light>(POINTLIGHT, 10.0f, glm::vec3(1.0f, 0.8f, 0.6f));
	light->SetPosition(glm::vec3(3.0f, 3.0f, 3.0f));
	light->SetRadius(0.05f);
	scene.UseLight(light);

	project.Save(tmp.path);

	auto loaded = project::Project::Open(tmp.path);
	auto& loadedScene = loaded.GetScene();

	ASSERT_EQ(loadedScene.light_list.size(), 1u);

	auto* loadedLight = loadedScene.light_list.begin()->second.get();
	ASSERT_NE(loadedLight, nullptr);
	EXPECT_EQ(loadedLight->light_type, POINTLIGHT);
	EXPECT_FLOAT_EQ(loadedLight->light_power, 10.0f);
	EXPECT_EQ(loadedLight->light_color, glm::vec3(1.0f, 0.8f, 0.6f));
	EXPECT_FLOAT_EQ(loadedLight->light_radius, 0.05f);
	EXPECT_EQ(loadedLight->GetPosition(), glm::vec3(3.0f, 3.0f, 3.0f));
	EXPECT_EQ(loadedLight->o_type, ObjectID::GOType::GO_LIGHT);
}

// -----------------------------------------------------------------------
// Roundtrip: Full Scene (camera + mesh + light)
// -----------------------------------------------------------------------

/**
 * @test Create a full scene with camera, mesh, and light. Save, load —
 *       all three objects present with correct parameters.
 */
TEST(ProjectRoundtrip, FullScene)
{
	TempFile tmp("test_roundtrip_full.neurus.json");

	auto project = project::Project::New();
	auto& scene = project.GetScene();

	// Camera
	auto camera = std::make_shared<Camera>();
	camera->SetCamPos(glm::vec3(0.0f, 3.0f, 10.0f));
	camera->cam_tar = glm::vec3(0.0f, 1.0f, 0.0f);
	camera->cam_pers = 45.0f;
	scene.UseCamera(camera);

	// Mesh
	auto mesh = std::make_shared<Mesh>("obj/cube.obj");
	mesh->SetPosition(glm::vec3(1.0f, 0.0f, 0.0f));
	scene.UseMesh(mesh);

	// Light
	auto light = std::make_shared<Light>(POINTLIGHT, 20.0f, glm::vec3(0.2f, 0.5f, 1.0f));
	light->SetPosition(glm::vec3(-2.0f, 5.0f, 0.0f));
	light->SetRadius(0.1f);
	scene.UseLight(light);

	project.Save(tmp.path);

	auto loaded = project::Project::Open(tmp.path);
	auto& loadedScene = loaded.GetScene();

	// Pool sizes
	EXPECT_EQ(loadedScene.cam_list.size(), 1u);
	EXPECT_EQ(loadedScene.mesh_list.size(), 1u);
	EXPECT_EQ(loadedScene.light_list.size(), 1u);

	// --- Camera validation ---
	auto* loadedCam = loadedScene.cam_list.begin()->second.get();
	ASSERT_NE(loadedCam, nullptr);
	EXPECT_EQ(loadedCam->GetPosition(), glm::vec3(0.0f, 3.0f, 10.0f));
	EXPECT_EQ(loadedCam->cam_tar, glm::vec3(0.0f, 1.0f, 0.0f));
	EXPECT_FLOAT_EQ(loadedCam->cam_pers, 45.0f);
	EXPECT_EQ(loadedCam->o_type, ObjectID::GOType::GO_CAM);

	// --- Mesh validation ---
	auto* loadedMesh = loadedScene.mesh_list.begin()->second.get();
	ASSERT_NE(loadedMesh, nullptr);
	EXPECT_EQ(loadedMesh->o_meshPath, std::string("obj/cube.obj"));
	EXPECT_EQ(loadedMesh->GetPosition(), glm::vec3(1.0f, 0.0f, 0.0f));
	EXPECT_EQ(loadedMesh->o_type, ObjectID::GOType::GO_MESH);

	// --- Light validation ---
	auto* loadedLight = loadedScene.light_list.begin()->second.get();
	ASSERT_NE(loadedLight, nullptr);
	EXPECT_EQ(loadedLight->light_type, POINTLIGHT);
	EXPECT_FLOAT_EQ(loadedLight->light_power, 20.0f);
	EXPECT_EQ(loadedLight->light_color, glm::vec3(0.2f, 0.5f, 1.0f));
	EXPECT_FLOAT_EQ(loadedLight->light_radius, 0.1f);
	EXPECT_EQ(loadedLight->GetPosition(), glm::vec3(-2.0f, 5.0f, 0.0f));
	EXPECT_EQ(loadedLight->o_type, ObjectID::GOType::GO_LIGHT);
}
