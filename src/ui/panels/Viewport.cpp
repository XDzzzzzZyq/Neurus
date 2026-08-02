#include "panels/Viewport.h"

#include "UIContext.h"
#include "core/Log.h"
#include "editor/events/UIEvents.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QStringList>
#include <QWheelEvent>

#include <algorithm>

namespace neurus {

Viewport::Viewport(QWidget* parent)
	: UIPanel(PanelType::Viewport, QString(), parent)
{
	// Ensure a dedicated native window handle exists for Vulkan surface creation
	setAttribute(Qt::WA_NativeWindow);

	// Tell Qt this widget paints all its pixels - no background fill needed.
	// Vulkan handles all rendering for this widget's area.
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_NoSystemBackground);
	setAutoFillBackground(false);

	// Enable keyboard focus for input handling
	setFocusPolicy(Qt::StrongFocus);

	// Enable mouse tracking so mouseMoveEvent fires even without a button held
	setMouseTracking(true);
}

Viewport::~Viewport() = default;

void Viewport::Refresh(const UIContext& ctx)
{
	// Viewport is event-driven: input forwarding and Vulkan rendering
	// are handled outside the Qt widget refresh cycle.

	// GPU profiling overlay: copy the latest profile and repaint only when
	// the numbers actually changed (profiling off => static/empty overlay).
	const auto* profile = static_cast<const FrameProfile*>(ctx.frameProfile);
	if (!profile)
		return;
	const bool changed = !m_hasProfile ||
		m_latestProfile.cpuRecordMs != profile->cpuRecordMs ||
		m_latestProfile.gpuTotalMs != profile->gpuTotalMs ||
		m_latestProfile.passCount != profile->passCount;
	m_latestProfile = *profile;
	m_hasProfile = true;
	if (changed)
		update();  // Repaint the overlay when a new profile arrives.
}

void Viewport::paintEvent(QPaintEvent* /*event*/)
{
	// No-op: all rendering is handled by Vulkan. This override prevents
	// Qt from drawing a default widget background behind the Vulkan content.

	// --- GPU profiling overlay (Debug menu, opt-in) ---
	// Drawn on top of the Vulkan viewport in the top-left corner.
	if (!m_hasProfile || m_latestProfile.passCount == 0)
		return;

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, false);

	QFont font("Consolas", 8);
	font.setStyleHint(QFont::Monospace);
	painter.setFont(font);

	QStringList lines;
	lines << "Neurus GPU Profiling";
	lines << QString("CPU record: %1 ms   GPU frame: %2 ms")
		.arg(m_latestProfile.cpuRecordMs, 0, 'f', 2)
		.arg((m_latestProfile.gpuTimingAvailable && m_latestProfile.gpuReady)
			? QString::number(m_latestProfile.gpuTotalMs, 'f', 2)
			: QString("--"));
	lines << QString("Passes: %1   Draws: %2   Dispatches: %3")
		.arg(m_latestProfile.passCount)
		.arg(m_latestProfile.drawCalls)
		.arg(m_latestProfile.dispatches);
	for (const auto& pass : m_latestProfile.passes)
	{
		QString line = QString("%1  CPU %2 ms")
			.arg(QString::fromStdString(pass.name), -28)
			.arg(pass.cpuMs, 0, 'f', 2);
		if (m_latestProfile.gpuTimingAvailable)
			line += QString("  GPU %1 ms").arg(pass.gpuMs, 0, 'f', 2);
		line += QString("  [%1 draw, %2 disp]").arg(pass.drawCalls).arg(pass.dispatches);
		lines << line;
	}

	int maxWidth = 0;
	for (const QString& line : lines)
		maxWidth = std::max(maxWidth, painter.fontMetrics().horizontalAdvance(line));

	const int margin = 8;
	const int lineHeight = painter.fontMetrics().height() + 2;
	const QRect panelRect(margin, margin,
		maxWidth + 2 * margin, lines.size() * lineHeight + 2 * margin);

	painter.fillRect(panelRect, QColor(20, 20, 20, 190));

	int y = margin + painter.fontMetrics().ascent() + 1;
	for (int i = 0; i < lines.size(); ++i)
	{
		painter.setPen(i == 0 ? QColor(130, 220, 130) : QColor(240, 240, 240));
		painter.drawText(margin + 4, y, lines[i]);
		y += lineHeight;
	}
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
		.position = Input::GetMousePos(static_cast<float>(pos.x()), static_cast<float>(pos.y())),
		.delta = Input::GetMousePos(static_cast<float>(pos.x() - m_lastPos.x()), static_cast<float>(pos.y() - m_lastPos.y())),
		.modifiers = Input::GetModifiers(static_cast<uint32_t>(event->modifiers().toInt())),
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
	const auto btn = Input::GetMouseButton(static_cast<uint32_t>(event->button()));
	switch (btn)
	{
		case Input::MouseButton::Left:   m_leftHeld = true; break;
		case Input::MouseButton::Right:  m_rightHeld = true; break;
		case Input::MouseButton::Middle: m_middleHeld = true; break;
	}
	const MousePressEvent evt{
		.button = btn,
		.position = Input::GetMousePos(static_cast<float>(event->position().x()), static_cast<float>(event->position().y())),
		.modifiers = Input::GetModifiers(static_cast<uint32_t>(event->modifiers().toInt()))
	};
	emit mousePressed(evt);
	QWidget::mousePressEvent(event);
}

void Viewport::mouseReleaseEvent(QMouseEvent* event)
{
	const auto btn = Input::GetMouseButton(static_cast<uint32_t>(event->button()));
	switch (btn)
	{
		case Input::MouseButton::Left:   m_leftHeld = false; break;
		case Input::MouseButton::Right:  m_rightHeld = false; break;
		case Input::MouseButton::Middle: m_middleHeld = false; break;
	}
	const MouseReleaseEvent evt{
		.button = btn,
		.position = Input::GetMousePos(static_cast<float>(event->position().x()), static_cast<float>(event->position().y())),
		.modifiers = Input::GetModifiers(static_cast<uint32_t>(event->modifiers().toInt()))
	};
	emit mouseReleased(evt);
	QWidget::mouseReleaseEvent(event);
}

void Viewport::wheelEvent(QWheelEvent* event)
{
	const float notches = event->angleDelta().y() / 120.0f;
	const MouseScrollEvent evt{
		.delta = notches,
		.position = Input::GetMousePos(static_cast<float>(event->position().x()), static_cast<float>(event->position().y())),
		.modifiers = Input::GetModifiers(static_cast<uint32_t>(event->modifiers().toInt())),
		.leftHeld = m_leftHeld,
		.middleHeld = m_middleHeld,
		.rightHeld = m_rightHeld
	};
	emit mouseScrolled(evt);
	QWidget::wheelEvent(event);
}

} // namespace neurus
