/**
 * @file test_operation_manager.cpp
 * @brief Undo/redo history + operation replay tests (no GPU).
 *
 * These exercise the full Phase 1 slice through the real controller path:
 * a scene event mutates state AND records an operation, then Undo/Redo replay
 * synthesized inverse/forward events synchronously. Tests cover round-trip,
 * involution, recording suppression during replay, redo invalidation, and
 * stale-identity no-op.
 */

#include <gtest/gtest.h>

#include <memory>

#include "glm/glm.hpp"

#include "editor/controllers/SceneController.h"
#include "editor/events/EventBus.h"
#include "editor/events/SceneEvents.h"
#include "editor/Input.h"
#include "editor/operations/OperationContext.h"
#include "editor/operations/OperationManager.h"
#include "editor/operations/SceneOperations.h"
#include "scene/Camera.h"
#include "scene/Light.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"
#include "scene/UID.h"

using namespace neurus;

class OperationManagerTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_controller.Init(m_eventBus, m_operations);

		m_mesh   = std::make_shared<Mesh>();
		m_light  = std::make_shared<Light>(POINTLIGHT, 10.0f, glm::vec3(1.0f));
		m_camera = std::make_shared<Camera>();
		m_scene.UseMesh(m_mesh);
		m_scene.UseLight(m_light);
		m_scene.UseCamera(m_camera);
	}

	void Process() { m_eventBus.Process(); }

	EventQueue m_eventBus;
	Scene m_scene;
	OperationManager m_operations{ m_eventBus, [this]() -> Scene* { return &m_scene; } };
	SceneController m_controller;
	std::shared_ptr<Mesh> m_mesh;
	std::shared_ptr<Light> m_light;
	std::shared_ptr<Camera> m_camera;
};

// --- Round-trip ------------------------------------------------------------

TEST_F(OperationManagerTest, LightPower_RoundTrip)
{
	m_eventBus.enqueue(LightPowerChanged{ m_light.get(), 42.0f });
	Process();
	EXPECT_FLOAT_EQ(m_light->light_power, 42.0f);
	ASSERT_TRUE(m_operations.CanUndo());
	EXPECT_FALSE(m_operations.CanRedo());

	m_operations.Undo();
	EXPECT_FLOAT_EQ(m_light->light_power, 10.0f);
	EXPECT_FALSE(m_operations.CanUndo());
	ASSERT_TRUE(m_operations.CanRedo());

	m_operations.Redo();
	EXPECT_FLOAT_EQ(m_light->light_power, 42.0f);
	EXPECT_TRUE(m_operations.CanUndo());
	EXPECT_FALSE(m_operations.CanRedo());
}

TEST_F(OperationManagerTest, Position_RoundTrip)
{
	m_eventBus.enqueue(PositionChanged{ m_mesh.get(), 1.0f, 2.0f, 3.0f });
	Process();
	EXPECT_EQ(m_mesh->GetPosition(), glm::vec3(1.0f, 2.0f, 3.0f));

	m_operations.Undo();
	EXPECT_EQ(m_mesh->GetPosition(), glm::vec3(0.0f, 0.0f, 0.0f));

	m_operations.Redo();
	EXPECT_EQ(m_mesh->GetPosition(), glm::vec3(1.0f, 2.0f, 3.0f));
}

// --- Recording suppression during replay -----------------------------------

TEST_F(OperationManagerTest, Replay_DoesNotRecordNewOperations)
{
	m_eventBus.enqueue(LightPowerChanged{ m_light.get(), 20.0f });
	Process();

	// Undo/Redo replay events synchronously through the same handler that
	// calls Submit(); the Replaying guard must suppress re-recording so the
	// stacks stay a single entry each and don't grow without bound.
	m_operations.Undo();
	EXPECT_FALSE(m_operations.CanUndo());
	EXPECT_TRUE(m_operations.CanRedo());

	m_operations.Redo();
	EXPECT_TRUE(m_operations.CanUndo());
	EXPECT_FALSE(m_operations.CanRedo());

	// A single undo returns to the recorded state (not stuck mid-stack).
	m_operations.Undo();
	EXPECT_FLOAT_EQ(m_light->light_power, 10.0f);
	EXPECT_FALSE(m_operations.CanUndo());
}

// --- Redo invalidation ------------------------------------------------------

TEST_F(OperationManagerTest, NewForwardOperation_ClearsRedo)
{
	m_eventBus.enqueue(LightPowerChanged{ m_light.get(), 30.0f });
	Process();
	m_operations.Undo();
	ASSERT_TRUE(m_operations.CanRedo());

	// A fresh edit after an undo invalidates the redo branch.
	m_eventBus.enqueue(LightPowerChanged{ m_light.get(), 55.0f });
	Process();
	EXPECT_FALSE(m_operations.CanRedo());
	EXPECT_TRUE(m_operations.CanUndo());
}

// --- Involution -------------------------------------------------------------

TEST_F(OperationManagerTest, Inverse_IsInvolution)
{
	auto op = std::make_unique<SetLightPowerOp>(m_light->GetObjectID(), 3.0f, 77.0f);
	auto twice = op->Inverse()->Inverse();

	// (g⁻¹)⁻¹ must reproduce the original forward effect.
	EXPECT_EQ(twice->Label(), op->Label());

	OperationContext ctx{ m_scene, m_eventBus };
	twice->Emit(ctx); // EmitNow() dispatches synchronously.
	EXPECT_FLOAT_EQ(m_light->light_power, 77.0f);
}

TEST_F(OperationManagerTest, Inverse_SwapsBeforeAfter)
{
	m_light->SetPower(10.0f);
	auto op = std::make_unique<SetLightPowerOp>(m_light->GetObjectID(), 10.0f, 90.0f);
	auto inv = op->Inverse();

	OperationContext ctx{ m_scene, m_eventBus };
	inv->Emit(ctx); // inverse applies the "before" value.
	EXPECT_FLOAT_EQ(m_light->light_power, 10.0f);
}

// --- Stale identity ---------------------------------------------------------

TEST_F(OperationManagerTest, Emit_UnknownUid_IsNoOp)
{
	const float before = m_light->light_power;
	auto op = std::make_unique<SetLightPowerOp>(999999, 1.0f, 2.0f); // no object with this UID

	OperationContext ctx{ m_scene, m_eventBus };
	op->Emit(ctx); // Resolve() -> nullptr -> safe no-op, must not crash.
	EXPECT_FLOAT_EQ(m_light->light_power, before);
}

// --- Empty stacks -----------------------------------------------------------

TEST_F(OperationManagerTest, UndoRedo_EmptyStacks_AreNoOps)
{
	EXPECT_FALSE(m_operations.CanUndo());
	EXPECT_FALSE(m_operations.CanRedo());
	m_operations.Undo(); // no-op
	m_operations.Redo(); // no-op
	EXPECT_FALSE(m_operations.CanUndo());
	EXPECT_FALSE(m_operations.CanRedo());
}

TEST_F(OperationManagerTest, Clear_EmptiesBothStacks)
{
	m_eventBus.enqueue(LightPowerChanged{ m_light.get(), 15.0f });
	Process();
	m_operations.Undo();
	ASSERT_TRUE(m_operations.CanRedo());

	m_operations.Clear();
	EXPECT_FALSE(m_operations.CanUndo());
	EXPECT_FALSE(m_operations.CanRedo());
}

// --- Visibility round-trip -------------------------------------------------

TEST_F(OperationManagerTest, Visibility_RoundTrip)
{
	// Mesh starts fully visible.
	ASSERT_TRUE(m_mesh->is_viewport);
	ASSERT_TRUE(m_mesh->is_rendered);

	m_eventBus.enqueue(VisibilityChanged{ m_mesh.get(), false, false });
	Process();
	EXPECT_FALSE(m_mesh->is_viewport);
	EXPECT_FALSE(m_mesh->is_rendered);

	m_operations.Undo();
	EXPECT_TRUE(m_mesh->is_viewport);
	EXPECT_TRUE(m_mesh->is_rendered);

	m_operations.Redo();
	EXPECT_FALSE(m_mesh->is_viewport);
	EXPECT_FALSE(m_mesh->is_rendered);
}

// --- Camera FOV ("Camera Ratio") round-trip --------------------------------

TEST_F(OperationManagerTest, CameraFov_RoundTrip)
{
	const float original = m_camera->cam_pers;

	m_eventBus.enqueue(CameraFovChanged{ m_camera.get(), 35.0f });
	Process();
	EXPECT_FLOAT_EQ(m_camera->cam_pers, 35.0f);

	m_operations.Undo();
	EXPECT_FLOAT_EQ(m_camera->cam_pers, original);

	m_operations.Redo();
	EXPECT_FLOAT_EQ(m_camera->cam_pers, 35.0f);
}

// --- Camera pose (coupled transform) round-trip ----------------------------

TEST_F(OperationManagerTest, CameraPose_RoundTrip)
{
	const glm::vec3 startPos(0.0f, 5.0f, 0.0f);
	const glm::vec3 startTar(0.0f, 0.0f, 0.0f);
	m_camera->SetPosition(startPos);
	m_camera->SetTarPos(startTar);

	const glm::vec3 endPos(2.0f, 5.0f, 1.0f);
	const glm::vec3 endTar(1.0f, 0.0f, 0.0f);

	// Simulate a recorded navigation: state already moved to end, op records it.
	m_camera->SetPosition(endPos);
	m_camera->SetTarPos(endTar);
	m_operations.Submit(std::make_unique<CameraTransformOp>(
		m_camera->GetObjectID(),
		CameraPose{ startPos, startTar },
		CameraPose{ endPos, endTar }));

	m_operations.Undo();
	EXPECT_EQ(m_camera->GetPosition(), startPos);
	EXPECT_EQ(m_camera->cam_tar, startTar);

	m_operations.Redo();
	EXPECT_EQ(m_camera->GetPosition(), endPos);
	EXPECT_EQ(m_camera->cam_tar, endTar);
}

// --- Merge coalescing -------------------------------------------------------

TEST_F(OperationManagerTest, CameraPose_Merge_CoalescesIntoOneUndo)
{
	const int uid = m_camera->GetObjectID();
	const glm::vec3 a(0.0f, 5.0f, 0.0f);
	const glm::vec3 b(0.0f, 4.0f, 0.0f);
	const glm::vec3 c(0.0f, 3.0f, 0.0f);
	const glm::vec3 tar(0.0f, 0.0f, 0.0f);

	// Three consecutive same-camera pose edits share a MergeKey and collapse
	// into a single undo entry spanning a→c.
	m_operations.Submit(std::make_unique<CameraTransformOp>(uid, CameraPose{ a, tar }, CameraPose{ b, tar }));
	m_operations.Submit(std::make_unique<CameraTransformOp>(uid, CameraPose{ b, tar }, CameraPose{ c, tar }));
	m_operations.Submit(std::make_unique<CameraTransformOp>(uid, CameraPose{ c, tar }, CameraPose{ a, tar }));

	ASSERT_TRUE(m_operations.CanUndo());

	// A single undo returns to the original endpoint, proving one merged entry.
	m_operations.Undo();
	EXPECT_EQ(m_camera->GetPosition(), a);
	EXPECT_FALSE(m_operations.CanUndo());
}

TEST_F(OperationManagerTest, DifferentKeys_DoNotMerge)
{
	m_eventBus.enqueue(CameraFovChanged{ m_camera.get(), 40.0f });
	Process();
	m_eventBus.enqueue(LightPowerChanged{ m_light.get(), 25.0f });
	Process();

	// Two different-key edits are two entries: two undos to clear.
	ASSERT_TRUE(m_operations.CanUndo());
	m_operations.Undo();
	EXPECT_TRUE(m_operations.CanUndo());
	m_operations.Undo();
	EXPECT_FALSE(m_operations.CanUndo());
}

TEST_F(OperationManagerTest, CameraTransform_Inverse_IsInvolution)
{
	const int uid = m_camera->GetObjectID();
	const glm::vec3 pos(1.0f, 2.0f, 3.0f);
	const glm::vec3 tar(4.0f, 5.0f, 6.0f);
	auto op = std::make_unique<CameraTransformOp>(
		uid, CameraPose{ glm::vec3(0.0f), glm::vec3(0.0f) }, CameraPose{ pos, tar });
	auto twice = op->Inverse()->Inverse();

	EXPECT_EQ(twice->Label(), op->Label());
	EXPECT_EQ(twice->MergeKey(), op->MergeKey());

	OperationContext ctx{ m_scene, m_eventBus };
	twice->Emit(ctx);
	EXPECT_EQ(m_camera->GetPosition(), pos);
	EXPECT_EQ(m_camera->cam_tar, tar);
}

// --- Selection undo/redo ----------------------------------------------------

TEST_F(OperationManagerTest, Selection_RoundTrip)
{
	// Select the mesh.
	m_eventBus.enqueue(ObjectSelected{ &m_scene, m_mesh.get(), 0 });
	Process();
	EXPECT_TRUE(m_scene.selections.IsSelected(m_mesh.get()));
	EXPECT_EQ(m_scene.selections.GetActiveObject(), m_mesh.get());
	ASSERT_TRUE(m_operations.CanUndo());

	// Undo clears the selection.
	m_operations.Undo();
	EXPECT_FALSE(m_scene.selections.IsSelected(m_mesh.get()));
	EXPECT_EQ(m_scene.selections.GetSelectionCount(), 0u);

	// Redo restores it.
	m_operations.Redo();
	EXPECT_TRUE(m_scene.selections.IsSelected(m_mesh.get()));
	EXPECT_EQ(m_scene.selections.GetActiveObject(), m_mesh.get());
}

TEST_F(OperationManagerTest, Selection_MultiSelect_RoundTrip)
{
	// Select mesh, then shift-add the light.
	m_eventBus.enqueue(ObjectSelected{ &m_scene, m_mesh.get(), 0 });
	Process();
	m_eventBus.enqueue(ObjectSelected{ &m_scene, m_light.get(), Input::Mod_Shift });
	Process();
	EXPECT_EQ(m_scene.selections.GetSelectionCount(), 2u);
	EXPECT_TRUE(m_scene.selections.IsSelected(m_mesh.get()));
	EXPECT_TRUE(m_scene.selections.IsSelected(m_light.get()));

	// Undo removes only the multi-select step (back to mesh only).
	m_operations.Undo();
	EXPECT_EQ(m_scene.selections.GetSelectionCount(), 1u);
	EXPECT_TRUE(m_scene.selections.IsSelected(m_mesh.get()));
	EXPECT_FALSE(m_scene.selections.IsSelected(m_light.get()));

	// Redo re-adds the light.
	m_operations.Redo();
	EXPECT_EQ(m_scene.selections.GetSelectionCount(), 2u);
	EXPECT_TRUE(m_scene.selections.IsSelected(m_light.get()));
}

TEST_F(OperationManagerTest, Selection_DoesNotClearRedo)
{
	// Record a real edit, then undo it so a redo is pending.
	m_eventBus.enqueue(LightPowerChanged{ m_light.get(), 42.0f });
	Process();
	m_operations.Undo();
	EXPECT_FLOAT_EQ(m_light->light_power, 10.0f);
	ASSERT_TRUE(m_operations.CanRedo());

	// A selection change is transparent: it appends to undo but MUST keep the
	// pending redo intact (the key property of PreservesRedo()).
	m_eventBus.enqueue(ObjectSelected{ &m_scene, m_mesh.get(), 0 });
	Process();
	EXPECT_TRUE(m_scene.selections.IsSelected(m_mesh.get()));
	ASSERT_TRUE(m_operations.CanRedo());

	// Redo still restores the undone edit.
	m_operations.Redo();
	EXPECT_FLOAT_EQ(m_light->light_power, 42.0f);
}

TEST_F(OperationManagerTest, RealEdit_ClearsRedo_AfterSelection)
{
	// Undo a real edit (redo pending), interleave a selection, then a real edit.
	m_eventBus.enqueue(LightPowerChanged{ m_light.get(), 42.0f });
	Process();
	m_operations.Undo();
	ASSERT_TRUE(m_operations.CanRedo());

	m_eventBus.enqueue(ObjectSelected{ &m_scene, m_mesh.get(), 0 });
	Process();
	ASSERT_TRUE(m_operations.CanRedo()); // selection preserved redo

	// A branching edit still discards the redo timeline.
	m_eventBus.enqueue(LightPowerChanged{ m_light.get(), 88.0f });
	Process();
	EXPECT_FALSE(m_operations.CanRedo());
}

TEST_F(OperationManagerTest, SetSelectionOp_Inverse_IsInvolution)
{
	SelectionState before{ {}, 0 };
	SelectionState after{ { m_mesh->GetObjectID() }, m_mesh->GetObjectID() };
	auto op = std::make_unique<SetSelectionOp>(before, after);
	auto twice = op->Inverse()->Inverse();

	EXPECT_EQ(twice->Label(), op->Label());
	EXPECT_TRUE(twice->PreservesRedo());

	OperationContext ctx{ m_scene, m_eventBus };
	twice->Emit(ctx); // reproduces the original "after" selection.
	EXPECT_TRUE(m_scene.selections.IsSelected(m_mesh.get()));
	EXPECT_EQ(m_scene.selections.GetActiveObject(), m_mesh.get());
}

