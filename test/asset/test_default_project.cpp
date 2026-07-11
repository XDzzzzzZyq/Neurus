/**
 * @file test_default_project.cpp
 * @brief Tests loading and validating the default project from res/default.neurus.json.
 *
 * Verifies that the shipped default project file can be loaded without errors
 * and contains the expected scene objects (camera, mesh, light).
 *
 * TDD: RED (test written first) → GREEN (implementation verified).
 * All tests are pure CPU — no GPU required.
 */

#include <gtest/gtest.h>

#include <string>

#include "project/Project.h"
#include "scene/Camera.h"
#include "scene/Environment.h"
#include "scene/Light.h"
#include "scene/Mesh.h"

using namespace neurus;

// -----------------------------------------------------------------------
// Path helper
// -----------------------------------------------------------------------

/**
 * @brief Constructs the full path to res/default.neurus.json.
 *
 * Uses the compile-time TEST_SOURCE_DIR definition (set in CMakeLists.txt)
 * to locate the resource file regardless of the current working directory.
 *
 * @return Absolute path to the default project file.
 */
static std::string DefaultProjectPath()
{
	return std::string(TEST_SOURCE_DIR) + "/res/default.neurus.json";
}

// -----------------------------------------------------------------------
// DefaultProject: Loads without exception
// -----------------------------------------------------------------------

/**
 * @test Opening the default project file does not throw.
 */
TEST(DefaultProject, LoadsWithoutException)
{
	EXPECT_NO_THROW({
		auto project = project::Project::Open(DefaultProjectPath());
	});
}

// -----------------------------------------------------------------------
// DefaultProject: Has camera
// -----------------------------------------------------------------------

/**
 * @test The default project contains at least one camera with valid FOV
 *       and near/far clip planes.
 */
TEST(DefaultProject, HasCamera)
{
	auto project = project::Project::Open(DefaultProjectPath());
	auto& scene = project.GetScene();

	EXPECT_GE(scene.cam_list.size(), 1u);

	// Verify the first camera has correct type and sensible parameters
	auto* cam = scene.cam_list.begin()->second.get();
	ASSERT_NE(cam, nullptr);
	EXPECT_EQ(cam->o_type, ObjectID::GOType::GO_CAM);
	EXPECT_GT(cam->cam_pers, 0.0f);
	EXPECT_GT(cam->cam_near, 0.0f);
	EXPECT_GT(cam->cam_far, cam->cam_near);
}

// -----------------------------------------------------------------------
// DefaultProject: Has mesh with valid OBJ path
// -----------------------------------------------------------------------

/**
 * @test The default project contains at least one mesh with a non-empty
 *       OBJ path configured for rendering.
 */
TEST(DefaultProject, HasMesh)
{
	auto project = project::Project::Open(DefaultProjectPath());
	auto& scene = project.GetScene();

	EXPECT_GE(scene.mesh_list.size(), 1u);

	auto* mesh = scene.mesh_list.begin()->second.get();
	ASSERT_NE(mesh, nullptr);
	EXPECT_EQ(mesh->o_type, ObjectID::GOType::GO_MESH);
	EXPECT_FALSE(mesh->o_meshPath.empty());
}

// -----------------------------------------------------------------------
// DefaultProject: Has light
// -----------------------------------------------------------------------

/**
 * @test The default project contains at least one light with positive power
 *       and a valid light type.
 */
TEST(DefaultProject, HasLight)
{
	auto project = project::Project::Open(DefaultProjectPath());
	auto& scene = project.GetScene();

	EXPECT_GE(scene.light_list.size(), 1u);

	auto* light = scene.light_list.begin()->second.get();
	ASSERT_NE(light, nullptr);
	EXPECT_EQ(light->o_type, ObjectID::GOType::GO_LIGHT);
	EXPECT_GT(light->light_power, 0.0f);
}

// -----------------------------------------------------------------------
// DefaultProject: Has environment in env_list
// -----------------------------------------------------------------------

/**
 * @test The default project file contains an Environment in env_list
 *       with the expected IBL equirect path.
 */
TEST(DefaultProject, HasEnvironment)
{
	auto project = project::Project::Open(DefaultProjectPath());
	auto& scene = project.GetScene();
	EXPECT_FALSE(scene.env_list.empty());
	auto env = scene.env_list.begin()->second;
	ASSERT_NE(env, nullptr);
	EXPECT_EQ(env->GetEquirectPath(), "tex/hdr/room.hdr");
}

// -----------------------------------------------------------------------
// DefaultProject: Environment roundtrip via CreateDefault
// -----------------------------------------------------------------------

/**
 * @test CreateDefault adds an Environment to env_list with the default
 *       equirect path, and the path can be updated via SetEquirectPath().
 */
TEST(DefaultProject, EnvironmentRoundtrip)
{
	auto project = project::Project::CreateDefault("fake.obj");
	auto& scene = project.GetScene();
	EXPECT_FALSE(scene.env_list.empty());
	auto env = scene.env_list.begin()->second;
	ASSERT_NE(env, nullptr);
	EXPECT_EQ(env->GetEquirectPath(), "tex/hdr/room.hdr");

	env->SetEquirectPath("tex/hdr/sunset.hdr");
	EXPECT_EQ(env->GetEquirectPath(), "tex/hdr/sunset.hdr");

	// Empty path should be allowed (procedural fallback)
	env->SetEquirectPath("");
	EXPECT_TRUE(env->GetEquirectPath().empty());
}
