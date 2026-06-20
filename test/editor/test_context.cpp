#include <gtest/gtest.h>

#include <memory>

#include "editor/Context.h"
#include "editor/events/EventQueue.h"
#include "editor/events/EditorEvents.h"
#include "scene/Scene.h"
#include "scene/Camera.h"

using namespace neurus;

// ===========================================================================
// SceneContext tests
// ===========================================================================

class SceneContextTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_scene = std::make_unique<Scene>();
	}

	std::unique_ptr<Scene> m_scene;
};

TEST_F(SceneContextTest, UseScene_NullptrSafe)
{
	SceneContext sc;
	EXPECT_NO_THROW({ sc.UseScene(nullptr); });
	EXPECT_EQ(sc.GetActiveScene(), nullptr);
}

TEST_F(SceneContextTest, UseScene_StoresPointer)
{
	SceneContext sc;
	sc.UseScene(m_scene.get());
	EXPECT_EQ(sc.GetActiveScene(), m_scene.get());
}

TEST_F(SceneContextTest, GetActiveCamera_NoScene)
{
	SceneContext sc;
	EXPECT_EQ(sc.GetActiveCamera(), nullptr);
}

TEST_F(SceneContextTest, GetActiveCamera_EmptyScene)
{
	SceneContext sc;
	sc.UseScene(m_scene.get());
	// No cameras registered — returns nullptr
	EXPECT_EQ(sc.GetActiveCamera(), nullptr);
}

TEST_F(SceneContextTest, GetObjectID_NoScene)
{
	SceneContext sc;
	EXPECT_EQ(sc.GetObjectID(0), nullptr);
	EXPECT_EQ(sc.GetObjectID(42), nullptr);
}

TEST_F(SceneContextTest, GetObjectIDs_NoScene)
{
	SceneContext sc;
	auto objs = sc.GetObjectIDs();
	EXPECT_TRUE(objs.empty());
}

TEST_F(SceneContextTest, GetObjectIDs_EmptyScene)
{
	SceneContext sc;
	sc.UseScene(m_scene.get());
	auto objs = sc.GetObjectIDs();
	EXPECT_TRUE(objs.empty());
}

TEST_F(SceneContextTest, GetObjectID_ReturnsRegisteredObject)
{
	// Register a camera so there's something in obj_list
	auto cam = std::make_shared<Camera>();
	m_scene->UseCamera(cam);

	SceneContext sc;
	sc.UseScene(m_scene.get());

	int camId = cam->GetObjectID();
	const ObjectID* found = sc.GetObjectID(camId);
	EXPECT_NE(found, nullptr);
	EXPECT_EQ(found->GetObjectID(), camId);
}

TEST_F(SceneContextTest, GetObjectIDs_ReturnsRegisteredObjects)
{
	auto cam = std::make_shared<Camera>();
	m_scene->UseCamera(cam);

	SceneContext sc;
	sc.UseScene(m_scene.get());

	auto objs = sc.GetObjectIDs();
	EXPECT_EQ(objs.size(), 1u);
	EXPECT_EQ(objs[0]->GetObjectID(), cam->GetObjectID());
}

TEST_F(SceneContextTest, GetActiveCamera_ReturnsActiveCamera)
{
	auto cam = std::make_shared<Camera>();
	m_scene->UseCamera(cam);

	SceneContext sc;
	sc.UseScene(m_scene.get());

	const Camera* active = sc.GetActiveCamera();
	EXPECT_NE(active, nullptr);
	EXPECT_EQ(active->GetObjectID(), cam->GetObjectID());
}

// ===========================================================================
// EditorContext tests (backward-compatible signals + new features)
// ===========================================================================

class EditorContextRefactoredTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_editor = std::make_unique<EditorContext>();
	}

	std::unique_ptr<EditorContext> m_editor;
};

TEST_F(EditorContextRefactoredTest, Constructor_NoThrow)
{
	ASSERT_NO_THROW({
		EditorContext ctx;
	});
}

TEST_F(EditorContextRefactoredTest, IsQObject)
{
	EXPECT_NE(m_editor->metaObject(), nullptr);
}

TEST_F(EditorContextRefactoredTest, SceneChanged_SignalExists)
{
	bool wasCalled = false;

	QObject::connect(m_editor.get(), &EditorContext::sceneChanged,
	                 [&wasCalled]() { wasCalled = true; });

	emit m_editor->sceneChanged();

	EXPECT_TRUE(wasCalled);
}

TEST_F(EditorContextRefactoredTest, SelectionChanged_SignalExists)
{
	bool wasCalled = false;

	QObject::connect(m_editor.get(), &EditorContext::selectionChanged,
	                 [&wasCalled]() { wasCalled = true; });

	emit m_editor->selectionChanged();

	EXPECT_TRUE(wasCalled);
}

TEST_F(EditorContextRefactoredTest, SetScene_NullptrSafeWhenNoSceneCtx)
{
	// When EditorContext is created standalone (not via Context),
	// m_sceneCtx is nullptr. SetScene should not crash.
	EXPECT_NO_THROW({ m_editor->SetScene(nullptr); });
}

TEST_F(EditorContextRefactoredTest, activeScene_ReturnsNullWhenNoSceneCtx)
{
	EXPECT_EQ(m_editor->activeScene(), nullptr);
}

TEST_F(EditorContextRefactoredTest, DirtyTracking_InitiallyClean)
{
	EXPECT_FALSE(m_editor->IsDirty());
}

TEST_F(EditorContextRefactoredTest, DirtyTracking_MarkAndClear)
{
	m_editor->MarkDirty();
	EXPECT_TRUE(m_editor->IsDirty());

	m_editor->ClearDirty();
	EXPECT_FALSE(m_editor->IsDirty());
}

TEST_F(EditorContextRefactoredTest, SelectionManager_InitiallyEmpty)
{
	EXPECT_EQ(m_editor->selections.GetActiveObject(), nullptr);
	EXPECT_EQ(m_editor->selections.GetSelectedObjects(), nullptr);
	EXPECT_EQ(m_editor->selections.GetSelectionCount(), 0u);
}

TEST_F(EditorContextRefactoredTest, NotifySceneChanged_EmitsViaEventQueue)
{
	auto& pool = EventQueue();
	int receivedStatus = -1;

	pool.subscribe<SceneStatusChanged>(
		[&](const SceneStatusChanged& e) { receivedStatus = e.status; });

	m_editor->NotifySceneChanged(Scene::SceneChanged);

	pool.Process();

	EXPECT_EQ(receivedStatus, Scene::SceneChanged);
}

// ===========================================================================
// RenderContext tests
// ===========================================================================

class RenderContextTest : public ::testing::Test
{
};

TEST_F(RenderContextTest, DefaultNullConfig)
{
	RenderContext rc;
	EXPECT_EQ(rc.GetConfig(), nullptr);
}

TEST_F(RenderContextTest, UseConfig_StoresPointer)
{
	RenderContext rc;
	// RenderConfigs is a forward-declared stub — we test with a dummy int
	// cast to verify the pointer round-trip works.
	int dummy = 42;
	auto* fakeConfig = reinterpret_cast<RenderConfigs*>(&dummy);

	rc.UseConfig(fakeConfig);
	EXPECT_EQ(rc.GetConfig(), fakeConfig);
}

// ===========================================================================
// Context composite tests
// ===========================================================================

class ContextTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_queue = &EventQueue();
		m_scene = std::make_unique<Scene>();
		m_context = std::make_unique<Context>(*m_queue);
	}

	void TearDown() override
	{
		m_queue->Process();
	}

	EventQueue* m_queue = nullptr;
	std::unique_ptr<Scene> m_scene;
	std::unique_ptr<Context> m_context;
};

TEST_F(ContextTest, ConstructsWithoutCrash)
{
	ASSERT_NO_THROW({
		Context ctx(EventQueue());
	});
}

TEST_F(ContextTest, EditorContextBackPointerIsWired)
{
	// The Context constructor sets editor.m_sceneCtx = &scene
	// Verify by setting a scene and reading through EditorContext
	m_context->editor.SetScene(m_scene.get());
	EXPECT_EQ(m_context->scene.GetActiveScene(), m_scene.get());
	EXPECT_EQ(m_context->editor.activeScene(), m_scene.get());
}

TEST_F(ContextTest, NonCopyable)
{
	static_assert(!std::is_copy_constructible_v<Context>, "Context must not be copyable");
	static_assert(!std::is_copy_assignable_v<Context>, "Context must not be copy-assignable");
	SUCCEED();
}

TEST_F(ContextTest, SceneStatusChanged_UpdatesSceneAndEmitsSignal)
{
	bool signalCalled = false;
	QObject::connect(&m_context->editor, &EditorContext::sceneChanged,
	                 [&signalCalled]() { signalCalled = true; });

	// Enqueue a scene status change event — Context constructor subscribed to it
	m_queue->enqueue(SceneStatusChanged{Scene::LightChanged});
	m_queue->Process();

	// EditorContext::sceneChanged should have been emitted
	EXPECT_TRUE(signalCalled);
}

TEST_F(ContextTest, SceneStatusChanged_UpdatesSceneStatusFlag)
{
	// Set scene and reset status
	m_context->editor.SetScene(m_scene.get());
	m_scene->ResetStatus();

	m_queue->enqueue(SceneStatusChanged{Scene::ObjectTransChanged});
	m_queue->Process();

	// The Scene should now have ObjectTransChanged flag set
	EXPECT_TRUE(m_scene->CheckStatus(Scene::ObjectTransChanged));
}

TEST_F(ContextTest, SubContextsAccessible)
{
	// All three sub-contexts are public members
	EXPECT_EQ(&m_context->scene, &m_context->scene);  // sanity
	EXPECT_NE(m_context->editor.metaObject(), nullptr); // is QObject

	// RenderContext starts with null config
	EXPECT_EQ(m_context->render.GetConfig(), nullptr);
}

TEST_F(ContextTest, SetSceneThroughEditorPropagatesToSceneContext)
{
	m_context->editor.SetScene(m_scene.get());

	const Scene* fromSceneCtx = m_context->scene.GetActiveScene();
	const Scene* fromEditor = m_context->editor.activeScene();

	EXPECT_EQ(fromSceneCtx, m_scene.get());
	EXPECT_EQ(fromEditor, m_scene.get());
}
