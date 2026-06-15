#pragma once

#include <QWidget>

namespace neurus {

/**
 * @brief A QWidget subclass that exposes a native Win32 window handle (HWND)
 *        for Vulkan surface creation via VK_KHR_win32_surface.
 *
 * Sets WA_NativeWindow and WA_PaintOnScreen attributes to ensure the widget
 * has a dedicated native window handle and Qt does not paint over the Vulkan
 * content. Overrides paintEngine() to return nullptr, preventing Qt's paint
 * system from drawing on this widget.
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
	 * @brief Override to disable Qt's paint engine.
	 * @return nullptr to prevent Qt from painting over Vulkan content.
	 *
	 * This widget is fully rendered by Vulkan; Qt must not draw on it.
	 */
	QPaintEngine* paintEngine() const override { return nullptr; }

	/**
	 * @brief Handles resize events and emits the resized signal.
	 * @param event The resize event containing new dimensions.
	 */
	void resizeEvent(QResizeEvent* event) override;
};

} // namespace neurus
