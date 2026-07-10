/**
 * @file PixelFormat.h
 * @brief Vulkan‑free pixel format enum for CPU‑side image data.
 *
 * Maps to Vulkan `vk::Format` equivalents via comments.  Zero Vulkan includes.
 * Pure C++ standard library only — no external dependencies beyond <cstdint>.
 *
 * Analogous to MeshData.h (pure CPU data, no Vulkan/GPU resources).
 */
#pragma once

#include <cstdint>

namespace neurus {

/**
 * @brief CPU‑side pixel format identifiers.
 *
 * These correspond to Vulkan `vk::Format` values used throughout the
 * render pipeline.  The mapping is documented per‑enumerator; the
 * numeric values deliberately differ from Vulkan's — do NOT cast.
 *
 * | PixelFormat         | vk::Format equivalent              | Bytes/px | Channels |
 * |---------------------|------------------------------------|----------|----------|
 * | Undefined           | vk::Format::eUndefined             | 0        | 0        |
 * | RGBA8U              | vk::Format::eR8G8B8A8Unorm         | 4        | 4        |
 * | RGBA8S              | vk::Format::eR8G8B8A8Srgb          | 4        | 4        |
 * | RGBA32F             | vk::Format::eR32G32B32A32Sfloat    | 16       | 4        |
 * | RGBA16F             | vk::Format::eR16G16B16A16Sfloat    | 8        | 4        |
 * | R8U                 | vk::Format::eR8Unorm               | 1        | 1        |
 * | D32F                | vk::Format::eD32Sfloat             | 4        | 1        |
 * | BGRA8U              | vk::Format::eB8G8R8A8Unorm         | 4        | 4        |
 * | BGRA8S              | vk::Format::eB8G8R8A8Srgb          | 4        | 4        |
 * | RGBA16U             | vk::Format::eR16G16B16A16Unorm     | 8        | 4        |
 * | RGBA16SN            | vk::Format::eR16G16B16A16Snorm     | 8        | 4        |
 * | R8S                 | vk::Format::eR8Srgb                | 1        | 1        |
 */
enum class PixelFormat
{
	Undefined,

	/** 4 × 8-bit unsigned normalized [0,1] — RGBA. */
	RGBA8U,

	/** 4 × 8-bit sRGB (gamma‑encoded) — RGBA. */
	RGBA8S,

	/** 4 × 32-bit IEEE 754 float — RGBA.  Used for HDR framebuffers. */
	RGBA32F,

	/** 4 × 16-bit IEEE 754 half‑float — RGBA.  Used for G‑Buffer attachments. */
	RGBA16F,

	/** 1 × 8-bit unsigned normalized [0,1] — single channel. */
	R8U,

	/** 1 × 32-bit IEEE 754 float — depth (non‑colour). */
	D32F,

	/** 4 × 8-bit unsigned normalized [0,1] — BGRA (swapchain). */
	BGRA8U,

	/** 4 × 8-bit sRGB (gamma‑encoded) — BGRA (swapchain). */
	BGRA8S,

	/** 4 × 16-bit unsigned normalized [0,1] — RGBA. */
	RGBA16U,

	/** 4 × 16-bit signed normalized [-1,1] — RGBA. Used for normals. */
	RGBA16SN,

	/** 1 × 8-bit sRGB (gamma‑encoded) — single channel. */
	R8S,
};

// ---------------------------------------------------------------------------
// Helper functions
// ---------------------------------------------------------------------------

/**
 * @brief Bytes per pixel for a given PixelFormat.
 * @param fmt Pixel format.
 * @return Byte count, or 0 for Undefined / unhandled formats.
 */
inline uint32_t PixelByteSize(PixelFormat fmt)
{
	switch (fmt)
	{
	case PixelFormat::RGBA8U:    return 4;
	case PixelFormat::RGBA8S:    return 4;
	case PixelFormat::RGBA32F:   return 16;
	case PixelFormat::RGBA16F:   return 8;
	case PixelFormat::R8U:       return 1;
	case PixelFormat::D32F:      return 4;
	case PixelFormat::BGRA8U:    return 4;
	case PixelFormat::BGRA8S:    return 4;
	case PixelFormat::RGBA16U:   return 8;
	case PixelFormat::RGBA16SN:  return 8;
	case PixelFormat::R8S:       return 1;
	default:
	case PixelFormat::Undefined: return 0;
	}
}

/**
 * @brief Number of colour / depth channels for a given PixelFormat.
 * @param fmt Pixel format.
 * @return Channel count, or 0 for Undefined.
 */
inline uint32_t ChannelCount(PixelFormat fmt)
{
	switch (fmt)
	{
	case PixelFormat::R8U:
	case PixelFormat::R8S:
		return 1;
	case PixelFormat::RGBA8U:
	case PixelFormat::RGBA8S:
	case PixelFormat::RGBA32F:
	case PixelFormat::RGBA16F:
	case PixelFormat::BGRA8U:
	case PixelFormat::BGRA8S:
	case PixelFormat::RGBA16U:
	case PixelFormat::RGBA16SN:
	default:
		return 4;
	}
}

/**
 * @brief True if the format is sRGB (gamma‑encoded).
 *
 * RGBA8S, BGRA8S, and R8S return true.  All other formats are linear.
 */
inline bool IsSRGB(PixelFormat fmt)
{
	return fmt == PixelFormat::RGBA8S
	    || fmt == PixelFormat::BGRA8S
	    || fmt == PixelFormat::R8S;
}

/**
 * @brief True if the format is high‑dynamic range (floating‑point).
 *
 * RGBA32F and RGBA16F are HDR.
 * D32F is a depth format and not considered "HDR" for colour purposes.
 */
inline bool IsHDR(PixelFormat fmt)
{
	return fmt == PixelFormat::RGBA32F
	    || fmt == PixelFormat::RGBA16F;
}

/**
 * @brief True if the format is BGRA byte order.
 *
 * BGRA8U and BGRA8S return true. All other formats return false.
 */
inline bool IsBGRFormat(PixelFormat fmt)
{
	return fmt == PixelFormat::BGRA8U
	    || fmt == PixelFormat::BGRA8S;
}

} // namespace neurus
