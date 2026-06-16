#include <gtest/gtest.h>

#include <string>

#include "editor/events/EventBus.h"
#include "editor/events/EditorEvents.h"

using namespace neurus;

/**
 * @brief Tests for the typed EventPool (EventBus) - no Qt, no GPU required.
 *
 * These tests validate the typed event dispatch system: subscribe, emit,
 * deferred Process(), independent event channels, and handler ordering.
 */
class TypedEventBusTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		// EventBus() returns the global singleton EventPool
		m_pool = &EventBus();
	}

	void TearDown() override
	{
		// Each test starts fresh - Process any remaining events
		m_pool->Process();
	}

	EventPool* m_pool = nullptr;
};

// ---------------------------------------------------------------------------
// Core subscribe/emit/Process lifecycle
// ---------------------------------------------------------------------------

TEST_F(TypedEventBusTest, SubscribeAndEmit_HandlerReceivesEvent)
{
	bool received = false;
	int receivedId = 0;

	m_pool->subscribe<ObjectSelected>(
		[&](const ObjectSelected& e) {
			received = true;
			receivedId = e.objectId;
		});

	m_pool->enqueue(ObjectSelected{42});

	// Not received yet - deferred dispatch
	EXPECT_FALSE(received);

	m_pool->Process();

	EXPECT_TRUE(received);
	EXPECT_EQ(receivedId, 42);
}

TEST_F(TypedEventBusTest, Emit_MultipleHandlersAllReceive)
{
	int callCount = 0;

	m_pool->subscribe<ObjectSelected>([&](const ObjectSelected&) { callCount++; });
	m_pool->subscribe<ObjectSelected>([&](const ObjectSelected&) { callCount++; });
	m_pool->subscribe<ObjectSelected>([&](const ObjectSelected&) { callCount++; });

	m_pool->enqueue(ObjectSelected{1});
	m_pool->Process();

	EXPECT_EQ(callCount, 3);
}

TEST_F(TypedEventBusTest, Process_EmptyQueueIsNoOp)
{
	// Verify no crash or side effects when processing empty queue
	EXPECT_NO_THROW({ m_pool->Process(); });
	EXPECT_NO_THROW({ m_pool->Process(0); });
}

TEST_F(TypedEventBusTest, Emit_WithoutSubscribersIsNoOp)
{
	// No subscribers registered - emit + process should not crash
	EXPECT_NO_THROW({
		m_pool->enqueue(ObjectSelected{99});
		m_pool->Process();
	});
}

// ---------------------------------------------------------------------------
// Independent event channels
// ---------------------------------------------------------------------------

TEST_F(TypedEventBusTest, DifferentEventTypes_IndependentChannels)
{
	int objectSelectedCount = 0;
	int objectDeselectedCount = 0;

	m_pool->subscribe<ObjectSelected>([&](const ObjectSelected&) { objectSelectedCount++; });
	m_pool->subscribe<ObjectDeselected>([&](const ObjectDeselected&) { objectDeselectedCount++; });

	// Only emit ObjectSelected
	m_pool->enqueue(ObjectSelected{1});
	m_pool->enqueue(ObjectSelected{2});
	m_pool->Process();

	EXPECT_EQ(objectSelectedCount, 2);
	EXPECT_EQ(objectDeselectedCount, 0);
}

TEST_F(TypedEventBusTest, MultipleEventTypes_EmitAndProcess)
{
	int selectCount = 0;
	int deselectCount = 0;
	int addCount = 0;
	int removeCount = 0;
	int camCount = 0;

	m_pool->subscribe<ObjectSelected>([&](const ObjectSelected&) { selectCount++; });
	m_pool->subscribe<ObjectDeselected>([&](const ObjectDeselected&) { deselectCount++; });
	m_pool->subscribe<SceneObjectAdded>([&](const SceneObjectAdded&) { addCount++; });
	m_pool->subscribe<SceneObjectRemoved>([&](const SceneObjectRemoved&) { removeCount++; });
	m_pool->subscribe<ActiveCameraChanged>([&](const ActiveCameraChanged&) { camCount++; });

	// Emit each type exactly once
	m_pool->enqueue(ObjectSelected{1});
	m_pool->enqueue(ObjectDeselected{2});
	m_pool->enqueue(SceneObjectAdded{3, "Light"});
	m_pool->enqueue(SceneObjectRemoved{4});
	m_pool->enqueue(ActiveCameraChanged{5});

	m_pool->Process();

	EXPECT_EQ(selectCount, 1);
	EXPECT_EQ(deselectCount, 1);
	EXPECT_EQ(addCount, 1);
	EXPECT_EQ(removeCount, 1);
	EXPECT_EQ(camCount, 1);
}

// ---------------------------------------------------------------------------
// Complex event data
// ---------------------------------------------------------------------------

TEST_F(TypedEventBusTest, SceneObjectAdded_CarriesCorrectData)
{
	std::string receivedType;
	int receivedId = 0;

	m_pool->subscribe<SceneObjectAdded>(
		[&](const SceneObjectAdded& e) {
			receivedId = e.objectId;
			receivedType = e.typeName;
		});

	m_pool->enqueue(SceneObjectAdded{100, std::string("Mesh")});
	m_pool->Process();

	EXPECT_EQ(receivedId, 100);
	EXPECT_EQ(receivedType, "Mesh");
}

TEST_F(TypedEventBusTest, ActiveCameraChanged_MinusOneForNoCamera)
{
	int receivedId = 999;

	m_pool->subscribe<ActiveCameraChanged>(
		[&](const ActiveCameraChanged& e) { receivedId = e.cameraId; });

	m_pool->enqueue(ActiveCameraChanged{-1});
	m_pool->Process();

	EXPECT_EQ(receivedId, -1);
}

// ---------------------------------------------------------------------------
// Multiple emits, single Process
// ---------------------------------------------------------------------------

TEST_F(TypedEventBusTest, MultipleEmits_ProcessedInOrder)
{
	std::vector<int> receivedIds;

	m_pool->subscribe<ObjectSelected>(
		[&](const ObjectSelected& e) { receivedIds.push_back(e.objectId); });

	m_pool->enqueue(ObjectSelected{1});
	m_pool->enqueue(ObjectSelected{3});
	m_pool->enqueue(ObjectSelected{5});

	m_pool->Process();

	ASSERT_EQ(receivedIds.size(), 3);
	EXPECT_EQ(receivedIds[0], 1);
	EXPECT_EQ(receivedIds[1], 3);
	EXPECT_EQ(receivedIds[2], 5);
}

// ---------------------------------------------------------------------------
// Re-entrant emit (handler emits within Process)
// ---------------------------------------------------------------------------

TEST_F(TypedEventBusTest, ReentrantEmit_HandlerEmitsDuringProcess)
{
	int outerCount = 0;
	int innerCount = 0;

	m_pool->subscribe<ObjectSelected>([&](const ObjectSelected&) {
		outerCount++;
		// Emit a different event type from within the handler
		m_pool->enqueue(ObjectDeselected{outerCount});
	});

	m_pool->subscribe<ObjectDeselected>([&](const ObjectDeselected&) {
		innerCount++;
	});

	m_pool->enqueue(ObjectSelected{1});
	m_pool->Process();

	// Both should execute because re-entrant emits are deferred to end of queue
	EXPECT_EQ(outerCount, 1);
	EXPECT_EQ(innerCount, 1);
}

TEST_F(TypedEventBusTest, ReentrantEmit_SameTypeCreatesChain)
{
	int callCount = 0;

	m_pool->subscribe<ObjectSelected>([&](const ObjectSelected&) {
		callCount++;
		if (callCount < 5)
		{
			m_pool->enqueue(ObjectSelected{callCount});
		}
	});

	m_pool->enqueue(ObjectSelected{0});
	m_pool->Process();

	EXPECT_EQ(callCount, 5);
}

// ---------------------------------------------------------------------------
// maxEvents guard
// ---------------------------------------------------------------------------

TEST_F(TypedEventBusTest, Process_MaxEventsCapPreventsInfiniteLoop)
{
	// Handler that keeps re-emitting
	m_pool->subscribe<ObjectSelected>([&](const ObjectSelected&) {
		m_pool->enqueue(ObjectSelected{0});
	});

	m_pool->enqueue(ObjectSelected{0});

	// Should not hang - capped at maxEvents
	EXPECT_NO_THROW({ m_pool->Process(10); });
}

// ---------------------------------------------------------------------------
// EventPool is NOT copyable (compile-time check via static assert)
// ---------------------------------------------------------------------------

TEST_F(TypedEventBusTest, EventPoolIsNotCopyable)
{
	static_assert(!std::is_copy_constructible_v<EventPool>,
	              "EventPool must not be copyable");
	static_assert(!std::is_copy_assignable_v<EventPool>,
	              "EventPool must not be copy-assignable");
	SUCCEED();
}
