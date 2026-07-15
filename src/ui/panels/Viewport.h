#pragma once

#include "UIPanel.h"

#include <glm/glm.hpp>

#include "editor/events/InputEvents.h"

class QKeyEvent;
class QMouseEvent;
class QWheelEvent;

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
 * Mouse events are translated to typed event structs and emitted as Qt signals
 * (mouseMoved, mousePressed, mouseReleased, mouseScrolled). The Viewport tracks
 * its own button state and mouse position internally; it no longer calls
 * Input::Record*().
 *
 * @note This class owns no Vulkan resources. It provides the HWND only;
 *       surface creation is the Renderer's responsibility.
 */
class Viewport : public UIPanel
{
	Q_OBJECT

public:
	static constexpr PanelType kType = PanelType::Viewport;

	/**
	 * @brief Constructs a Viewport with native window attributes.
	 * @param parent Optional parent widget.
	 */
	explicit Viewport(QWidget* parent = nullptr);

	/**
	 * @brief Refreshes the panel from a UIContext snapshot.
	 *
	 * Currently a no-op - the Viewport is event-driven (input forwarding)
	 * and renders via Vulkan outside the Qt widget hierarchy.
	 *
	 * @param ctx Read-only UI context (unused).
	 */
	void Refresh(const UIContext& ctx) override;

	/** @brief Default destructor. */
	~Viewport() override;

	Viewport(const Viewport&) = delete;
	Viewport& operator=(const Viewport&) = delete;

	/**
	 * @brief Returns the native Win32 window handle (HWND).
	 * @return HWND of the underlying native window.
	 *
	 * The handle is obtained via winId() and is valid once the widget is
	 * shown or realize()d. Pass this to the Renderer for surface creation
	 * via vk::Win32SurfaceCreateInfoKHR.
	 */
	HWND hwnd() const { return reinterpret_cast<HWND>(winId()); }

signals:
	/**
	 * @brief Emitted when the widget is resized.
	 * @param width New width in pixels.
	 * @param height New height in pixels.
	 *
	 * Connect this to the Renderer to trigger swapchain recreation with
	 * the updated dimensions.
	 */
	void resized(int width, int height);

	/**
	 * @brief Emitted when the mouse cursor moves over the viewport.
	 * @param event MouseMoveEvent with position, delta, modifiers, and button state.
	 */
	void mouseMoved(const neurus::MouseMoveEvent& event);

	/**
	 * @brief Emitted when a mouse button is pressed inside the viewport.
	 * @param event MousePressEvent with button, position, and modifiers.
	 */
	void mousePressed(const neurus::MousePressEvent& event);

	/**
	 * @brief Emitted when a mouse button is released inside the viewport.
	 * @param event MouseReleaseEvent with button, position, and modifiers.
	 */
	void mouseReleased(const neurus::MouseReleaseEvent& event);

	/**
	 * @brief Emitted when the mouse wheel is scrolled over the viewport.
	 * @param event MouseScrollEvent with delta, position, modifiers, and button state.
	 */
	void mouseScrolled(const neurus::MouseScrollEvent& event);

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
	 * @brief Handles keyboard input.
	 * @param event The key event.
	 *
	 * F12 triggers screenshot via UIEvents; Ctrl+F12 triggers attachment
	 * dump. All other keys chain to the base class. No longer calls
	 * Input::RecordKeyPress().
	 */
	void keyPressEvent(QKeyEvent* event) override;

	/**
	 * @brief Handles key release events.
	 * @param event The key event.
	 *
	 * Chains to the base class. No longer calls Input::RecordKeyRelease().
	 */
	void keyReleaseEvent(QKeyEvent* event) override;

	/**
	 * @brief Handles mouse movement and emits the mouseMoved signal.
	 * @param event The mouse event.
	 *
	 * Computes delta from the last recorded position, translates Qt types
	 * via Input helpers, builds a MouseMoveEvent, and emits mouseMoved().
	 * Mouse tracking is enabled so movement is captured even without a
	 * button held.
	 */
	void mouseMoveEvent(QMouseEvent* event) override;

	/**
	 * @brief Handles mouse button presses and emits the mousePressed signal.
	 * @param event The mouse event.
	 *
	 * Resets m_lastPos to current position on press (zero delta for the
	 * next move), updates internal button state, translates via Input
	 * helpers, and emits mousePressed().
	 */
	void mousePressEvent(QMouseEvent* event) override;

	/**
	 * @brief Handles mouse button releases and emits the mouseReleased signal.
	 * @param event The mouse event.
	 *
	 * Clears internal button state, translates via Input helpers, and
	 * emits mouseReleased().
	 */
	void mouseReleaseEvent(QMouseEvent* event) override;

	/**
	 * @brief Handles mouse wheel scrolling and emits the mouseScrolled signal.
	 * @param event The wheel event.
	 *
	 * Converts angleDelta to notches (~+/-1 per detent), translates via
	 * Input helpers, and emits mouseScrolled().
	 */
	void wheelEvent(QWheelEvent* event) override;

private:
	QPointF m_lastPos;        ///< Last mouse position for delta computation.
	bool    m_leftHeld = false;   ///< Left mouse button held.
	bool    m_middleHeld = false; ///< Middle mouse button held.
	bool    m_rightHeld = false;  ///< Right mouse button held.
};

} // namespace neurus
