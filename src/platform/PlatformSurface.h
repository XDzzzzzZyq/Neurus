#pragma once

/**
 * @file PlatformSurface.h
 * @brief Abstract platform surface — isolates Vulkan surface creation from OS details.
 *
 * Each platform (Win32, macOS, Linux/X11, etc.) provides a subclass that knows
 * how to create a VkSurfaceKHR from a native window handle. The Application
 * layer uses this interface without any #ifdef.
 *
 * Architecture: Platform layer (leaf dependency — only Vulkan headers).
 */

#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include <memory>

namespace neurus {

/// Opaque native window handle. HWND on Win32, NSView* on macOS.
using NativeWindowHandle = void*;

/// Abstract platform surface factory.
class PlatformSurface {
public:
	virtual ~PlatformSurface() = default;

	/// Vulkan instance extensions required by this platform's surface type.
	virtual std::vector<const char*> requiredInstanceExtensions() const = 0;

	/// Instance create flags (e.g., eEnumeratePortabilityKHR on macOS). Default: none.
	virtual vk::InstanceCreateFlags instanceCreateFlags() const { return {}; }

	/// Create a VkSurfaceKHR from a native window handle.
	/// On macOS this internally configures the CAMetalLayer.
	virtual vk::raii::SurfaceKHR createSurface(
		const vk::raii::Instance& instance,
		NativeWindowHandle windowHandle) const = 0;

	/// Notify the platform of a resize (e.g., update CAMetalLayer drawable size).
	/// Default: no-op.
	virtual void onResize(NativeWindowHandle windowHandle,
	                      uint32_t width, uint32_t height) const {}
};

/// Compile-time factory — returns the platform-appropriate PlatformSurface.
std::unique_ptr<PlatformSurface> CreatePlatformSurface();

} // namespace neurus
