#include <gtest/gtest.h>

#include <QObject>

#include "editor/EventBus.h"

using namespace neurus;

/**
 * @brief Tests for the EventBus singleton — no GPU required.
 */
class EventBusTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_bus = &EventBus::instance();
	}

	EventBus* m_bus = nullptr;
};

TEST_F(EventBusTest, Singleton_SameInstance)
{
	auto& bus1 = EventBus::instance();
	auto& bus2 = EventBus::instance();

	EXPECT_EQ(&bus1, &bus2);
}

TEST_F(EventBusTest, EmitRenderRequested_CallsConnectedSlot)
{
	bool wasCalled = false;

	QObject::connect(m_bus, &EventBus::renderRequested,
	                 [&wasCalled]() { wasCalled = true; });

	emit m_bus->renderRequested();

	EXPECT_TRUE(wasCalled);
}

TEST_F(EventBusTest, EmitWindowResized_CallsConnectedSlot)
{
	int receivedWidth = 0;
	int receivedHeight = 0;

	QObject::connect(m_bus, &EventBus::windowResized,
	                 [&receivedWidth, &receivedHeight](int w, int h) {
	                     receivedWidth = w;
	                     receivedHeight = h;
	                 });

	emit m_bus->windowResized(1920, 1080);

	EXPECT_EQ(receivedWidth, 1920);
	EXPECT_EQ(receivedHeight, 1080);
}

TEST_F(EventBusTest, EmitWindowResized_MultipleSubscribers)
{
	int callCount = 0;

	QObject::connect(m_bus, &EventBus::windowResized,
	                 [&callCount](int, int) { callCount++; });
	QObject::connect(m_bus, &EventBus::windowResized,
	                 [&callCount](int, int) { callCount++; });

	emit m_bus->windowResized(800, 600);

	EXPECT_EQ(callCount, 2);
}

TEST_F(EventBusTest, GpuName_DefaultEmpty)
{
	EXPECT_TRUE(m_bus->gpuName().isEmpty());
}

TEST_F(EventBusTest, GpuName_UpdateEmitsChanged)
{
	bool wasEmitted = false;
	QObject::connect(m_bus, &EventBus::gpuNameChanged,
	                 [&wasEmitted]() { wasEmitted = true; });

	m_bus->setGpuName("NVIDIA GeForce RTX 4090");

	EXPECT_EQ(m_bus->gpuName(), QString("NVIDIA GeForce RTX 4090"));
	EXPECT_TRUE(wasEmitted);
}

TEST_F(EventBusTest, EmitValidationMessage_CallsConnectedSlot)
{
	QString receivedSeverity;
	QString receivedMessage;

	QObject::connect(m_bus, &EventBus::validationMessage,
	                 [&receivedSeverity, &receivedMessage](QString severity, QString message) {
	                     receivedSeverity = severity;
	                     receivedMessage = message;
	                 });

	emit m_bus->validationMessage("error", "Test error message");

	EXPECT_EQ(receivedSeverity, QString("error"));
	EXPECT_EQ(receivedMessage, QString("Test error message"));
}

TEST_F(EventBusTest, EmitDeviceLost_CallsConnectedSlot)
{
	bool wasCalled = false;

	QObject::connect(m_bus, &EventBus::deviceLost,
	                 [&wasCalled]() { wasCalled = true; });

	emit m_bus->deviceLost();

	EXPECT_TRUE(wasCalled);
}

TEST_F(EventBusTest, MultipleSignals_IndependentChannels)
{
	int renderCount = 0;
	int resizeCount = 0;

	QObject::connect(m_bus, &EventBus::renderRequested,
	                 [&renderCount]() { renderCount++; });
	QObject::connect(m_bus, &EventBus::windowResized,
	                 [&resizeCount](int, int) { resizeCount++; });

	// Only emit renderRequested
	emit m_bus->renderRequested();
	emit m_bus->renderRequested();

	// Only resizeCount should NOT have changed
	EXPECT_EQ(renderCount, 2);
	EXPECT_EQ(resizeCount, 0);
}
