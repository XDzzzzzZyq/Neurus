#pragma once

#include <QObject>
#include <QString>

#include <vulkan/vulkan_raii.hpp>

#define NOMINMAX
#include <windows.h>

#include <memory>

namespace neurus {

class EventBus;

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
	                    EventBus* bus,
	                    int width = 800, int height = 600,
	                    const QString& title = "Neurus",
	                    QObject* parent = nullptr);
	~MainWindow() override;

	MainWindow(const MainWindow&) = delete;
	MainWindow& operator=(const MainWindow&) = delete;

	const vk::raii::SurfaceKHR& surface() const { return *m_surface; }
	HWND hwnd() const { return m_hwnd; }
	int getWidth() const { return m_width; }
	int getHeight() const { return m_height; }

private:
	EventBus* m_bus = nullptr;

	std::unique_ptr<vk::raii::SurfaceKHR> m_surface;
	HWND m_hwnd = nullptr;

	int m_width = 800;
	int m_height = 600;
	QString m_title = "Neurus";
};

} // namespace neurus
