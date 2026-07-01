// Must define platform before including any Vulkan headers
#define VK_USE_PLATFORM_WIN32_KHR

#include "MainWindow.h"
#include "editor/events/UIEvents.h"

#include <stdexcept>

namespace neurus {

MainWindow::MainWindow(const vk::raii::Instance& vulkanInstance,
                       UIEvents* bus,
                       int width, int height,
                       const QString& title,
                       QObject* parent)
	: QObject(parent)
	, win_bus(bus)
	, win_width(width)
	, win_height(height)
	, win_title(title)
{
	HINSTANCE hinstance = GetModuleHandle(nullptr);

	WNDCLASSEX wc = {};
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.lpfnWndProc = DefWindowProc;
	wc.hInstance = hinstance;
	wc.lpszClassName = L"NeurusVulkanWindow";
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	RegisterClassEx(&wc);

	win_hwnd = CreateWindowEx(
		0, L"NeurusVulkanWindow", L"Neurus",
		WS_OVERLAPPEDWINDOW,  // Not WS_VISIBLE - show after swapchain is ready
		CW_USEDEFAULT, CW_USEDEFAULT, win_width, win_height,
		nullptr, nullptr, hinstance, nullptr);

	if (!win_hwnd)
	{
		throw std::runtime_error("Failed to create native window.");
	}

	vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo({}, hinstance, win_hwnd);
	win_surface = std::make_unique<vk::raii::SurfaceKHR>(vulkanInstance, surfaceCreateInfo);
}

MainWindow::~MainWindow()
{
	win_surface.reset();
	if (win_hwnd)
	{
		DestroyWindow(win_hwnd);
	}
}

} // namespace neurus
