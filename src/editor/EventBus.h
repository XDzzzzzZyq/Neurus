#pragma once

#include <QObject>
#include <QString>

namespace neurus {

/**
 * @brief Singleton EventBus for cross-layer communication.
 *
 * Uses Qt's signal/slot mechanism for type-safe, decoupled message dispatch.
 * Exposed to QML via context property for UI-driven events.
 *
 * Usage:
 *   auto& bus = EventBus::instance();
 *   QObject::connect(&bus, &EventBus::renderRequested, &myRenderer, &Renderer::DrawFrame);
 */
class EventBus : public QObject
{
	Q_OBJECT

public:
	/**
	 * @brief Returns the singleton EventBus instance.
	 * @note Thread-safe after first call (QML requires main-thread usage for MVP).
	 */
	static EventBus& instance();

	// Prevent copies
	EventBus(const EventBus&) = delete;
	EventBus& operator=(const EventBus&) = delete;

	// Q_PROPERTY for QML access
	Q_PROPERTY(QString gpuName READ gpuName WRITE setGpuName NOTIFY gpuNameChanged)

	QString gpuName() const;
	void setGpuName(const QString& name);

signals:
	/** @brief Emitted each frame to trigger rendering. Connected to Renderer::DrawFrame(). */
	void renderRequested();

	/** @brief Emitted when the application window is resized.
	 *  @param width New window width in pixels.
	 *  @param height New window height in pixels. */
	void windowResized(int width, int height);

	/** @brief Emitted when the Vulkan device is lost. Application should attempt cleanup. */
	void deviceLost();

	/** @brief Emitted by Vulkan validation layers with diagnostic messages.
	 *  @param severity "error", "warning", "info", or "verbose".
	 *  @param message Human-readable validation message. */
	void validationMessage(QString severity, QString message);

	/** @brief Emitted when GPU name changes (e.g., after device selection). */
	void gpuNameChanged();

private:
	EventBus() = default;

	QString m_gpuName;
};

} // namespace neurus
