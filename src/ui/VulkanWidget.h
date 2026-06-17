#pragma once

#include <QWidget>

class QKeyEvent;

namespace neurus {

/**
 * @brief A QWidget subclass that exposes a native Win32 window handle (HWND)
 *        for Vulkan surface creation via VK_KHR_win32_surface.
 *
 * Sets WA_NativeWindow and WA_OpaquePaintEvent attributes to ensure the widget
 * has a dedicated native window handle and Qt does not draw a background behind
 * the Vulkan content. Overrides paintEvent() to be a no-op - all rendering is
 * handled by Vulkan.
 *
 * The widget is focusable (Qt::StrongFocus) to receive keyboard input.
 * Emits resized(int, int) when the widget is resized, allowing the renderer
 * to recreate the swapchain with the new dimensions.
 *
 * @note This class owns no Vulkan resources. It provides the HWND only;
 *       surface creation is the Renderer's responsibility.
 */
class VulkanWidget : public QWidget
{
	Q_OBJECT

public:
	/**
	 * @brief Constructs a VulkanWidget with native window attributes.
	 * @param parent Optional parent widget.
	 */
	explicit VulkanWidget(QWidget* parent = nullptr);

	/** @brief Default destructor. */
	~VulkanWidget() override;

	VulkanWidget(const VulkanWidget&) = delete;
	VulkanWidget& operator=(const VulkanWidget&) = delete;

	/**
	 * @brief Returns the native Win32 window handle (HWND).
	 * @return HWND of the underlying native window.
	 *
	 * The handle is obtained via winId() and is valid once the widget is
	 * shown or realize()d. Pass this to the Renderer for surface creation
	 * via vk::Win32SurfaceCreateInfoKHR.
	 */
	HWND hwnd() const { return reinterpret_cast<HWND>(winId()); }

Q_SIGNALS:
	/**
	 * @brief Emitted when the widget is resized.
	 * @param width New width in pixels.
	 * @param height New height in pixels.
	 *
	 * Connect this to the Renderer to trigger swapchain recreation with
	 * the updated dimensions.
	 */
	void resized(int width, int height);

protected:
	/**
	 * @brief Override to prevent Qt from drawing a background.
	 * @param event The paint event (ignored).
	 *
	 * All rendering for this widget is done by Vulkan. Qt's paint system
	 * should not draw anything in this widget's area.
	 */
	void paintEvent(QPaintEvent* event) override;

	/**
	 * @brief Handles resize events and emits the resized signal.
	 * @param event The resize event containing new dimensions.
	 */
	void resizeEvent(QResizeEvent* event) override;

	/**
	 * @brief Handles keyboard shortcuts for screenshot capture.
	 * @param event The key event.
	 *
	 * F12       → Emits UIEvents::screenshotRequested() via event system.
	 * Ctrl+F12  → Emits UIEvents::screenshotAllRequested() via event system.
	 * All other keys are passed to the base class.
	 */
	void keyPressEvent(QKeyEvent* event) override;
};

} // namespace neurus
