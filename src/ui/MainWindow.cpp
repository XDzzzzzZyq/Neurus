// Must define platform before including any Vulkan headers
#define VK_USE_PLATFORM_WIN32_KHR

#include "MainWindow.h"
#include "editor/EventBus.h"

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
	HINSTANCE hinstance = GetModuleHandle(nullptr);

	WNDCLASSEX wc = {};
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.lpfnWndProc = DefWindowProc;
	wc.hInstance = hinstance;
	wc.lpszClassName = L"NeurusVulkanWindow";
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	RegisterClassEx(&wc);

	m_hwnd = CreateWindowEx(
		0, L"NeurusVulkanWindow", L"Neurus",
		WS_OVERLAPPEDWINDOW,  // Not WS_VISIBLE — show after swapchain is ready
		CW_USEDEFAULT, CW_USEDEFAULT, m_width, m_height,
		nullptr, nullptr, hinstance, nullptr);

	if (!m_hwnd)
	{
		throw std::runtime_error("Failed to create native window.");
	}

	vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo({}, hinstance, m_hwnd);
	m_surface = std::make_unique<vk::raii::SurfaceKHR>(vulkanInstance, surfaceCreateInfo);
}

MainWindow::~MainWindow()
{
	m_surface.reset();
	if (m_hwnd)
	{
		DestroyWindow(m_hwnd);
	}
}

} // namespace neurus
