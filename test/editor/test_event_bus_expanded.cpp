#include <gtest/gtest.h>

#include "editor/events/EventBus.h"
#include "editor/events/EditorEvents.h"

using namespace neurus;

/**
 * @brief Expanded tests for typed EventQueue - covering edge cases and
 *        specific editor event type behavior.
 *
 * These complement test_event_bus_typed.cpp with additional validation
 * of concrete editor event structs.
 */
class TypedEventQueueExpandedTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_queue = &EventQueue();
	}

	void TearDown() override
	{
		m_queue->Process();
	}

	class EventQueue* m_queue = nullptr;
};

// --- ObjectSelected: multiple values ---

TEST_F(TypedEventQueueExpandedTest, ObjectSelected_MultipleEmits)
{
	std::vector<int> receivedIds;

	m_queue->subscribe<ObjectSelected>(
		[&](const ObjectSelected& e) { receivedIds.push_back(e.objectId); });

	m_queue->enqueue(ObjectSelected{1});
	m_queue->enqueue(ObjectSelected{2});
	m_queue->enqueue(ObjectSelected{99});

	m_queue->Process();

	ASSERT_EQ(receivedIds.size(), 3);
	EXPECT_EQ(receivedIds[0], 1);
	EXPECT_EQ(receivedIds[1], 2);
	EXPECT_EQ(receivedIds[2], 99);
}

// --- ObjectSelected vs ObjectDeselected - no cross-contamination ---

TEST_F(TypedEventQueueExpandedTest, ObjectDeselected_NoCrossContamination)
{
	int selectCount = 0;
	int deselectCount = 0;

	m_queue->subscribe<ObjectSelected>([&](const ObjectSelected&) { selectCount++; });
	m_queue->subscribe<ObjectDeselected>([&](const ObjectDeselected&) { deselectCount++; });

	m_queue->enqueue(ObjectDeselected{7});
	m_queue->Process();

	EXPECT_EQ(selectCount, 0);
	EXPECT_EQ(deselectCount, 1);
}

// --- ObjectDeselected: correct data ---

TEST_F(TypedEventQueueExpandedTest, ObjectDeselected_EmitReceivesCorrectId)
{
	int receivedId = 0;

	m_queue->subscribe<ObjectDeselected>(
		[&](const ObjectDeselected& e) { receivedId = e.objectId; });

	m_queue->enqueue(ObjectDeselected{7});
	m_queue->Process();

	EXPECT_EQ(receivedId, 7);
}

// --- SceneObjectRemoved: correct ID ---

TEST_F(TypedEventQueueExpandedTest, SceneObjectRemoved_EmitReceivesCorrectId)
{
	int receivedId = 0;

	m_queue->subscribe<SceneObjectRemoved>(
		[&](const SceneObjectRemoved& e) { receivedId = e.objectId; });

	m_queue->enqueue(SceneObjectRemoved{200});
	m_queue->Process();

	EXPECT_EQ(receivedId, 200);
}

// --- ActiveCameraChanged: normal value ---

TEST_F(TypedEventQueueExpandedTest, ActiveCameraChanged_EmitReceivesCorrectId)
{
	int receivedId = -1;

	m_queue->subscribe<ActiveCameraChanged>(
		[&](const ActiveCameraChanged& e) { receivedId = e.cameraId; });

	m_queue->enqueue(ActiveCameraChanged{3});
	m_queue->Process();

	EXPECT_EQ(receivedId, 3);
}

// --- SceneStatusChanged: propagation (mimics EditorContext::NotifySceneChanged) ---

TEST_F(TypedEventQueueExpandedTest, SceneStatusChanged_EmitReceivesCorrectStatus)
{
	int receivedStatus = -1;

	m_queue->subscribe<SceneStatusChanged>(
		[&](const SceneStatusChanged& e) { receivedStatus = e.status; });

	// Simulate what EditorContext::NotifySceneChanged does
	m_queue->enqueue(SceneStatusChanged{1 << 2});
	m_queue->Process();

	EXPECT_EQ(receivedStatus, 4);
}

// --- SceneStatusChanged: multiple status values ---

TEST_F(TypedEventQueueExpandedTest, SceneStatusChanged_MultipleValues)
{
	std::vector<int> receivedStatuses;

	m_queue->subscribe<SceneStatusChanged>(
		[&](const SceneStatusChanged& e) { receivedStatuses.push_back(e.status); });

	m_queue->enqueue(SceneStatusChanged{1});   // ObjectTransChanged
	m_queue->enqueue(SceneStatusChanged{2 | 4}); // LightChanged | CameraChanged
	m_queue->enqueue(SceneStatusChanged{0});    // NoChanges

	m_queue->Process();

	ASSERT_EQ(receivedStatuses.size(), 3);
	EXPECT_EQ(receivedStatuses[0], 1);
	EXPECT_EQ(receivedStatuses[1], 6);
	EXPECT_EQ(receivedStatuses[2], 0);
}

// --- All event types: independent channels ---

TEST_F(TypedEventQueueExpandedTest, AllNewSignals_IndependentChannels)
{
	int selectCount = 0;
	int deselectCount = 0;
	int addCount = 0;
	int removeCount = 0;
	int camCount = 0;
	int statusCount = 0;

	m_queue->subscribe<ObjectSelected>([&](const ObjectSelected&) { selectCount++; });
	m_queue->subscribe<ObjectDeselected>([&](const ObjectDeselected&) { deselectCount++; });
	m_queue->subscribe<SceneObjectAdded>([&](const SceneObjectAdded&) { addCount++; });
	m_queue->subscribe<SceneObjectRemoved>([&](const SceneObjectRemoved&) { removeCount++; });
	m_queue->subscribe<ActiveCameraChanged>([&](const ActiveCameraChanged&) { camCount++; });
	m_queue->subscribe<SceneStatusChanged>([&](const SceneStatusChanged&) { statusCount++; });

	// Emit each signal exactly once
	m_queue->enqueue(ObjectSelected{1});
	m_queue->enqueue(ObjectDeselected{2});
	m_queue->enqueue(SceneObjectAdded{3, "Light"});
	m_queue->enqueue(SceneObjectRemoved{4});
	m_queue->enqueue(ActiveCameraChanged{5});
	m_queue->enqueue(SceneStatusChanged{0});

	m_queue->Process();

	EXPECT_EQ(selectCount, 1);
	EXPECT_EQ(deselectCount, 1);
	EXPECT_EQ(addCount, 1);
	EXPECT_EQ(removeCount, 1);
	EXPECT_EQ(camCount, 1);
	EXPECT_EQ(statusCount, 1);
}
