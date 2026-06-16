#include <gtest/gtest.h>

#include <memory>

#include "editor/EditorContext.h"
#include "editor/events/EventBus.h"
#include "editor/events/EditorEvents.h"
#include "scene/Scene.h"

using namespace neurus;

/**
 * @brief Tests for SceneStatusChanged propagation from EditorContext
 *        to the typed EventBus (EventPool).
 *
 * TDD: RED (test written first) → GREEN (after implementing method).
 * All tests are pure CPU - no GPU required.
 */
class SceneStatusTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_context = std::make_unique<EditorContext>();
		m_scene = std::make_unique<Scene>();
		m_pool = &EventBus();
	}

	void TearDown() override
	{
		// Process any remaining events
		m_pool->Process();
	}

	std::unique_ptr<EditorContext> m_context;
	std::unique_ptr<Scene> m_scene;
	EventPool* m_pool = nullptr;
};

// -----------------------------------------------------------------------
// NotifySceneChanged → EventPool::emit(SceneStatusChanged{...})
// -----------------------------------------------------------------------

TEST_F(SceneStatusTest, NotifySceneChanged_EmitsViaEventBus)
{
	int receivedStatus = -1;

	m_pool->subscribe<SceneStatusChanged>(
		[&](const SceneStatusChanged& e) { receivedStatus = e.status; });

	m_context->NotifySceneChanged(Scene::SceneChanged);

	// Event is enqueued, not yet dispatched
	m_pool->Process();

	EXPECT_EQ(receivedStatus, Scene::SceneChanged);
}

TEST_F(SceneStatusTest, NotifySceneChanged_MultipleStatusValues)
{
	std::vector<int> receivedStatuses;

	m_pool->subscribe<SceneStatusChanged>(
		[&](const SceneStatusChanged& e) { receivedStatuses.push_back(e.status); });

	m_context->NotifySceneChanged(Scene::ObjectTransChanged);
	m_context->NotifySceneChanged(Scene::LightChanged | Scene::CameraChanged);
	m_context->NotifySceneChanged(Scene::NoChanges);

	m_pool->Process();

	ASSERT_EQ(receivedStatuses.size(), 3);
	EXPECT_EQ(receivedStatuses[0], Scene::ObjectTransChanged);
	EXPECT_EQ(receivedStatuses[1], Scene::LightChanged | Scene::CameraChanged);
	EXPECT_EQ(receivedStatuses[2], Scene::NoChanges);
}

// -----------------------------------------------------------------------
// SetScene stores pointer (no-op for now, verify no crash)
// -----------------------------------------------------------------------

TEST_F(SceneStatusTest, SetScene_StoresPointer)
{
	EXPECT_NO_THROW({ m_context->SetScene(m_scene.get()); });
}

TEST_F(SceneStatusTest, SetScene_Nullptr)
{
	EXPECT_NO_THROW({ m_context->SetScene(nullptr); });
}

// -----------------------------------------------------------------------
// Isolated channel - only SceneStatusChanged fires
// -----------------------------------------------------------------------

TEST_F(SceneStatusTest, NoCrossContaminationWithOtherSignals)
{
	int sceneStatusCount = 0;
	int objectSelectedCount = 0;

	m_pool->subscribe<SceneStatusChanged>([&](const SceneStatusChanged&) { sceneStatusCount++; });
	m_pool->subscribe<ObjectSelected>([&](const ObjectSelected&) { objectSelectedCount++; });

	m_context->NotifySceneChanged(Scene::MaterialChanged);

	m_pool->Process();

	// SceneStatusChanged should have fired
	EXPECT_EQ(sceneStatusCount, 1);
	// ObjectSelected should NOT have fired
	EXPECT_EQ(objectSelectedCount, 0);
}
