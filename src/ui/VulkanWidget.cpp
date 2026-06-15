#include "VulkanWidget.h"

#include <QResizeEvent>

namespace neurus {

VulkanWidget::VulkanWidget(QWidget* parent)
	: QWidget(parent)
{
	// Ensure a dedicated native window handle exists for Vulkan surface creation
	setAttribute(Qt::WA_NativeWindow);

	// Prevent Qt from painting on top of Vulkan-rendered content
	setAttribute(Qt::WA_PaintOnScreen);

	// Enable keyboard focus for input handling
	setFocusPolicy(Qt::StrongFocus);
}

VulkanWidget::~VulkanWidget() = default;

void VulkanWidget::resizeEvent(QResizeEvent* event)
{
	// Emit the resized signal with the new dimensions so the Renderer
	// can recreate the swapchain at the correct size.
	emit resized(event->size().width(), event->size().height());

	// Chain to base class for standard Qt resize handling.
	QWidget::resizeEvent(event);
}

} // namespace neurus
