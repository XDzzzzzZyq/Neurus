#pragma once

#include "platform/PlatformSurface.h"

namespace neurus {

/// macOS platform surface — creates VkSurfaceKHR via VK_EXT_metal_surface.
/// Internally manages a NeurusMetalView subview with a CAMetalLayer.
class MacOSSurface : public PlatformSurface {
public:
	std::vector<const char*> requiredInstanceExtensions() const override;
	vk::InstanceCreateFlags instanceCreateFlags() const override;
	vk::raii::SurfaceKHR createSurface(
		const vk::raii::Instance& instance,
		NativeWindowHandle windowHandle) const override;
	void onResize(NativeWindowHandle windowHandle,
	              uint32_t width, uint32_t height) const override;
};

} // namespace neurus
