#pragma once

#include <QObject>
#include <QWindow>

#include <vulkan/vulkan_raii.hpp>

#include <memory>

namespace neurus {

class EventBus;

/**
 * @brief Main application window with Vulkan surface.
 *
 * Creates a QWindow for Vulkan rendering and manages the VkSurfaceKHR lifecycle.
 * The Vulkan instance and device are owned by VulkanContext, NOT by Qt.
 *
 * Exposed to QML via context property for window title, size, and state.
 */
class MainWindow : public QObject
{
	Q_OBJECT

	Q_PROPERTY(int windowWidth READ windowWidth NOTIFY windowWidthChanged)
	Q_PROPERTY(int windowHeight READ windowHeight NOTIFY windowHeightChanged)
	Q_PROPERTY(QString windowTitle READ windowTitle NOTIFY windowTitleChanged)

public:
	/**
	 * @brief Creates the window and Vulkan surface.
	 * @param vulkanInstance A valid VkInstance (from VulkanContext).
	 * @param bus EventBus for resize notifications.
	 * @param width Initial window width (default 800).
	 * @param height Initial window height (default 600).
	 * @param title Window title.
	 * @param parent Qt parent object.
	 */
	explicit MainWindow(const vk::raii::Instance& vulkanInstance,
	                    EventBus* bus,
	                    int width = 800, int height = 600,
	                    const QString& title = "Neurus",
	                    QObject* parent = nullptr);
	~MainWindow() override;

	// Non-copyable — owns OS + Vulkan resources
	MainWindow(const MainWindow&) = delete;
	MainWindow& operator=(const MainWindow&) = delete;

	/** @brief The VkSurfaceKHR for swapchain creation (pass to Renderer). */
	const vk::raii::SurfaceKHR& surface() const { return *m_surface; }

	/** @brief The QWindow for Qt integration. */
	QWindow* window() const { return m_window.get(); }

	int windowWidth() const { return m_width; }
	int windowHeight() const { return m_height; }
	QString windowTitle() const { return m_title; }

signals:
	void windowWidthChanged();
	void windowHeightChanged();
	void windowTitleChanged();

protected:
	/** @brief Handles native window resize events. */
	bool eventFilter(QObject* obj, QEvent* event) override;

private:
	EventBus* m_bus = nullptr;

	std::unique_ptr<QWindow> m_window;
	std::unique_ptr<vk::raii::SurfaceKHR> m_surface;

	int m_width = 800;
	int m_height = 600;
	QString m_title = "Neurus";
};

} // namespace neurus
