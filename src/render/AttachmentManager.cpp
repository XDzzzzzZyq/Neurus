#include "AttachmentManager.h"

#include "Log.h"

#include <stdexcept>

namespace neurus {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

AttachmentManager::AttachmentManager(const vk::raii::Device& device,
                                     const vk::raii::PhysicalDevice& physicalDevice)
	: m_device(&device)
	, m_physicalDevice(&physicalDevice)
{
}

// ---------------------------------------------------------------------------
// Create / Resize
// ---------------------------------------------------------------------------

void AttachmentManager::Create(const vk::Extent2D extent)
{
	m_extent = extent;
	m_attachments.clear();

	// --- G-Buffer ---
	createAttachment(AttachmentName::Position);
	createAttachment(AttachmentName::Normal);
	createAttachment(AttachmentName::Albedo);
	createAttachment(AttachmentName::MetallicRoughness);
	createAttachment(AttachmentName::Depth);

	// --- Post-FX ---
	createAttachment(AttachmentName::HDRColor);
	createAttachment(AttachmentName::SSAO);
	createAttachment(AttachmentName::SSR);

	NEURUS_LOG("[AttachmentManager] extent=" << m_extent.width << "x" << m_extent.height
	          << " attachments=8"
	          << " (position, normal, albedo, metallicRoughness, depth, hdrColor, ssao, ssr)");
}

void AttachmentManager::Resize(const vk::Extent2D extent)
{
	Create(extent);
}

// ---------------------------------------------------------------------------
// Single attachment creation
// ---------------------------------------------------------------------------

void AttachmentManager::createAttachment(const AttachmentName name)
{
	const auto config = ConfigFor(name);

	Image image(*m_device,
	                  *m_physicalDevice,
	                  m_extent,
	                  config.format,
	                  config.usage,
	                  1,                // mipLevels
	                  config.imageType,
	                  AttachmentNameToString(name));  // debug name

	m_attachments.emplace(name, std::move(image));
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

Image& AttachmentManager::GetAttachment(const AttachmentName name)
{
	const auto it = m_attachments.find(name);
	if (it == m_attachments.end())
	{
		throw std::out_of_range("AttachmentManager::GetAttachment: attachment not found");
	}
	return it->second;
}

const Image& AttachmentManager::GetAttachment(const AttachmentName name) const
{
	const auto it = m_attachments.find(name);
	if (it == m_attachments.end())
	{
		throw std::out_of_range("AttachmentManager::GetAttachment: attachment not found");
	}
	return it->second;
}

bool AttachmentManager::HasAttachment(const AttachmentName name) const
{
	return m_attachments.find(name) != m_attachments.end();
}

// ---------------------------------------------------------------------------
// Attachment configuration
// ---------------------------------------------------------------------------

AttachmentManager::AttachmentConfig AttachmentManager::ConfigFor(const AttachmentName name)
{
	// Common usage for color attachments:
	//   COLOR_ATTACHMENT - written by fragment shader
	//   SAMPLED          - read by subsequent passes (deferred shading, post-FX)
	//   TRANSFER_SRC     - screenshot capture (T24a), debug readback
	constexpr vk::ImageUsageFlags kColorAttachmentUsage =
		vk::ImageUsageFlagBits::eColorAttachment |
		vk::ImageUsageFlagBits::eSampled |
		vk::ImageUsageFlagBits::eTransferSrc;

	// Depth attachment usage:
	//   DEPTH_STENCIL_ATTACHMENT - written by depth test
	//   SAMPLED                  - read by SSAO, SSR, etc.
	constexpr vk::ImageUsageFlags kDepthAttachmentUsage =
		vk::ImageUsageFlagBits::eDepthStencilAttachment |
		vk::ImageUsageFlagBits::eSampled;

	constexpr auto e2D = Image::ImageType::e2D;
	constexpr auto eDS = Image::ImageType::eDepthStencil;

	switch (name)
	{
	// --- G-Buffer ---
	case AttachmentName::Position:
		return { vk::Format::eR16G16B16A16Sfloat, kColorAttachmentUsage, e2D };
	case AttachmentName::Normal:
		return { vk::Format::eR16G16B16A16Sfloat, kColorAttachmentUsage, e2D };
	case AttachmentName::Albedo:
		return { vk::Format::eR8G8B8A8Srgb, kColorAttachmentUsage, e2D };
	case AttachmentName::MetallicRoughness:
		return { vk::Format::eR8G8B8A8Unorm, kColorAttachmentUsage, e2D };
	case AttachmentName::Depth:
		return { vk::Format::eD32Sfloat, kDepthAttachmentUsage, eDS };

	// --- Post-FX ---
	case AttachmentName::HDRColor:
		// STORAGE added for compute shader write (PBR lighting pass)
		return { vk::Format::eR16G16B16A16Sfloat,
		         kColorAttachmentUsage | vk::ImageUsageFlagBits::eStorage, e2D };
	case AttachmentName::SSAO:
		return { vk::Format::eR8Unorm, kColorAttachmentUsage, e2D };
	case AttachmentName::SSR:
		return { vk::Format::eR16G16B16A16Sfloat, kColorAttachmentUsage, e2D };
	}

	throw std::invalid_argument("AttachmentManager::ConfigFor: unknown attachment name");
}

// ---------------------------------------------------------------------------
// String conversion
// ---------------------------------------------------------------------------

const char* AttachmentNameToString(const AttachmentName name)
{
	switch (name)
	{
	case AttachmentName::Position:          return "Position";
	case AttachmentName::Normal:            return "Normal";
	case AttachmentName::Albedo:            return "Albedo";
	case AttachmentName::MetallicRoughness: return "MetallicRoughness";
	case AttachmentName::Depth:             return "Depth";
	case AttachmentName::HDRColor:          return "HDRColor";
	case AttachmentName::SSAO:              return "SSAO";
	case AttachmentName::SSR:               return "SSR";
	}
	return "Unknown";
}

} // namespace neurus
