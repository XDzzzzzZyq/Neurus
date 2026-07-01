#include "RenderCache.h"

#include "Log.h"
#include "scene/Light.h"

#include <cassert>
#include <stdexcept>

namespace neurus {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

RenderCache::RenderCache(const vk::raii::Device& device,
                       const vk::raii::PhysicalDevice& physicalDevice)
	: rc_device(&device)
	, rc_physicalDevice(&physicalDevice)
{
}

// ---------------------------------------------------------------------------
// Lazy attachment creation
// ---------------------------------------------------------------------------

void RenderCache::createAttachment(const AttachmentName name, const vk::Extent2D extent)
{
	const auto config = ConfigFor(name);

	Image image(*rc_device,
	                  *rc_physicalDevice,
	                  extent,
	                  config.format,
	                  config.usage,
	                  1,                // mipLevels
	                  config.imageType,
	                  AttachmentNameToString(name));  // debug name

	rc_attachments.emplace(name, std::move(image));
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

Image& RenderCache::GetAttachment(const AttachmentName name, const vk::Extent2D extent)
{
	auto it = rc_attachments.find(name);
	if (it == rc_attachments.end())
	{
		NEURUS_LOG("[RenderCache] Lazily creating attachment \""
		          << AttachmentNameToString(name) << "\" at "
		          << extent.width << "x" << extent.height);
		createAttachment(name, extent);
		it = rc_attachments.find(name);
	}
	return it->second;
}

const Image& RenderCache::GetAttachment(const AttachmentName name) const
{
	const auto it = rc_attachments.find(name);
	if (it == rc_attachments.end())
	{
		throw std::out_of_range("RenderCache::GetAttachment: attachment not found");
	}
	return it->second;
}

// ---------------------------------------------------------------------------
// Per-light shadow resources (lazy creation)
// ---------------------------------------------------------------------------

Image& RenderCache::GetShadowMap(const int lightUID, const LightType type)
{
	auto it = rc_shadowMaps.find(lightUID);
	if (it != rc_shadowMaps.end())
	{
		return it->second;
	}

	if (type == LightType::SUNLIGHT)
	{
		constexpr vk::Extent2D kSunShadowRes{2048, 2048};
		const std::string debugName = "SunShadowDepth_Light_" + std::to_string(lightUID);

		Image sunShadow(*rc_device,
		                *rc_physicalDevice,
		                kSunShadowRes,
		                vk::Format::eD32Sfloat,
		                vk::ImageUsageFlagBits::eDepthStencilAttachment |
		                    vk::ImageUsageFlagBits::eSampled |
		                    vk::ImageUsageFlagBits::eTransferSrc,
		                1,                           // mipLevels
		                Image::ImageType::e2D,
		                debugName.c_str()); // debug name

		const auto [insertedIt, _] = rc_shadowMaps.emplace(lightUID, std::move(sunShadow));
		return insertedIt->second;
	}

	// Default: point light cubemap
	constexpr vk::Extent2D kShadowRes{1024, 1024};
	const std::string debugName = "ShadowDepthCubemap_Light_" + std::to_string(lightUID);

	Image cubemap(*rc_device,
	              *rc_physicalDevice,
	              kShadowRes,
	              vk::Format::eD32Sfloat,
	              vk::ImageUsageFlagBits::eDepthStencilAttachment |
	                  vk::ImageUsageFlagBits::eSampled |
	                  vk::ImageUsageFlagBits::eTransferSrc,
	              1,                           // mipLevels
	              Image::ImageType::eCube,
	              debugName.c_str()); // debug name

	const auto [insertedIt, _] = rc_shadowMaps.emplace(lightUID, std::move(cubemap));
	return insertedIt->second;
}

Image& RenderCache::GetShadowIntensityArray(const vk::Extent2D extent)
{
	if (rc_shadowIntensityArray)
	{
		return *rc_shadowIntensityArray;
	}

	NEURUS_LOG("[RenderCache] Lazily creating shadow intensity array at "
	           << extent.width << "x" << extent.height
	           << " with " << MAX_SHADOW_LAYERS << " layers");

	rc_shadowIntensityArray = std::make_unique<Image>(
		*rc_device,
		*rc_physicalDevice,
		extent,
		vk::Format::eR8Unorm,
		vk::ImageUsageFlagBits::eStorage |
			vk::ImageUsageFlagBits::eSampled |
			vk::ImageUsageFlagBits::eTransferSrc |
			vk::ImageUsageFlagBits::eTransferDst,
		1,                              // mipLevels
		Image::ImageType::eArray,
		"ShadowIntensityArray",         // debug name
		false,                          // arrayView
		MAX_SHADOW_LAYERS);             // arrayLayers

	return *rc_shadowIntensityArray;
}

uint32_t RenderCache::GetShadowIntensityLayer(const int lightUID, const vk::Extent2D /*extent*/)
{
	auto it = rc_shadowIntensityLayerIndex.find(lightUID);
	if (it != rc_shadowIntensityLayerIndex.end())
	{
		return it->second;
	}

	const uint32_t layer = static_cast<uint32_t>(rc_shadowIntensityLayerIndex.size());
	if (layer >= MAX_SHADOW_LAYERS)
	{
		NEURUS_ERR("[RenderCache] Shadow intensity layer overflow: lightUID="
		           << lightUID << " exceeds MAX_SHADOW_LAYERS=" << MAX_SHADOW_LAYERS);
		assert(false && "MAX_SHADOW_LAYERS exceeded");
		return 0;
	}

	rc_shadowIntensityLayerIndex[lightUID] = layer;
	NEURUS_LOG("[RenderCache] Allocated shadow intensity layer " << layer
	           << " for lightUID=" << lightUID);
	return layer;
}

Image& RenderCache::GetShadowColorMap(const int lightUID, const vk::Extent2D extent)
{
	auto it = rc_shadowColorMaps.find(lightUID);
	if (it != rc_shadowColorMaps.end())
	{
		return it->second;
	}

	const std::string debugName = "ShadowColorCubemap_Light_" + std::to_string(lightUID);
	Image colorCube(*rc_device,
	                *rc_physicalDevice,
	                extent,
	                vk::Format::eR32G32B32A32Sfloat,
	                vk::ImageUsageFlagBits::eColorAttachment |
	                    vk::ImageUsageFlagBits::eSampled |
	                    vk::ImageUsageFlagBits::eTransferSrc,
	                1,                           // mipLevels
	                Image::ImageType::eCube,
	                debugName.c_str()); // debug name

	const auto [insertedIt, _] = rc_shadowColorMaps.emplace(lightUID, std::move(colorCube));
	return insertedIt->second;
}

std::vector<int> RenderCache::GetShadowMapUIDs() const
{
	std::vector<int> uids;
	uids.reserve(rc_shadowMaps.size());
	for (const auto& [uid, _] : rc_shadowMaps)
	{
		uids.push_back(uid);
	}
	return uids;
}

Image* RenderCache::GetShadowIntensityArray() const
{
	return rc_shadowIntensityArray.get();
}

uint32_t RenderCache::GetShadowIntensityLayerIndex(const int lightUID) const
{
	const auto it = rc_shadowIntensityLayerIndex.find(lightUID);
	return (it != rc_shadowIntensityLayerIndex.end()) ? it->second : 0;
}

void RenderCache::RemoveLight(const int lightUID)
{
	rc_shadowMaps.erase(lightUID);
	rc_shadowIntensityLayerIndex.erase(lightUID);
	rc_shadowColorMaps.erase(lightUID);
}

bool RenderCache::HasAttachment(const AttachmentName name) const
{
	return rc_attachments.find(name) != rc_attachments.end();
}

// ---------------------------------------------------------------------------
// Clean / CleanScreenSpace
// ---------------------------------------------------------------------------

void RenderCache::Clean()
{
	rc_attachments.clear();
	rc_shadowMaps.clear();
	rc_shadowColorMaps.clear();
	rc_shadowIntensityArray.reset();
	rc_shadowIntensityLayerIndex.clear();
}

void RenderCache::CleanScreenSpace()
{
	rc_attachments.clear();
	rc_shadowColorMaps.clear();
	rc_shadowIntensityArray.reset();
	rc_shadowIntensityLayerIndex.clear();
	// rc_shadowMaps preserved — shadow cubemaps survive resize
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
	}
	return "Unknown";
}

} // namespace neurus
