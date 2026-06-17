#pragma once

#include <QObject>
#include <QString>

namespace neurus {

/**
 * @brief Singleton Qt signal bus for UI Layer ↔ Editor Layer communication.
 *
 * Owns signals related to window lifecycle, rendering triggers, and Vulkan device
 * status. These signals are Qt-dependent by design - the UI layer (QWindow,
 * QTimer, Qt event loop) emits them, and Editor/Renderer slots consume them.
 *
 * For Editor ↔ Renderer typed events (no Qt dependency), see EventBus.h.
 *
 * Usage:
 *   auto& ui = UIEvents::instance();
 *   QObject::connect(&ui, &UIEvents::renderRequested, &myRenderer, &Renderer::DrawFrame);
 */
class UIEvents : public QObject
{
	Q_OBJECT

public:
	/**
	 * @brief Returns the singleton UIEvents instance.
	 * @note Thread-safe after first call (QML requires main-thread usage for MVP).
	 */
	static UIEvents& instance();

	// Prevent copies
	UIEvents(const UIEvents&) = delete;
	UIEvents& operator=(const UIEvents&) = delete;

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

	// --- UI-driven editor signals ---

	/** @brief Emitted when the render configuration is changed (toggled, slider, etc.). */
	void renderConfigChanged();

	/** @brief Emitted when the viewport widget is resized.
	 *  @param width New viewport width in pixels.
	 *  @param height New viewport height in pixels. */
	void viewportResized(int width, int height);

	// --- Screenshot signals ---

	/** @brief Emitted when a screenshot is requested (F12 or menu).
	 *  Capture the current swapchain image to a PNG file. */
	void screenshotRequested();

	/** @brief Emitted when a full attachment dump is requested (Ctrl+F12).
	 *  Capture all G-Buffer attachments to individual PNG files. */
	void screenshotAllRequested();

public:
	/**
	 * @brief Convenience method to emit screenshotRequested from any layer.
	 * @note Prefer this over direct signal emission from outside the class.
	 */
	void requestScreenshot() { emit screenshotRequested(); }

	/**
	 * @brief Convenience method to emit screenshotAllRequested from any layer.
	 */
	void requestScreenshotAll() { emit screenshotAllRequested(); }

private:
	UIEvents() = default;

	QString m_gpuName;
};

} // namespace neurus
