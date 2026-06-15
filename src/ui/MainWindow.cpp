#include "MainWindow.h"
#include "editor/EventBus.h"

#include <QEvent>
#include <QResizeEvent>

// Windows platform headers for HWND
#define NOMINMAX
#include <windows.h>
#include <vulkan/vulkan_win32.h>

#include <stdexcept>

namespace neurus {

MainWindow::MainWindow(const vk::raii::Instance& vulkanInstance,
                       EventBus* bus,
                       int width, int height,
                       const QString& title,
                       QObject* parent)
	: QObject(parent)
	, m_bus(bus)
	, m_width(width)
	, m_height(height)
	, m_title(title)
{
	// --- Create window ---
	m_window = std::make_unique<QWindow>();
	m_window->setSurfaceType(QSurface::VulkanSurface);
	m_window->setTitle(m_title);
	m_window->resize(m_width, m_height);

	// --- Create VkSurfaceKHR from QWindow's HWND ---
	HWND hwnd = reinterpret_cast<HWND>(m_window->winId());

	if (!hwnd)
	{
		throw std::runtime_error("Failed to get HWND from QWindow.");
	}

	HINSTANCE hinstance = GetModuleHandle(nullptr);

	vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo(
		{}, hinstance, hwnd
	);

	m_surface = std::make_unique<vk::raii::SurfaceKHR>(vulkanInstance, surfaceCreateInfo);

	// --- Install resize event filter ---
	m_window->installEventFilter(this);

	// --- Show window ---
	m_window->show();
}

MainWindow::~MainWindow()
{
	if (m_window)
	{
		m_window->removeEventFilter(this);
		m_window->close();
	}

	// vk::raii cleans up m_surface automatically
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
	if (obj == m_window.get() && event->type() == QEvent::Resize)
	{
		auto* resizeEvent = static_cast<QResizeEvent*>(event);
		int newWidth = resizeEvent->size().width();
		int newHeight = resizeEvent->size().height();

		// Skip minimized (zero-area) window
		if (newWidth > 0 && newHeight > 0)
		{
			m_width = newWidth;
			m_height = newHeight;
			emit windowWidthChanged();
			emit windowHeightChanged();

			// Notify renderer to recreate swapchain
			m_bus->windowResized(newWidth, newHeight);
		}
	}

	return QObject::eventFilter(obj, event);
}

} // namespace neurus
