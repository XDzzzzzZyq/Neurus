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
#include "editor/operations/OperationContext.h"
#include "editor/operations/OperationManager.h"
#include "editor/operations/SceneOperations.h"
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

		m_mesh  = std::make_shared<Mesh>();
		m_light = std::make_shared<Light>(POINTLIGHT, 10.0f, glm::vec3(1.0f));
		m_scene.UseMesh(m_mesh);
		m_scene.UseLight(m_light);
	}

	void Process() { m_eventBus.Process(); }

	EventQueue m_eventBus;
	Scene m_scene;
	OperationManager m_operations{ m_eventBus, [this]() -> Scene* { return &m_scene; } };
	SceneController m_controller;
	std::shared_ptr<Mesh> m_mesh;
	std::shared_ptr<Light> m_light;
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
	auto op = MakeSetPower(m_light->GetObjectID(), 3.0f, 77.0f);
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
	auto op = MakeSetPower(m_light->GetObjectID(), 10.0f, 90.0f);
	auto inv = op->Inverse();

	OperationContext ctx{ m_scene, m_eventBus };
	inv->Emit(ctx); // inverse applies the "before" value.
	EXPECT_FLOAT_EQ(m_light->light_power, 10.0f);
}

// --- Stale identity ---------------------------------------------------------

TEST_F(OperationManagerTest, Emit_UnknownUid_IsNoOp)
{
	const float before = m_light->light_power;
	auto op = MakeSetPower(999999, 1.0f, 2.0f); // no object with this UID

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
