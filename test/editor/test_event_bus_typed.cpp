#include <gtest/gtest.h>

#include <string>

#include "editor/events/EventBus.h"
#include "editor/events/EditorEvents.h"

using namespace neurus;

/**
 * @brief Tests for the typed EventQueue - no Qt, no GPU required.
 *
 * These tests validate the typed event dispatch system: subscribe, emit,
 * deferred Process(), independent event channels, and handler ordering.
 */
class TypedEventQueueTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		// EventQueue() returns the global singleton EventQueue
		m_queue = &EventQueue();
	}

	void TearDown() override
	{
		// Each test starts fresh - Process any remaining events
		m_queue->Process();
	}

	class EventQueue* m_queue = nullptr;
};

// ---------------------------------------------------------------------------
// Core subscribe/emit/Process lifecycle
// ---------------------------------------------------------------------------

TEST_F(TypedEventQueueTest, SubscribeAndEmit_HandlerReceivesEvent)
{
	bool received = false;
	int receivedId = 0;

	m_queue->subscribe<ObjectSelected>(
		[&](const ObjectSelected& e) {
			received = true;
			receivedId = e.objectId;
		});

	m_queue->enqueue(ObjectSelected{42});

	// Not received yet - deferred dispatch
	EXPECT_FALSE(received);

	m_queue->Process();

	EXPECT_TRUE(received);
	EXPECT_EQ(receivedId, 42);
}

TEST_F(TypedEventQueueTest, Emit_MultipleHandlersAllReceive)
{
	int callCount = 0;

	m_queue->subscribe<ObjectSelected>([&](const ObjectSelected&) { callCount++; });
	m_queue->subscribe<ObjectSelected>([&](const ObjectSelected&) { callCount++; });
	m_queue->subscribe<ObjectSelected>([&](const ObjectSelected&) { callCount++; });

	m_queue->enqueue(ObjectSelected{1});
	m_queue->Process();

	EXPECT_EQ(callCount, 3);
}

TEST_F(TypedEventQueueTest, Process_EmptyQueueIsNoOp)
{
	// Verify no crash or side effects when processing empty queue
	EXPECT_NO_THROW({ m_queue->Process(); });
	EXPECT_NO_THROW({ m_queue->Process(0); });
}

TEST_F(TypedEventQueueTest, Emit_WithoutSubscribersIsNoOp)
{
	// No subscribers registered - emit + process should not crash
	EXPECT_NO_THROW({
		m_queue->enqueue(ObjectSelected{99});
		m_queue->Process();
	});
}

// ---------------------------------------------------------------------------
// Independent event channels
// ---------------------------------------------------------------------------

TEST_F(TypedEventQueueTest, DifferentEventTypes_IndependentChannels)
{
	int objectSelectedCount = 0;
	int objectDeselectedCount = 0;

	m_queue->subscribe<ObjectSelected>([&](const ObjectSelected&) { objectSelectedCount++; });
	m_queue->subscribe<ObjectDeselected>([&](const ObjectDeselected&) { objectDeselectedCount++; });

	// Only emit ObjectSelected
	m_queue->enqueue(ObjectSelected{1});
	m_queue->enqueue(ObjectSelected{2});
	m_queue->Process();

	EXPECT_EQ(objectSelectedCount, 2);
	EXPECT_EQ(objectDeselectedCount, 0);
}

TEST_F(TypedEventQueueTest, MultipleEventTypes_EmitAndProcess)
{
	int selectCount = 0;
	int deselectCount = 0;
	int addCount = 0;
	int removeCount = 0;
	int camCount = 0;

	m_queue->subscribe<ObjectSelected>([&](const ObjectSelected&) { selectCount++; });
	m_queue->subscribe<ObjectDeselected>([&](const ObjectDeselected&) { deselectCount++; });
	m_queue->subscribe<SceneObjectAdded>([&](const SceneObjectAdded&) { addCount++; });
	m_queue->subscribe<SceneObjectRemoved>([&](const SceneObjectRemoved&) { removeCount++; });
	m_queue->subscribe<ActiveCameraChanged>([&](const ActiveCameraChanged&) { camCount++; });

	// Emit each type exactly once
	m_queue->enqueue(ObjectSelected{1});
	m_queue->enqueue(ObjectDeselected{2});
	m_queue->enqueue(SceneObjectAdded{3, "Light"});
	m_queue->enqueue(SceneObjectRemoved{4});
	m_queue->enqueue(ActiveCameraChanged{5});

	m_queue->Process();

	EXPECT_EQ(selectCount, 1);
	EXPECT_EQ(deselectCount, 1);
	EXPECT_EQ(addCount, 1);
	EXPECT_EQ(removeCount, 1);
	EXPECT_EQ(camCount, 1);
}

// ---------------------------------------------------------------------------
// Complex event data
// ---------------------------------------------------------------------------

TEST_F(TypedEventQueueTest, SceneObjectAdded_CarriesCorrectData)
{
	std::string receivedType;
	int receivedId = 0;

	m_queue->subscribe<SceneObjectAdded>(
		[&](const SceneObjectAdded& e) {
			receivedId = e.objectId;
			receivedType = e.typeName;
		});

	m_queue->enqueue(SceneObjectAdded{100, std::string("Mesh")});
	m_queue->Process();

	EXPECT_EQ(receivedId, 100);
	EXPECT_EQ(receivedType, "Mesh");
}

TEST_F(TypedEventQueueTest, ActiveCameraChanged_MinusOneForNoCamera)
{
	int receivedId = 999;

	m_queue->subscribe<ActiveCameraChanged>(
		[&](const ActiveCameraChanged& e) { receivedId = e.cameraId; });

	m_queue->enqueue(ActiveCameraChanged{-1});
	m_queue->Process();

	EXPECT_EQ(receivedId, -1);
}

// ---------------------------------------------------------------------------
// Multiple emits, single Process
// ---------------------------------------------------------------------------

TEST_F(TypedEventQueueTest, MultipleEmits_ProcessedInOrder)
{
	std::vector<int> receivedIds;

	m_queue->subscribe<ObjectSelected>(
		[&](const ObjectSelected& e) { receivedIds.push_back(e.objectId); });

	m_queue->enqueue(ObjectSelected{1});
	m_queue->enqueue(ObjectSelected{3});
	m_queue->enqueue(ObjectSelected{5});

	m_queue->Process();

	ASSERT_EQ(receivedIds.size(), 3);
	EXPECT_EQ(receivedIds[0], 1);
	EXPECT_EQ(receivedIds[1], 3);
	EXPECT_EQ(receivedIds[2], 5);
}

// ---------------------------------------------------------------------------
// Re-entrant emit (handler emits within Process)
// ---------------------------------------------------------------------------

TEST_F(TypedEventQueueTest, ReentrantEmit_HandlerEmitsDuringProcess)
{
	int outerCount = 0;
	int innerCount = 0;

	m_queue->subscribe<ObjectSelected>([&](const ObjectSelected&) {
		outerCount++;
		// Emit a different event type from within the handler
		m_queue->enqueue(ObjectDeselected{outerCount});
	});

	m_queue->subscribe<ObjectDeselected>([&](const ObjectDeselected&) {
		innerCount++;
	});

	m_queue->enqueue(ObjectSelected{1});
	m_queue->Process();

	// Both should execute because re-entrant emits are deferred to end of queue
	EXPECT_EQ(outerCount, 1);
	EXPECT_EQ(innerCount, 1);
}

TEST_F(TypedEventQueueTest, ReentrantEmit_SameTypeCreatesChain)
{
	int callCount = 0;

	m_queue->subscribe<ObjectSelected>([&](const ObjectSelected&) {
		callCount++;
		if (callCount < 5)
		{
			m_queue->enqueue(ObjectSelected{callCount});
		}
	});

	m_queue->enqueue(ObjectSelected{0});
	m_queue->Process();

	EXPECT_EQ(callCount, 5);
}

// ---------------------------------------------------------------------------
// maxEvents guard
// ---------------------------------------------------------------------------

TEST_F(TypedEventQueueTest, Process_MaxEventsCapPreventsInfiniteLoop)
{
	// Handler that keeps re-emitting
	m_queue->subscribe<ObjectSelected>([&](const ObjectSelected&) {
		m_queue->enqueue(ObjectSelected{0});
	});

	m_queue->enqueue(ObjectSelected{0});

	// Should not hang - capped at maxEvents
	EXPECT_NO_THROW({ m_queue->Process(10); });
}

// ---------------------------------------------------------------------------
// EventQueue is NOT copyable (compile-time check via static assert)
// ---------------------------------------------------------------------------

TEST_F(TypedEventQueueTest, EventQueueIsNotCopyable)
{
	static_assert(!std::is_copy_constructible_v<class EventQueue>,
	              "EventQueue must not be copyable");
	static_assert(!std::is_copy_assignable_v<class EventQueue>,
	              "EventQueue must not be copy-assignable");
	SUCCEED();
}
