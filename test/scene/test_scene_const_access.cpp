/**
 * @file test_scene_const_access.cpp
 * @brief TDD tests for const Scene accessors — GetActiveCamera and GetObjectID.
 *
 * Validates:
 *   - const Scene::GetActiveCamera() returns valid pointer when camera is registered
 *   - const Scene::GetObjectID(int) returns valid pointer when object is registered
 *
 * @note Pure CPU tests — no Vulkan/GPU required.
 */

#include <gtest/gtest.h>

#include <scene/Camera.h>
#include <scene/Mesh.h>
#include <scene/Scene.h>

using namespace neurus;

// ---------------------------------------------------------------------------
// 1. const GetActiveCamera returns valid pointer
// ---------------------------------------------------------------------------

/**
 * @test Calling GetActiveCamera() on a const Scene with a registered camera returns a non-null pointer.
 */
TEST(SceneConstAccessTest, ConstScene_GetActiveCamera_ReturnsValidPointer)
{
	Scene scene;
	auto cam = std::make_shared<Camera>();
	int camID = cam->GetObjectID();
	scene.UseCamera(cam);

	// Obtain a const reference
	const Scene& constScene = scene;

	// const GetActiveCamera() must return non-null
	const Camera* active = constScene.GetActiveCamera();
	ASSERT_NE(active, nullptr);
	EXPECT_EQ(active->GetObjectID(), camID);

	// Verify it's the same camera
	EXPECT_EQ(active->o_type, ObjectID::GOType::GO_CAM);
}

/**
 * @test Calling GetActiveCamera() on a const empty Scene returns nullptr.
 */
TEST(SceneConstAccessTest, ConstScene_GetActiveCamera_EmptyScene_ReturnsNull)
{
	const Scene constScene;

	const Camera* active = constScene.GetActiveCamera();
	EXPECT_EQ(active, nullptr);
}

// ---------------------------------------------------------------------------
// 2. const GetObjectID returns valid pointer
// ---------------------------------------------------------------------------

/**
 * @test Calling GetObjectID(int) on a const Scene returns a valid pointer for a registered object.
 */
TEST(SceneConstAccessTest, ConstScene_GetObjectID_ReturnsValidPointer)
{
	Scene scene;

	// Register a mesh
	auto mesh = std::make_shared<Mesh>();
	int meshID = mesh->GetObjectID();
	scene.UseMesh(mesh);

	// Register a camera
	auto cam = std::make_shared<Camera>();
	int camID = cam->GetObjectID();
	scene.UseCamera(cam);

	const Scene& constScene = scene;

	// Query mesh by ID
	const ObjectID* foundMesh = constScene.GetObjectID(meshID);
	ASSERT_NE(foundMesh, nullptr);
	EXPECT_EQ(foundMesh->GetObjectID(), meshID);
	EXPECT_EQ(foundMesh->o_type, ObjectID::GOType::GO_MESH);

	// Query camera by ID
	const ObjectID* foundCam = constScene.GetObjectID(camID);
	ASSERT_NE(foundCam, nullptr);
	EXPECT_EQ(foundCam->GetObjectID(), camID);
	EXPECT_EQ(foundCam->o_type, ObjectID::GOType::GO_CAM);
}

/**
 * @test Calling GetObjectID(int) on a const Scene for a non-existent ID returns nullptr.
 */
TEST(SceneConstAccessTest, ConstScene_GetObjectID_Nonexistent_ReturnsNull)
{
	const Scene constScene;

	const ObjectID* missing = constScene.GetObjectID(42);
	EXPECT_EQ(missing, nullptr);

	// Negative ID should also return nullptr
	const ObjectID* negative = constScene.GetObjectID(-1);
	EXPECT_EQ(negative, nullptr);
}
