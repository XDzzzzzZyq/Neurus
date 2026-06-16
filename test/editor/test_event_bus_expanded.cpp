#include <gtest/gtest.h>

#include "editor/events/EventBus.h"
#include "editor/events/EditorEvents.h"

using namespace neurus;

/**
 * @brief Expanded tests for typed EventBus - covering edge cases and
 *        specific editor event type behavior.
 *
 * These complement test_event_bus_typed.cpp with additional validation
 * of concrete editor event structs.
 */
class TypedEventBusExpandedTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_pool = &EventBus();
	}

	void TearDown() override
	{
		m_pool->Process();
	}

	EventPool* m_pool = nullptr;
};

// --- ObjectSelected: multiple values ---

TEST_F(TypedEventBusExpandedTest, ObjectSelected_MultipleEmits)
{
	std::vector<int> receivedIds;

	m_pool->subscribe<ObjectSelected>(
		[&](const ObjectSelected& e) { receivedIds.push_back(e.objectId); });

	m_pool->enqueue(ObjectSelected{1});
	m_pool->enqueue(ObjectSelected{2});
	m_pool->enqueue(ObjectSelected{99});

	m_pool->Process();

	ASSERT_EQ(receivedIds.size(), 3);
	EXPECT_EQ(receivedIds[0], 1);
	EXPECT_EQ(receivedIds[1], 2);
	EXPECT_EQ(receivedIds[2], 99);
}

// --- ObjectSelected vs ObjectDeselected - no cross-contamination ---

TEST_F(TypedEventBusExpandedTest, ObjectDeselected_NoCrossContamination)
{
	int selectCount = 0;
	int deselectCount = 0;

	m_pool->subscribe<ObjectSelected>([&](const ObjectSelected&) { selectCount++; });
	m_pool->subscribe<ObjectDeselected>([&](const ObjectDeselected&) { deselectCount++; });

	m_pool->enqueue(ObjectDeselected{7});
	m_pool->Process();

	EXPECT_EQ(selectCount, 0);
	EXPECT_EQ(deselectCount, 1);
}

// --- ObjectDeselected: correct data ---

TEST_F(TypedEventBusExpandedTest, ObjectDeselected_EmitReceivesCorrectId)
{
	int receivedId = 0;

	m_pool->subscribe<ObjectDeselected>(
		[&](const ObjectDeselected& e) { receivedId = e.objectId; });

	m_pool->enqueue(ObjectDeselected{7});
	m_pool->Process();

	EXPECT_EQ(receivedId, 7);
}

// --- SceneObjectRemoved: correct ID ---

TEST_F(TypedEventBusExpandedTest, SceneObjectRemoved_EmitReceivesCorrectId)
{
	int receivedId = 0;

	m_pool->subscribe<SceneObjectRemoved>(
		[&](const SceneObjectRemoved& e) { receivedId = e.objectId; });

	m_pool->enqueue(SceneObjectRemoved{200});
	m_pool->Process();

	EXPECT_EQ(receivedId, 200);
}

// --- ActiveCameraChanged: normal value ---

TEST_F(TypedEventBusExpandedTest, ActiveCameraChanged_EmitReceivesCorrectId)
{
	int receivedId = -1;

	m_pool->subscribe<ActiveCameraChanged>(
		[&](const ActiveCameraChanged& e) { receivedId = e.cameraId; });

	m_pool->enqueue(ActiveCameraChanged{3});
	m_pool->Process();

	EXPECT_EQ(receivedId, 3);
}

// --- SceneStatusChanged: propagation (mimics EditorContext::NotifySceneChanged) ---

TEST_F(TypedEventBusExpandedTest, SceneStatusChanged_EmitReceivesCorrectStatus)
{
	int receivedStatus = -1;

	m_pool->subscribe<SceneStatusChanged>(
		[&](const SceneStatusChanged& e) { receivedStatus = e.status; });

	// Simulate what EditorContext::NotifySceneChanged does
	m_pool->enqueue(SceneStatusChanged{1 << 2});
	m_pool->Process();

	EXPECT_EQ(receivedStatus, 4);
}

// --- SceneStatusChanged: multiple status values ---

TEST_F(TypedEventBusExpandedTest, SceneStatusChanged_MultipleValues)
{
	std::vector<int> receivedStatuses;

	m_pool->subscribe<SceneStatusChanged>(
		[&](const SceneStatusChanged& e) { receivedStatuses.push_back(e.status); });

	m_pool->enqueue(SceneStatusChanged{1});   // ObjectTransChanged
	m_pool->enqueue(SceneStatusChanged{2 | 4}); // LightChanged | CameraChanged
	m_pool->enqueue(SceneStatusChanged{0});    // NoChanges

	m_pool->Process();

	ASSERT_EQ(receivedStatuses.size(), 3);
	EXPECT_EQ(receivedStatuses[0], 1);
	EXPECT_EQ(receivedStatuses[1], 6);
	EXPECT_EQ(receivedStatuses[2], 0);
}

// --- All event types: independent channels ---

TEST_F(TypedEventBusExpandedTest, AllNewSignals_IndependentChannels)
{
	int selectCount = 0;
	int deselectCount = 0;
	int addCount = 0;
	int removeCount = 0;
	int camCount = 0;
	int statusCount = 0;

	m_pool->subscribe<ObjectSelected>([&](const ObjectSelected&) { selectCount++; });
	m_pool->subscribe<ObjectDeselected>([&](const ObjectDeselected&) { deselectCount++; });
	m_pool->subscribe<SceneObjectAdded>([&](const SceneObjectAdded&) { addCount++; });
	m_pool->subscribe<SceneObjectRemoved>([&](const SceneObjectRemoved&) { removeCount++; });
	m_pool->subscribe<ActiveCameraChanged>([&](const ActiveCameraChanged&) { camCount++; });
	m_pool->subscribe<SceneStatusChanged>([&](const SceneStatusChanged&) { statusCount++; });

	// Emit each signal exactly once
	m_pool->enqueue(ObjectSelected{1});
	m_pool->enqueue(ObjectDeselected{2});
	m_pool->enqueue(SceneObjectAdded{3, "Light"});
	m_pool->enqueue(SceneObjectRemoved{4});
	m_pool->enqueue(ActiveCameraChanged{5});
	m_pool->enqueue(SceneStatusChanged{0});

	m_pool->Process();

	EXPECT_EQ(selectCount, 1);
	EXPECT_EQ(deselectCount, 1);
	EXPECT_EQ(addCount, 1);
	EXPECT_EQ(removeCount, 1);
	EXPECT_EQ(camCount, 1);
	EXPECT_EQ(statusCount, 1);
}
