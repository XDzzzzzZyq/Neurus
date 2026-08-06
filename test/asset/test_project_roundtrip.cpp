/**
 * @file test_project_roundtrip.cpp
 * @brief Roundtrip serialization tests for Project Save -> Load.
 *
 * Verifies that Project::Save() followed by Project::Load() preserves
 * all scene data (cameras, meshes, lights) with exact parameter matching.
 *
 * Tests create bare Scene+RenderConfig objects, register them with a
 * Project serializer via SceneComponent and ConfigComponent, then
 * Save/Load through the Project.
 *
 * TDD: RED (test written first) -> GREEN (implementation verified).
 * All tests are pure CPU -- no GPU required.
 */

#include <gtest/gtest.h>

#include <cstdio>   // std::remove

#include "asset/Project.h"
#include "asset/SceneComponent.h"
#include "asset/ConfigComponent.h"
#include "render/RenderConfig.h"
#include "scene/Camera.h"
#include "scene/Light.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"

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
// Helpers
// -----------------------------------------------------------------------

/**
 * @brief Creates a Project serializer with SceneComponent + ConfigComponent bound.
 *
 * This is the standard pattern for roundtrip tests: register both components
 * so that Save stores and Load restores the scene and config together.
 *
 * @param scene   Scene object to serialize.
 * @param config  RenderConfig object to serialize.
 * @return Initialised project::Project ready for Save/Load.
 */
static project::Project MakeProject(Scene& scene, RenderConfig& config)
{
	project::Project p;
	p.Register<project::SceneComponent>(scene);
	p.Register<project::ConfigComponent>(config);
	return p;
}

// -----------------------------------------------------------------------
// Roundtrip: Empty Scene
// -----------------------------------------------------------------------

/**
 * @test Save an empty project, load it back -- all pools still empty.
 */
TEST(ProjectRoundtrip, EmptyScene)
{
	TempFile tmp("test_rt_empty.neurus.json");
	{
		Scene scene;
		RenderConfig config;
		auto p = MakeProject(scene, config);
		p.Save(tmp.path);
	}
	Scene loadedScene;
	RenderConfig loadedConfig;
	{
		auto p = MakeProject(loadedScene, loadedConfig);
		p.Load(tmp.path);
	}
	EXPECT_EQ(loadedScene.cam_list.size(), 1u);
	EXPECT_TRUE(loadedScene.mesh_list.empty());
	EXPECT_TRUE(loadedScene.light_list.empty());
	EXPECT_TRUE(loadedScene.sprite_list.empty());
	EXPECT_TRUE(loadedScene.dLine_list.empty());
	EXPECT_TRUE(loadedScene.dPoints_list.empty());
}

// -----------------------------------------------------------------------
// Roundtrip: Camera Only
// -----------------------------------------------------------------------

/**
 * @test Create scene with a camera, save, load -- camera data matches.
 */
TEST(ProjectRoundtrip, CameraOnly)
{
	TempFile tmp("test_rt_camera.neurus.json");
	{
		Scene scene;
		RenderConfig config;
		auto camera = std::make_shared<Camera>();
		camera->cam_pers = 60.0f;
		camera->cam_near = 0.1f;
		camera->cam_far = 100.0f;
		camera->cam_tar = glm::vec3(0.0f, 0.0f, 1.0f);
		camera->SetPosition(glm::vec3(0.0f, -5.0f, 2.0f));
		scene.UseCamera(camera);
		auto p = MakeProject(scene, config);
		p.Save(tmp.path);
	}
	Scene loadedScene;
	RenderConfig loadedConfig;
	{
		auto p = MakeProject(loadedScene, loadedConfig);
		p.Load(tmp.path);
	}
	ASSERT_EQ(loadedScene.cam_list.size(), 1u);
	auto* loadedCam = loadedScene.cam_list.begin()->second.get();
	ASSERT_NE(loadedCam, nullptr);
	EXPECT_FLOAT_EQ(loadedCam->cam_pers, 60.0f);
	EXPECT_FLOAT_EQ(loadedCam->cam_near, 0.1f);
	EXPECT_FLOAT_EQ(loadedCam->cam_far, 100.0f);
	EXPECT_EQ(loadedCam->cam_tar, glm::vec3(0.0f, 0.0f, 1.0f));
	EXPECT_EQ(loadedCam->GetPosition(), glm::vec3(0.0f, -5.0f, 2.0f));
	EXPECT_EQ(loadedCam->o_type, ObjectID::GOType::GO_CAM);
}

// -----------------------------------------------------------------------
// Roundtrip: Mesh with OBJ path
// -----------------------------------------------------------------------

/**
 * @test Create scene with a mesh, save, load -- o_meshPath and flags match.
 */
TEST(ProjectRoundtrip, MeshWithOBJ)
{
	TempFile tmp("test_rt_mesh.neurus.json");
	{
		Scene scene;
		RenderConfig config;
		auto mesh = std::make_shared<Mesh>("obj/sphere.obj");
		mesh->using_shadow = false;
		mesh->using_sdf = false;
		mesh->o_name = "TestSphere";
		scene.UseMesh(mesh);
		auto p = MakeProject(scene, config);
		p.Save(tmp.path);
	}
	Scene loadedScene;
	RenderConfig loadedConfig;
	{
		auto p = MakeProject(loadedScene, loadedConfig);
		p.Load(tmp.path);
	}
	ASSERT_EQ(loadedScene.mesh_list.size(), 1u);
	auto* loadedMesh = loadedScene.mesh_list.begin()->second.get();
	ASSERT_NE(loadedMesh, nullptr);
	EXPECT_EQ(loadedMesh->o_meshPath, "obj/sphere.obj");
	EXPECT_FALSE(loadedMesh->using_shadow);
	EXPECT_FALSE(loadedMesh->using_sdf);
	EXPECT_EQ(loadedMesh->o_name, "TestSphere");
	EXPECT_EQ(loadedMesh->o_type, ObjectID::GOType::GO_MESH);
}

// -----------------------------------------------------------------------
// Roundtrip: Point Light
// -----------------------------------------------------------------------

/**
 * @test Create scene with a point light, save, load -- light params match.
 */
TEST(ProjectRoundtrip, LightPoint)
{
	TempFile tmp("test_rt_light.neurus.json");
	{
		Scene scene;
		RenderConfig config;
		auto light = std::make_shared<Light>(POINTLIGHT, 10.0f, glm::vec3(1.0f, 0.8f, 0.6f));
		light->SetPosition(glm::vec3(3.0f, 3.0f, 3.0f));
		light->SetRadius(0.01f);
		scene.UseLight(light);
		auto p = MakeProject(scene, config);
		p.Save(tmp.path);
	}
	Scene loadedScene;
	RenderConfig loadedConfig;
	{
		auto p = MakeProject(loadedScene, loadedConfig);
		p.Load(tmp.path);
	}
	ASSERT_EQ(loadedScene.light_list.size(), 1u);
	auto* loadedLight = loadedScene.light_list.begin()->second.get();
	ASSERT_NE(loadedLight, nullptr);
	EXPECT_EQ(loadedLight->light_type, POINTLIGHT);
	EXPECT_FLOAT_EQ(loadedLight->light_power, 10.0f);
	EXPECT_EQ(loadedLight->light_color, glm::vec3(1.0f, 0.8f, 0.6f));
	EXPECT_FLOAT_EQ(loadedLight->light_radius, 0.01f);
	EXPECT_EQ(loadedLight->GetPosition(), glm::vec3(3.0f, 3.0f, 3.0f));
	EXPECT_EQ(loadedLight->o_type, ObjectID::GOType::GO_LIGHT);
}

// -----------------------------------------------------------------------
// Roundtrip: Full Scene (camera + mesh + light)
// -----------------------------------------------------------------------

/**
 * @test Create a full scene with camera, mesh, and light. Save, load --
 *       all three objects present with correct parameters.
 */
TEST(ProjectRoundtrip, FullScene)
{
	TempFile tmp("test_rt_full.neurus.json");
	{
		Scene scene;
		RenderConfig config;

		// Camera
		auto camera = std::make_shared<Camera>();
		camera->SetPosition(glm::vec3(0.0f, 10.0f, 3.0f));
		camera->cam_tar = glm::vec3(0.0f, 0.0f, 1.0f);
		camera->cam_pers = 45.0f;
		scene.UseCamera(camera);

		// Mesh
		auto mesh = std::make_shared<Mesh>("obj/cube.obj");
		mesh->SetPosition(glm::vec3(1.0f, 0.0f, 0.0f));
		scene.UseMesh(mesh);

		// Light
		auto light = std::make_shared<Light>(POINTLIGHT, 20.0f, glm::vec3(0.2f, 0.5f, 1.0f));
		light->SetPosition(glm::vec3(-2.0f, 0.0f, 5.0f));
		light->SetRadius(0.1f);
		scene.UseLight(light);

		auto p = MakeProject(scene, config);
		p.Save(tmp.path);
	}
	Scene loadedScene;
	RenderConfig loadedConfig;
	{
		auto p = MakeProject(loadedScene, loadedConfig);
		p.Load(tmp.path);
	}

	// Pool sizes
	EXPECT_EQ(loadedScene.cam_list.size(), 1u);
	EXPECT_EQ(loadedScene.mesh_list.size(), 1u);
	EXPECT_EQ(loadedScene.light_list.size(), 1u);

	// --- Camera validation ---
	auto* loadedCam = loadedScene.cam_list.begin()->second.get();
	ASSERT_NE(loadedCam, nullptr);
	EXPECT_EQ(loadedCam->GetPosition(), glm::vec3(0.0f, 10.0f, 3.0f));
	EXPECT_EQ(loadedCam->cam_tar, glm::vec3(0.0f, 0.0f, 1.0f));
	EXPECT_FLOAT_EQ(loadedCam->cam_pers, 45.0f);
	EXPECT_EQ(loadedCam->o_type, ObjectID::GOType::GO_CAM);

	// --- Mesh validation ---
	auto* loadedMesh = loadedScene.mesh_list.begin()->second.get();
	ASSERT_NE(loadedMesh, nullptr);
	EXPECT_EQ(loadedMesh->o_meshPath, "obj/cube.obj");
	EXPECT_EQ(loadedMesh->GetPosition(), glm::vec3(1.0f, 0.0f, 0.0f));
	EXPECT_EQ(loadedMesh->o_type, ObjectID::GOType::GO_MESH);

	// --- Light validation ---
	auto* loadedLight = loadedScene.light_list.begin()->second.get();
	ASSERT_NE(loadedLight, nullptr);
	EXPECT_EQ(loadedLight->light_type, POINTLIGHT);
	EXPECT_FLOAT_EQ(loadedLight->light_power, 20.0f);
	EXPECT_EQ(loadedLight->light_color, glm::vec3(0.2f, 0.5f, 1.0f));
	EXPECT_FLOAT_EQ(loadedLight->light_radius, 0.1f);
	EXPECT_EQ(loadedLight->GetPosition(), glm::vec3(-2.0f, 0.0f, 5.0f));
	EXPECT_EQ(loadedLight->o_type, ObjectID::GOType::GO_LIGHT);
}

// -----------------------------------------------------------------------
// Roundtrip: Selection state
// -----------------------------------------------------------------------

/**
 * @test Selection (stored as pointers, persisted as UIDs) survives a
 *       save/load roundtrip: selected set and active object are restored
 *       against the reloaded objects by UID.
 */
TEST(ProjectRoundtrip, SelectionState)
{
	TempFile tmp("test_rt_selection.neurus.json");
	int meshUid = 0;
	int lightUid = 0;
	{
		Scene scene;
		RenderConfig config;

		auto mesh = std::make_shared<Mesh>("obj/cube.obj");
		scene.UseMesh(mesh);
		meshUid = mesh->GetObjectID();

		auto light = std::make_shared<Light>(POINTLIGHT, 5.0f, glm::vec3(1.0f));
		scene.UseLight(light);
		lightUid = light->GetObjectID();

		// Select mesh (replace), then add light — light becomes active.
		scene.selections.Select(mesh.get(), false);
		scene.selections.Select(light.get(), true);

		auto p = MakeProject(scene, config);
		p.Save(tmp.path);
	}
	Scene loadedScene;
	RenderConfig loadedConfig;
	{
		auto p = MakeProject(loadedScene, loadedConfig);
		p.Load(tmp.path);
	}

	// Both objects selected, light is active.
	EXPECT_EQ(loadedScene.selections.GetSelectionCount(), 2u);

	const ObjectID* loadedMesh = loadedScene.GetObjectID(meshUid);
	const ObjectID* loadedLight = loadedScene.GetObjectID(lightUid);
	ASSERT_NE(loadedMesh, nullptr);
	ASSERT_NE(loadedLight, nullptr);
	EXPECT_TRUE(loadedScene.selections.IsSelected(loadedMesh));
	EXPECT_TRUE(loadedScene.selections.IsSelected(loadedLight));

	const ObjectID* active = loadedScene.selections.GetActiveObject();
	ASSERT_NE(active, nullptr);
	EXPECT_EQ(active->GetObjectID(), lightUid);
}

/**
 * @test A scene with no selection roundtrips to an empty selection
 *       (the selection block is present but empty).
 */
TEST(ProjectRoundtrip, EmptySelection)
{
	TempFile tmp("test_rt_empty_selection.neurus.json");
	{
		Scene scene;
		RenderConfig config;
		auto mesh = std::make_shared<Mesh>("obj/cube.obj");
		scene.UseMesh(mesh);
		auto p = MakeProject(scene, config);
		p.Save(tmp.path);
	}
	Scene loadedScene;
	RenderConfig loadedConfig;
	{
		auto p = MakeProject(loadedScene, loadedConfig);
		p.Load(tmp.path);
	}
	EXPECT_EQ(loadedScene.selections.GetSelectionCount(), 0u);
	EXPECT_EQ(loadedScene.selections.GetActiveObject(), nullptr);
}
