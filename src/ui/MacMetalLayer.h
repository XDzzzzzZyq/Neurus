#pragma once

/**
 * @file MacMetalLayer.h
 * @brief macOS helper to obtain a CAMetalLayer from a QWidget's NSView.
 *
 * On macOS, QWidget::winId() returns an NSView*, but Vulkan's
 * VK_EXT_metal_surface extension requires a CAMetalLayer*.
 * This helper configures the NSView to be layer-backed with a
 * CAMetalLayer and returns the layer pointer for surface creation.
 */

#ifdef __APPLE__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Ensures the NSView (from QWidget::winId()) is backed by a CAMetalLayer.
 *
 * Must be called on the main thread AFTER the widget is shown (winId() valid).
 *
 * @param nativeHandle The void* from reinterpret_cast<void*>(widget->winId()).
 * @return CAMetalLayer* (as void*) for vk::MetalSurfaceCreateInfoEXT, or nullptr on failure.
 */
void* makeViewMetalCompatible(void* nativeHandle);

/**
 * @brief Updates the CAMetalLayer drawable size to match the current view bounds.
 *
 * Call this when the Viewport widget is resized so the swapchain dimensions
 * match the actual pixel size (accounts for Retina scaling).
 *
 * @param nativeHandle The void* from reinterpret_cast<void*>(widget->winId()).
 */
void updateMetalLayerSize(void* nativeHandle);

#ifdef __cplusplus
}
#endif

#endif // __APPLE__
