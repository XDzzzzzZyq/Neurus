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

	// --- Editor-specific signals (T5) ---

	/** @brief Emitted when a scene object is selected by the user.
	 *  @param objectId Unique identifier of the selected object. */
	void objectSelected(int objectId);

	/** @brief Emitted when a scene object is deselected.
	 *  @param objectId Unique identifier of the deselected object. */
	void objectDeselected(int objectId);

	/** @brief Emitted when a new scene object is created.
	 *  @param objectId Unique identifier of the new object.
	 *  @param typeName Human-readable type name (e.g. "Mesh", "Light"). */
	void sceneObjectAdded(int objectId, QString typeName);

	/** @brief Emitted when a scene object is removed/deleted.
	 *  @param objectId Unique identifier of the removed object. */
	void sceneObjectRemoved(int objectId);

	/** @brief Emitted when the active scene camera is switched.
	 *  @param cameraId Unique identifier of the active camera (-1 if no camera). */
	void activeCameraChanged(int cameraId);

	/** @brief Emitted when the render configuration is changed (toggled, slider, etc.). */
	void renderConfigChanged();

	/** @brief Emitted when the viewport widget is resized.
	 *  @param width New viewport width in pixels.
	 *  @param height New viewport height in pixels. */
	void viewportResized(int width, int height);

private:
	EventBus() = default;

	QString m_gpuName;
};

} // namespace neurus
