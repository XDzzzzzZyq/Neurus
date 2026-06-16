#include <gtest/gtest.h>

#include "editor/EditorContext.h"

using namespace neurus;

/**
 * @brief Tests for EditorContext - no GPU required.
 */
class EditorContextTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_context = std::make_unique<EditorContext>();
	}

	std::unique_ptr<EditorContext> m_context;
};

TEST_F(EditorContextTest, Constructor_NoThrow)
{
	ASSERT_NO_THROW({
		EditorContext ctx;
	});
}

TEST_F(EditorContextTest, IsQObject)
{
	// EditorContext inherits QObject - verify it has a valid meta-object
	EXPECT_NE(m_context->metaObject(), nullptr);
}

TEST_F(EditorContextTest, SceneChanged_SignalExists)
{
	bool wasCalled = false;

	QObject::connect(m_context.get(), &EditorContext::sceneChanged,
	                 [&wasCalled]() { wasCalled = true; });

	emit m_context->sceneChanged();

	EXPECT_TRUE(wasCalled);
}

TEST_F(EditorContextTest, SelectionChanged_SignalExists)
{
	bool wasCalled = false;

	QObject::connect(m_context.get(), &EditorContext::selectionChanged,
	                 [&wasCalled]() { wasCalled = true; });

	emit m_context->selectionChanged();

	EXPECT_TRUE(wasCalled);
}
