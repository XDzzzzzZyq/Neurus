#include "panels/Viewport.h"

#include "UIContext.h"
#include "core/Log.h"
#include "editor/events/UIEvents.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>

namespace neurus {

Viewport::Viewport(QWidget* parent)
	: UIPanel(PanelType::Viewport, QString(), parent)
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

void Viewport::Refresh(const UIContext& /*ctx*/)
{
	// Viewport is event-driven — input forwarding and Vulkan rendering
	// are handled outside the Qt widget refresh cycle.
}

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
	QWidget::keyReleaseEvent(event);
}

void Viewport::mouseMoveEvent(QMouseEvent* event)
{
	const QPointF pos = event->position();
	const MouseMoveEvent evt{
		.position = Input::GetMousePos(pos),
		.delta = Input::GetMousePos(pos - m_lastPos),
		.modifiers = Input::GetModifiers(event->modifiers()),
		.leftHeld = m_leftHeld,
		.middleHeld = m_middleHeld,
		.rightHeld = m_rightHeld
	};
	m_lastPos = pos;
	emit mouseMoved(evt);
	QWidget::mouseMoveEvent(event);
}

void Viewport::mousePressEvent(QMouseEvent* event)
{
	m_lastPos = event->position();  // Reset delta on press
	const auto btn = Input::GetMouseButton(event->button());
	switch (btn)
	{
		case Input::MouseButton::Left:   m_leftHeld = true; break;
		case Input::MouseButton::Right:  m_rightHeld = true; break;
		case Input::MouseButton::Middle: m_middleHeld = true; break;
	}
	const MousePressEvent evt{
		.button = btn,
		.position = Input::GetMousePos(event->position()),
		.modifiers = Input::GetModifiers(event->modifiers())
	};
	emit mousePressed(evt);
	QWidget::mousePressEvent(event);
}

void Viewport::mouseReleaseEvent(QMouseEvent* event)
{
	const auto btn = Input::GetMouseButton(event->button());
	switch (btn)
	{
		case Input::MouseButton::Left:   m_leftHeld = false; break;
		case Input::MouseButton::Right:  m_rightHeld = false; break;
		case Input::MouseButton::Middle: m_middleHeld = false; break;
	}
	const MouseReleaseEvent evt{
		.button = btn,
		.position = Input::GetMousePos(event->position()),
		.modifiers = Input::GetModifiers(event->modifiers())
	};
	emit mouseReleased(evt);
	QWidget::mouseReleaseEvent(event);
}

void Viewport::wheelEvent(QWheelEvent* event)
{
	const float notches = event->angleDelta().y() / 120.0f;
	const MouseScrollEvent evt{
		.delta = notches,
		.position = Input::GetMousePos(event->position()),
		.modifiers = Input::GetModifiers(event->modifiers()),
		.leftHeld = m_leftHeld,
		.middleHeld = m_middleHeld,
		.rightHeld = m_rightHeld
	};
	emit mouseScrolled(evt);
	QWidget::wheelEvent(event);
}

} // namespace neurus
