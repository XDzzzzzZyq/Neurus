/**
 * @file test_selection.cpp
 * @brief Unit tests for SelectionController — click-select via raycast.
 *
 * Tests cover:
 * - Selection API: Select, Deselect, ClearSelection, IsSelected, GetActiveObject
 * - EventBus emission: ObjectSelected / ObjectDeselected on selection changes
 * - RaycastSelect: screen → world ray → sphere intersection against mesh objects
 * - BoxSelect: stub returns empty vector
 *
 * All tests are pure CPU — no GPU required.
 */

#include <gtest/gtest.h>

#include <memory>

#include "editor/SelectionController.h"
#include "editor/events/EditorEvents.h"
#include "editor/events/EventBus.h"
#include "scene/Camera.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"

using namespace neurus;

// ===========================================================================
// SelectionControllerTest — basic API (no EventBus dependency needed)
// ===========================================================================

class SelectionControllerTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_sel = std::make_unique<SelectionController>();
	}

	std::unique_ptr<SelectionController> m_sel;
};

// --- Construction ---

TEST_F(SelectionControllerTest, DefaultConstruction_NoSelection)
{
	EXPECT_TRUE(m_sel->GetSelection().empty());
	EXPECT_EQ(m_sel->GetActiveObject(), -1);
}

// --- Select ---

TEST_F(SelectionControllerTest, Select_SingleObject_AddedToSelection)
{
	m_sel->Select(42);

	EXPECT_TRUE(m_sel->IsSelected(42));
	EXPECT_EQ(m_sel->GetActiveObject(), 42);
	EXPECT_EQ(m_sel->GetSelection().size(), 1u);
}

TEST_F(SelectionControllerTest, Select_NegativeId_Ignored)
{
	m_sel->Select(-1);

	EXPECT_TRUE(m_sel->GetSelection().empty());
	EXPECT_EQ(m_sel->GetActiveObject(), -1);
}

TEST_F(SelectionControllerTest, Select_ZeroId_Accepted)
{
	m_sel->Select(0);

	EXPECT_TRUE(m_sel->IsSelected(0));
	EXPECT_EQ(m_sel->GetActiveObject(), 0);
}

TEST_F(SelectionControllerTest, Select_SameIdTwice_NoDuplicate)
{
	m_sel->Select(10);
	m_sel->Select(10);

	EXPECT_EQ(m_sel->GetSelection().size(), 1u);
	EXPECT_EQ(m_sel->GetActiveObject(), 10);
}

TEST_F(SelectionControllerTest, Select_MultipleIds_ReplacesSelection)
{
	m_sel->Select(1);
	m_sel->Select(2);
	m_sel->Select(3);

	// Each Select replaces the previous (single selection)
	EXPECT_EQ(m_sel->GetSelection().size(), 1u);
	EXPECT_TRUE(m_sel->IsSelected(3));
	EXPECT_FALSE(m_sel->IsSelected(1));
	EXPECT_FALSE(m_sel->IsSelected(2));
	EXPECT_EQ(m_sel->GetActiveObject(), 3);
}

// --- Deselect ---

TEST_F(SelectionControllerTest, Deselect_RemovesFromSelection)
{
	m_sel->Select(7);
	m_sel->Deselect(7);

	EXPECT_FALSE(m_sel->IsSelected(7));
	EXPECT_EQ(m_sel->GetActiveObject(), -1);
	EXPECT_TRUE(m_sel->GetSelection().empty());
}

TEST_F(SelectionControllerTest, Deselect_NegativeId_Ignored)
{
	m_sel->Select(5);
	m_sel->Deselect(-1);

	EXPECT_TRUE(m_sel->IsSelected(5));
	EXPECT_EQ(m_sel->GetSelection().size(), 1u);
}

TEST_F(SelectionControllerTest, Deselect_NotInSelection_NoOp)
{
	m_sel->Select(1);
	m_sel->Deselect(99);

	EXPECT_TRUE(m_sel->IsSelected(1));
	EXPECT_EQ(m_sel->GetSelection().size(), 1u);
}

// --- ClearSelection ---

TEST_F(SelectionControllerTest, ClearSelection_RemovesAll)
{
	m_sel->Select(1);
	m_sel->Select(2);
	m_sel->Select(3);

	EXPECT_EQ(m_sel->GetSelection().size(), 1u);

	m_sel->ClearSelection();

	EXPECT_TRUE(m_sel->GetSelection().empty());
	EXPECT_EQ(m_sel->GetActiveObject(), -1);
}

TEST_F(SelectionControllerTest, ClearSelection_EmptyNoOp)
{
	EXPECT_NO_THROW({ m_sel->ClearSelection(); });
	EXPECT_TRUE(m_sel->GetSelection().empty());
}

// --- IsSelected ---

TEST_F(SelectionControllerTest, IsSelected_ReturnsTrueForSelected)
{
	m_sel->Select(42);
	m_sel->Select(99);

	EXPECT_TRUE(m_sel->IsSelected(99));  // last select replaced 42
	EXPECT_FALSE(m_sel->IsSelected(42));
	EXPECT_FALSE(m_sel->IsSelected(-1));
	EXPECT_FALSE(m_sel->IsSelected(0));
}

// --- GetActiveObject ---

TEST_F(SelectionControllerTest, GetActiveObject_AfterDeselectActive_FallsBack)
{
	// Current design: Select replaces, so only one item.
	// After deselect, active becomes -1.
	m_sel->Select(1);
	m_sel->Deselect(1);

	EXPECT_EQ(m_sel->GetActiveObject(), -1);
}

// ===========================================================================
// SelectionControllerEventTest — EventBus emission
// ===========================================================================

class SelectionControllerEventTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_sel = std::make_unique<SelectionController>();
		m_pool = &EventBus();
	}

	void TearDown() override
	{
		// Drain any remaining events to keep queue clean
		m_pool->Process();
	}

	std::unique_ptr<SelectionController> m_sel;
	EventPool* m_pool = nullptr;
};

TEST_F(SelectionControllerEventTest, Select_EmitsObjectSelected)
{
	bool received = false;
	int receivedId = -1;

	m_pool->subscribe<ObjectSelected>([&](const ObjectSelected& e) {
		received = true;
		receivedId = e.objectId;
	});

	m_sel->Select(42);

	// Event is enqueued, must Process to dispatch
	EXPECT_FALSE(received); // not yet dispatched

	m_pool->Process();

	EXPECT_TRUE(received);
	EXPECT_EQ(receivedId, 42);
}

TEST_F(SelectionControllerEventTest, Deselect_EmitsObjectDeselected)
{
	bool received = false;
	int receivedId = -1;

	m_pool->subscribe<ObjectDeselected>([&](const ObjectDeselected& e) {
		received = true;
		receivedId = e.objectId;
	});

	m_sel->Select(7);
	m_pool->Process(); // drain the select event

	m_sel->Deselect(7);
	m_pool->Process();

	EXPECT_TRUE(received);
	EXPECT_EQ(receivedId, 7);
}

TEST_F(SelectionControllerEventTest, Deselect_NotSelected_NoEventEmitted)
{
	bool received = false;

	m_pool->subscribe<ObjectDeselected>([&](const ObjectDeselected&) {
		received = true;
	});

	m_sel->Deselect(99); // not in selection
	m_pool->Process();

	EXPECT_FALSE(received);
}

TEST_F(SelectionControllerEventTest, ClearSelection_EmitsDeselectedForAll)
{
	std::vector<int> deselectedIds;

	m_pool->subscribe<ObjectDeselected>([&](const ObjectDeselected& e) {
		deselectedIds.push_back(e.objectId);
	});

	// Select 42 (single selection replaces), so we need to test multiple deselections
	// by selecting sequentially and tracking what was selected
	m_sel->Select(10);
	m_pool->Process(); // drain select event

	m_sel->ClearSelection();
	m_pool->Process();

	ASSERT_EQ(deselectedIds.size(), 1u);
	EXPECT_EQ(deselectedIds[0], 10);
}

TEST_F(SelectionControllerEventTest, SelectSameId_NoDuplicateEvent)
{
	int selectCount = 0;

	m_pool->subscribe<ObjectSelected>([&](const ObjectSelected&) {
		selectCount++;
	});

	m_sel->Select(1);
	m_sel->Select(1); // same ID again
	m_pool->Process();

	// Only one ObjectSelected event should be emitted (first time)
	EXPECT_EQ(selectCount, 1);
}

// ===========================================================================
// SelectionControllerRaycastTest — raycast sphere intersection
// ===========================================================================

class SelectionControllerRaycastTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_sel = std::make_unique<SelectionController>();
		m_scene = std::make_unique<Scene>();

		// Camera looking along -Z at origin
		m_camera = std::make_shared<Camera>();
		m_camera->SetCamPos(glm::vec3(0.0f, 0.0f, 5.0f));
		m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
		m_camera->cam_w = 800.0f;
		m_camera->cam_h = 600.0f;
		m_camera->cam_pers = 60.0f;
		m_camera->cam_near = 0.1f;
		m_camera->cam_far = 100.0f;
		m_scene->UseCamera(m_camera);
	}

	std::unique_ptr<SelectionController> m_sel;
	std::unique_ptr<Scene> m_scene;
	std::shared_ptr<Camera> m_camera;
};

TEST_F(SelectionControllerRaycastTest, EmptyScene_ReturnsMinusOne)
{
	int hit = m_sel->RaycastSelect(*m_scene, *m_camera, 400.0f, 300.0f, 800, 600);
	EXPECT_EQ(hit, -1);
}

TEST_F(SelectionControllerRaycastTest, MeshAtOrigin_CenterClick_Hits)
{
	auto mesh = std::make_shared<Mesh>();
	mesh->SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
	int meshId = mesh->GetObjectID();
	m_scene->UseMesh(mesh);

	// Click at viewport center → ray from (0,0,5) toward (0,0,0)
	// Mesh at origin should be hit
	int hit = m_sel->RaycastSelect(*m_scene, *m_camera, 400.0f, 300.0f, 800, 600);

	EXPECT_EQ(hit, meshId);
}

TEST_F(SelectionControllerRaycastTest, MeshOffScreen_ClickMisses)
{
	auto mesh = std::make_shared<Mesh>();
	mesh->SetPosition(glm::vec3(100.0f, 0.0f, 0.0f)); // far off to the right
	m_scene->UseMesh(mesh);

	// Click at center — ray points at origin, mesh is way off
	int hit = m_sel->RaycastSelect(*m_scene, *m_camera, 400.0f, 300.0f, 800, 600);

	// The sphere test may still hit if the ray passes within radius 1.0 of (100,0,0)
	// To ensure miss, place mesh far enough away
	// Actually with a 1.0 radius sphere at (100,0,0), the ray from (0,0,5) to (0,0,0)
	// has direction (0,0,-1) — it stays at x=0, so it misses the sphere at x=100
	EXPECT_EQ(hit, -1);
}

TEST_F(SelectionControllerRaycastTest, MultipleMeshes_ReturnsClosest)
{
	auto meshFar = std::make_shared<Mesh>();
	meshFar->SetPosition(glm::vec3(0.0f, 0.0f, -3.0f)); // behind origin
	int idFar = meshFar->GetObjectID();
	m_scene->UseMesh(meshFar);

	auto meshNear = std::make_shared<Mesh>();
	meshNear->SetPosition(glm::vec3(0.0f, 0.0f, 0.0f)); // at origin (closer)
	int idNear = meshNear->GetObjectID();
	m_scene->UseMesh(meshNear);

	// Click center → ray hits both, should return the closer one (at origin)
	int hit = m_sel->RaycastSelect(*m_scene, *m_camera, 400.0f, 300.0f, 800, 600);

	EXPECT_EQ(hit, idNear);
}

TEST_F(SelectionControllerRaycastTest, MeshBehindCamera_NotHit)
{
	auto mesh = std::make_shared<Mesh>();
	mesh->SetPosition(glm::vec3(0.0f, 0.0f, 10.0f)); // behind camera (camera at z=5, looking toward -z)
	int meshId = mesh->GetObjectID();
	m_scene->UseMesh(mesh);

	// Click center — ray from (0,0,5) toward (0,0,0) (points toward -Z)
	// Mesh at z=10 is behind the camera → tca < 0 → no hit
	int hit = m_sel->RaycastSelect(*m_scene, *m_camera, 400.0f, 300.0f, 800, 600);

	EXPECT_EQ(hit, -1);
}

TEST_F(SelectionControllerRaycastTest, ScreenToRay_CornerProducesValidRay)
{
	auto mesh = std::make_shared<Mesh>();
	mesh->SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
	m_scene->UseMesh(mesh);

	// Click at top-left corner — ray should still be valid (no crash)
	int hit = m_sel->RaycastSelect(*m_scene, *m_camera, 0.0f, 0.0f, 800, 600);

	// May or may not hit depending on FOV — just verify no crash
	EXPECT_NO_THROW({
		m_sel->RaycastSelect(*m_scene, *m_camera, 0.0f, 0.0f, 800, 600);
	});
}

// ===========================================================================
// SelectionControllerBoxSelectTest — box-select stub
// ===========================================================================

class SelectionControllerBoxSelectTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_sel = std::make_unique<SelectionController>();
		m_scene = std::make_unique<Scene>();

		m_camera = std::make_shared<Camera>();
		m_camera->SetCamPos(glm::vec3(0.0f, 0.0f, 5.0f));
		m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
		m_camera->cam_w = 800.0f;
		m_camera->cam_h = 600.0f;
		m_scene->UseCamera(m_camera);
	}

	std::unique_ptr<SelectionController> m_sel;
	std::unique_ptr<Scene> m_scene;
	std::shared_ptr<Camera> m_camera;
};

TEST_F(SelectionControllerBoxSelectTest, BoxSelect_ReturnsEmpty)
{
	// Add some meshes to the scene
	auto mesh = std::make_shared<Mesh>();
	mesh->SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
	m_scene->UseMesh(mesh);

	std::vector<int> result = m_sel->BoxSelect(*m_scene, *m_camera,
	                                           100.0f, 100.0f, 300.0f, 300.0f,
	                                           800, 600);

	// Stub — always returns empty
	EXPECT_TRUE(result.empty());
}

TEST_F(SelectionControllerBoxSelectTest, BoxSelect_EmptyScene_ReturnsEmpty)
{
	std::vector<int> result = m_sel->BoxSelect(*m_scene, *m_camera,
	                                           0.0f, 0.0f, 800.0f, 600.0f,
	                                           800, 600);

	EXPECT_TRUE(result.empty());
}
