#include "panels/Viewport.h"

#include "core/Log.h"
#include "editor/Input.h"
#include "editor/events/UIEvents.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>

namespace neurus {

Viewport::Viewport(QWidget* parent)
	: UIPanel(parent)
{
	// Ensure a dedicated native window handle exists for Vulkan surface creation
	setAttribute(Qt::WA_NativeWindow);

	// Tell Qt this widget paints all its pixels - no background fill needed.
	// Vulkan handles all rendering for this widget's area.
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAutoFillBackground(false);

	// Enable keyboard focus for input handling
	setFocusPolicy(Qt::StrongFocus);

	// Enable mouse tracking so mouseMoveEvent fires even without a button held
	setMouseTracking(true);
}

Viewport::~Viewport() = default;

void Viewport::paintEvent(QPaintEvent* /*event*/)
{
	// No-op: all rendering is handled by Vulkan. This override prevents
	// Qt from drawing a default widget background behind the Vulkan content.
}

void Viewport::resizeEvent(QResizeEvent* event)
{
	// Emit the resized signal with the new dimensions so the Renderer
	// can recreate the swapchain at the correct size.
	emit resized(event->size().width(), event->size().height());

	// Chain to base class for standard Qt resize handling.
	QWidget::resizeEvent(event);
}

void Viewport::keyPressEvent(QKeyEvent* event)
{
	// Forward all key presses to the Input system for per-frame querying
	Input::RecordKeyPress(event->key());

	if (event->key() == Qt::Key_F12)
	{
		if (event->modifiers() == Qt::NoModifier)
		{
			// F12: capture swapchain screenshot via event system
			NEURUS_LOG("[Screenshot] F12 pressed - requesting screenshot via UIEvents");
			UIEvents::instance().requestScreenshot();
			return;
		}
		else if (event->modifiers() == Qt::ControlModifier)
		{
			// Ctrl+F12: dump all G-Buffer attachments via event system
			NEURUS_LOG("[Screenshot] Ctrl+F12 pressed - requesting attachment dump via UIEvents");
			UIEvents::instance().requestScreenshotAll();
			return;
		}
	}

	// Pass all other keys to the base class.
	QWidget::keyPressEvent(event);
}

void Viewport::keyReleaseEvent(QKeyEvent* event)
{
	Input::RecordKeyRelease(event->key());
	QWidget::keyReleaseEvent(event);
}

void Viewport::mouseMoveEvent(QMouseEvent* event)
{
	const QPointF pos = event->position();
	Input::RecordMouseMove(static_cast<float>(pos.x()), static_cast<float>(pos.y()));
	QWidget::mouseMoveEvent(event);
}

void Viewport::mousePressEvent(QMouseEvent* event)
{
	const auto button = event->button();
	if (button == Qt::LeftButton)
		Input::RecordMousePress(Input::MouseButton::Left);
	else if (button == Qt::RightButton)
		Input::RecordMousePress(Input::MouseButton::Right);
	else if (button == Qt::MiddleButton)
		Input::RecordMousePress(Input::MouseButton::Middle);

	QWidget::mousePressEvent(event);
}

void Viewport::mouseReleaseEvent(QMouseEvent* event)
{
	const auto button = event->button();
	if (button == Qt::LeftButton)
		Input::RecordMouseRelease(Input::MouseButton::Left);
	else if (button == Qt::RightButton)
		Input::RecordMouseRelease(Input::MouseButton::Right);
	else if (button == Qt::MiddleButton)
		Input::RecordMouseRelease(Input::MouseButton::Middle);

	QWidget::mouseReleaseEvent(event);
}

void Viewport::wheelEvent(QWheelEvent* event)
{
	// angleDelta().y() is typically ±120 per notch → divide by 120 for
	// notches, then by 8 for the common "lines per notch" factor ≈ ±1.
	const float notches = event->angleDelta().y() / 120.0f;
	Input::RecordScroll(notches);
	QWidget::wheelEvent(event);
}

} // namespace neurus
