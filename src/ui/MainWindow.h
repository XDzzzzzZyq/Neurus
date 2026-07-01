#pragma once

#include <QObject>
#include <QString>

#include <vulkan/vulkan_raii.hpp>

#define NOMINMAX
#include <windows.h>

#include <memory>

namespace neurus {

class UIEvents;

/**
 * @brief Main application window with Vulkan surface.
 *
 * Creates a native Win32 window for Vulkan rendering and manages the
 * VkSurfaceKHR lifecycle. Uses Qt only for the event loop (QApplication).
 */
class MainWindow : public QObject
{
	Q_OBJECT

public:
	explicit MainWindow(const vk::raii::Instance& vulkanInstance,
	                    UIEvents* bus,
	                    int width = 800, int height = 600,
	                    const QString& title = "Neurus",
	                    QObject* parent = nullptr);
	~MainWindow() override;

	MainWindow(const MainWindow&) = delete;
	MainWindow& operator=(const MainWindow&) = delete;

	const vk::raii::SurfaceKHR& surface() const { return *win_surface; }
	HWND hwnd() const { return win_hwnd; }
	void show() { ShowWindow(win_hwnd, SW_SHOW); UpdateWindow(win_hwnd); }
	int getWidth() const { return win_width; }
	int getHeight() const { return win_height; }

private:
	UIEvents* win_bus = nullptr;

	std::unique_ptr<vk::raii::SurfaceKHR> win_surface;
	HWND win_hwnd = nullptr;

	int win_width = 800;
	int win_height = 600;
	QString win_title = "Neurus";
};

} // namespace neurus
