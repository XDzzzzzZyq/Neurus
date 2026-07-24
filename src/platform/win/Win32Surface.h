#pragma once

#include "platform/PlatformSurface.h"

namespace neurus {

/// Win32 platform surface — creates VkSurfaceKHR via VK_KHR_win32_surface.
class Win32Surface : public PlatformSurface {
public:
	std::vector<const char*> requiredInstanceExtensions() const override;
	vk::raii::SurfaceKHR createSurface(
		const vk::raii::Instance& instance,
		NativeWindowHandle windowHandle) const override;
};

} // namespace neurus
