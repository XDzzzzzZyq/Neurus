#include <gtest/gtest.h>

#include <QObject>
#include <QSignalSpy>

#include "editor/EditorContext.h"
#include "editor/EventBus.h"
#include "scene/Scene.h"

using namespace neurus;

/**
 * @brief Tests for SceneModifStatus propagation from EditorContext to EventBus.
 *
 * TDD: RED (test written first) → GREEN (after implementing signal + methods).
 * All tests are pure CPU — no GPU required.
 */
class SceneStatusTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_context = std::make_unique<EditorContext>();
		m_scene = std::make_unique<Scene>();
		m_bus = &EventBus::instance();
	}

	std::unique_ptr<EditorContext> m_context;
	std::unique_ptr<Scene> m_scene;
	EventBus* m_bus = nullptr;
};

// -----------------------------------------------------------------------
// NotifySceneChanged → EventBus::sceneStatusChanged propagation
// -----------------------------------------------------------------------

TEST_F(SceneStatusTest, NotifySceneChanged_EmitsViaEventBus)
{
	QSignalSpy spy(m_bus, &EventBus::sceneStatusChanged);

	// RED phase: expect no signals before triggering
	EXPECT_EQ(spy.count(), 0);

	m_context->NotifySceneChanged(Scene::SceneChanged);

	// GREEN phase: exactly one emission with correct status
	ASSERT_EQ(spy.count(), 1);
	auto args = spy.takeFirst();
	ASSERT_EQ(args.size(), 1);
	EXPECT_EQ(args.at(0).toInt(), Scene::SceneChanged);
}

TEST_F(SceneStatusTest, NotifySceneChanged_MultipleStatusValues)
{
	QSignalSpy spy(m_bus, &EventBus::sceneStatusChanged);

	m_context->NotifySceneChanged(Scene::ObjectTransChanged);
	m_context->NotifySceneChanged(
		Scene::LightChanged | Scene::CameraChanged);
	m_context->NotifySceneChanged(Scene::NoChanges);

	ASSERT_EQ(spy.count(), 3);
	EXPECT_EQ(spy.at(0).at(0).toInt(), Scene::ObjectTransChanged);
	EXPECT_EQ(
		spy.at(1).at(0).toInt(),
		Scene::LightChanged | Scene::CameraChanged);
	EXPECT_EQ(spy.at(2).at(0).toInt(), Scene::NoChanges);
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
// Isolated channel — only sceneStatusChanged fires
// -----------------------------------------------------------------------

TEST_F(SceneStatusTest, NoCrossContaminationWithOtherSignals)
{
	QSignalSpy sceneSpy(m_bus, &EventBus::sceneStatusChanged);
	QSignalSpy renderSpy(m_bus, &EventBus::renderRequested);

	m_context->NotifySceneChanged(Scene::MaterialChanged);

	// sceneStatusChanged should have fired
	ASSERT_EQ(sceneSpy.count(), 1);

	// renderRequested should NOT have fired
	EXPECT_EQ(renderSpy.count(), 0);
}
