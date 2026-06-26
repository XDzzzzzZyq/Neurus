#include "passes/RenderCache.h"

#include "Log.h"

#include <stdexcept>

namespace neurus {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

RenderCache::RenderCache(const vk::raii::Device& device,
                       const vk::raii::PhysicalDevice& physicalDevice)
	: m_device(&device)
	, m_physicalDevice(&physicalDevice)
{
}

// ---------------------------------------------------------------------------
// Lazy attachment creation
// ---------------------------------------------------------------------------

void RenderCache::createAttachment(const AttachmentName name, const vk::Extent2D extent)
{
	const auto config = ConfigFor(name);

	Image image(*m_device,
	                  *m_physicalDevice,
	                  extent,
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

Image& RenderCache::GetAttachment(const AttachmentName name, const vk::Extent2D extent)
{
	auto it = m_attachments.find(name);
	if (it == m_attachments.end())
	{
		NEURUS_LOG("[RenderCache] Lazily creating attachment \""
		          << AttachmentNameToString(name) << "\" at "
		          << extent.width << "x" << extent.height);
		createAttachment(name, extent);
		it = m_attachments.find(name);
	}
	return it->second;
}

const Image& RenderCache::GetAttachment(const AttachmentName name) const
{
	const auto it = m_attachments.find(name);
	if (it == m_attachments.end())
	{
		throw std::out_of_range("RenderCache::GetAttachment: attachment not found");
	}
	return it->second;
}

// ---------------------------------------------------------------------------
// Per-light shadow resources (lazy creation)
// ---------------------------------------------------------------------------

Image& RenderCache::GetShadowMap(const int lightUID)
{
	auto it = m_shadowMaps.find(lightUID);
	if (it != m_shadowMaps.end())
	{
		return it->second;
	}

	constexpr vk::Extent2D kShadowRes{1024, 1024};

	Image cubemap(*m_device,
	              *m_physicalDevice,
	              kShadowRes,
	              vk::Format::eD32Sfloat,
	              vk::ImageUsageFlagBits::eDepthStencilAttachment |
	                  vk::ImageUsageFlagBits::eSampled,
	              1,                           // mipLevels
	              Image::ImageType::eCube,
	              "ShadowDepthCubemap_Light"); // debug name

	const auto [insertedIt, _] = m_shadowMaps.emplace(lightUID, std::move(cubemap));
	return insertedIt->second;
}

Image& RenderCache::GetShadowIntensity(const int lightUID, const vk::Extent2D extent)
{
	auto it = m_shadowIntensities.find(lightUID);
	if (it != m_shadowIntensities.end())
	{
		return it->second;
	}

	Image intensity(*m_device,
	                *m_physicalDevice,
	                extent,
	                vk::Format::eR8Unorm,
	                vk::ImageUsageFlagBits::eStorage |
	                    vk::ImageUsageFlagBits::eSampled |
	                    vk::ImageUsageFlagBits::eTransferSrc |
	                    vk::ImageUsageFlagBits::eTransferDst,
	                1,                         // mipLevels
	                Image::ImageType::e2D,
	                "ShadowIntensity_Light");  // debug name

	const auto [insertedIt, _] = m_shadowIntensities.emplace(lightUID, std::move(intensity));
	return insertedIt->second;
}

void RenderCache::RemoveLight(const int lightUID)
{
	m_shadowMaps.erase(lightUID);
	m_shadowIntensities.erase(lightUID);
}

bool RenderCache::HasAttachment(const AttachmentName name) const
{
	return m_attachments.find(name) != m_attachments.end();
}

// ---------------------------------------------------------------------------
// Clean / CleanScreenSpace
// ---------------------------------------------------------------------------

void RenderCache::Clean()
{
	m_attachments.clear();
	m_shadowMaps.clear();
	m_shadowIntensities.clear();
}

void RenderCache::CleanScreenSpace()
{
	m_attachments.clear();
	m_shadowIntensities.clear();
	// m_shadowMaps preserved — shadow cubemaps survive resize
}

// ---------------------------------------------------------------------------
// Attachment configuration
// ---------------------------------------------------------------------------

RenderCache::AttachmentConfig RenderCache::ConfigFor(const AttachmentName name)
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
		vk::ImageUsageFlagBits::eSampled |
		vk::ImageUsageFlagBits::eTransferSrc;

	constexpr auto e2D = Image::ImageType::e2D;
	constexpr auto eDS = Image::ImageType::eDepthStencil;
	constexpr auto eCube = Image::ImageType::eCube;

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
		return { vk::Format::eR8Unorm,
		         kColorAttachmentUsage | vk::ImageUsageFlagBits::eStorage, e2D };
	case AttachmentName::SSR:
		return { vk::Format::eR16G16B16A16Sfloat, kColorAttachmentUsage, e2D };

	// --- Shadow ---
	case AttachmentName::ShadowDepth:
		// Cubemap depth attachment: written by ShadowDepthPass, sampled by shadow evaluation
		return { vk::Format::eD32Sfloat,
		         vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
		         eCube };
	case AttachmentName::ShadowIntensity:
		// R8 shadow intensity: written by shadow evaluation compute shader
		return { vk::Format::eR8Unorm,
		         vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
		             vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst,
		         e2D };
	}

	throw std::invalid_argument("RenderCache::ConfigFor: unknown attachment name");
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
	case AttachmentName::ShadowDepth:       return "ShadowDepth";
	case AttachmentName::ShadowIntensity:   return "ShadowIntensity";
	}
	return "Unknown";
}

} // namespace neurus
