#include <gtest/gtest.h>

#include <QObject>
#include <QSignalSpy>

#include "editor/EventBus.h"

using namespace neurus;

/**
 * @brief Tests for the 7 new editor-specific EventBus signals — no GPU required.
 *
 * RED phase: this file will NOT compile until the signals exist in EventBus.h.
 * GREEN phase: after adding signals, all tests pass.
 */
class EventBusExpandedTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_bus = &EventBus::instance();
	}

	EventBus* m_bus = nullptr;
};

// --- objectSelected(int objectId) ---

TEST_F(EventBusExpandedTest, ObjectSelected_EmitReceivesCorrectId)
{
	QSignalSpy spy(m_bus, &EventBus::objectSelected);

	emit m_bus->objectSelected(42);

	ASSERT_EQ(spy.count(), 1);
	auto args = spy.takeFirst();
	ASSERT_EQ(args.size(), 1);
	EXPECT_EQ(args.at(0).toInt(), 42);
}

TEST_F(EventBusExpandedTest, ObjectSelected_MultipleEmits)
{
	QSignalSpy spy(m_bus, &EventBus::objectSelected);

	emit m_bus->objectSelected(1);
	emit m_bus->objectSelected(2);
	emit m_bus->objectSelected(99);

	ASSERT_EQ(spy.count(), 3);
	EXPECT_EQ(spy.at(0).at(0).toInt(), 1);
	EXPECT_EQ(spy.at(1).at(0).toInt(), 2);
	EXPECT_EQ(spy.at(2).at(0).toInt(), 99);
}

// --- objectDeselected(int objectId) ---

TEST_F(EventBusExpandedTest, ObjectDeselected_EmitReceivesCorrectId)
{
	QSignalSpy spy(m_bus, &EventBus::objectDeselected);

	emit m_bus->objectDeselected(7);

	ASSERT_EQ(spy.count(), 1);
	auto args = spy.takeFirst();
	ASSERT_EQ(args.size(), 1);
	EXPECT_EQ(args.at(0).toInt(), 7);
}

TEST_F(EventBusExpandedTest, ObjectDeselected_NoCrossContamination)
{
	QSignalSpy selectSpy(m_bus, &EventBus::objectSelected);
	QSignalSpy deselectSpy(m_bus, &EventBus::objectDeselected);

	emit m_bus->objectDeselected(7);

	EXPECT_EQ(selectSpy.count(), 0);
	EXPECT_EQ(deselectSpy.count(), 1);
}

// --- sceneObjectAdded(int objectId, QString typeName) ---

TEST_F(EventBusExpandedTest, SceneObjectAdded_EmitReceivesCorrectData)
{
	QSignalSpy spy(m_bus, &EventBus::sceneObjectAdded);

	emit m_bus->sceneObjectAdded(100, QString("Mesh"));

	ASSERT_EQ(spy.count(), 1);
	auto args = spy.takeFirst();
	ASSERT_EQ(args.size(), 2);
	EXPECT_EQ(args.at(0).toInt(), 100);
	EXPECT_EQ(args.at(1).toString(), QString("Mesh"));
}

// --- sceneObjectRemoved(int objectId) ---

TEST_F(EventBusExpandedTest, SceneObjectRemoved_EmitReceivesCorrectId)
{
	QSignalSpy spy(m_bus, &EventBus::sceneObjectRemoved);

	emit m_bus->sceneObjectRemoved(200);

	ASSERT_EQ(spy.count(), 1);
	auto args = spy.takeFirst();
	ASSERT_EQ(args.size(), 1);
	EXPECT_EQ(args.at(0).toInt(), 200);
}

// --- activeCameraChanged(int cameraId) ---

TEST_F(EventBusExpandedTest, ActiveCameraChanged_EmitReceivesCorrectId)
{
	QSignalSpy spy(m_bus, &EventBus::activeCameraChanged);

	emit m_bus->activeCameraChanged(3);

	ASSERT_EQ(spy.count(), 1);
	auto args = spy.takeFirst();
	ASSERT_EQ(args.size(), 1);
	EXPECT_EQ(args.at(0).toInt(), 3);
}

TEST_F(EventBusExpandedTest, ActiveCameraChanged_MinusOneForNoCamera)
{
	QSignalSpy spy(m_bus, &EventBus::activeCameraChanged);

	emit m_bus->activeCameraChanged(-1);

	ASSERT_EQ(spy.count(), 1);
	auto args = spy.takeFirst();
	EXPECT_EQ(args.at(0).toInt(), -1);
}

// --- renderConfigChanged() ---

TEST_F(EventBusExpandedTest, RenderConfigChanged_EmitTriggersSlot)
{
	QSignalSpy spy(m_bus, &EventBus::renderConfigChanged);

	emit m_bus->renderConfigChanged();

	ASSERT_EQ(spy.count(), 1);
	// No parameters to verify — just emission
}

TEST_F(EventBusExpandedTest, RenderConfigChanged_MultipleEmits)
{
	QSignalSpy spy(m_bus, &EventBus::renderConfigChanged);

	emit m_bus->renderConfigChanged();
	emit m_bus->renderConfigChanged();
	emit m_bus->renderConfigChanged();

	EXPECT_EQ(spy.count(), 3);
}

// --- viewportResized(int width, int height) ---

TEST_F(EventBusExpandedTest, ViewportResized_EmitReceivesCorrectDimensions)
{
	QSignalSpy spy(m_bus, &EventBus::viewportResized);

	emit m_bus->viewportResized(640, 480);

	ASSERT_EQ(spy.count(), 1);
	auto args = spy.takeFirst();
	ASSERT_EQ(args.size(), 2);
	EXPECT_EQ(args.at(0).toInt(), 640);
	EXPECT_EQ(args.at(1).toInt(), 480);
}

TEST_F(EventBusExpandedTest, ViewportResized_ZeroDimensions)
{
	QSignalSpy spy(m_bus, &EventBus::viewportResized);

	emit m_bus->viewportResized(0, 0);

	ASSERT_EQ(spy.count(), 1);
	auto args = spy.takeFirst();
	EXPECT_EQ(args.at(0).toInt(), 0);
	EXPECT_EQ(args.at(1).toInt(), 0);
}

// --- All signals independent channels ---

TEST_F(EventBusExpandedTest, AllNewSignals_IndependentChannels)
{
	QSignalSpy selectSpy(m_bus, &EventBus::objectSelected);
	QSignalSpy deselectSpy(m_bus, &EventBus::objectDeselected);
	QSignalSpy addSpy(m_bus, &EventBus::sceneObjectAdded);
	QSignalSpy removeSpy(m_bus, &EventBus::sceneObjectRemoved);
	QSignalSpy camSpy(m_bus, &EventBus::activeCameraChanged);
	QSignalSpy configSpy(m_bus, &EventBus::renderConfigChanged);
	QSignalSpy resizeSpy(m_bus, &EventBus::viewportResized);

	// Emit each signal exactly once
	emit m_bus->objectSelected(1);
	emit m_bus->objectDeselected(2);
	emit m_bus->sceneObjectAdded(3, "Light");
	emit m_bus->sceneObjectRemoved(4);
	emit m_bus->activeCameraChanged(5);
	emit m_bus->renderConfigChanged();
	emit m_bus->viewportResized(800, 600);

	EXPECT_EQ(selectSpy.count(), 1);
	EXPECT_EQ(deselectSpy.count(), 1);
	EXPECT_EQ(addSpy.count(), 1);
	EXPECT_EQ(removeSpy.count(), 1);
	EXPECT_EQ(camSpy.count(), 1);
	EXPECT_EQ(configSpy.count(), 1);
	EXPECT_EQ(resizeSpy.count(), 1);
}
