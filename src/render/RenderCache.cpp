#include "RenderCache.h"

#include "Log.h"

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
// Lazy attachment creation
// ---------------------------------------------------------------------------

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

Image* RenderCache::GetShadowIntensityArray() const
{
	return rc_shadowIntensityArray.get();
}

uint32_t RenderCache::GetShadowIntensityLayerIndex(const int lightUID) const
{
	const auto it = rc_shadowIntensityLayerIndex.find(lightUID);
	return (it != rc_shadowIntensityLayerIndex.end()) ? it->second : 0;
}

std::vector<int> RenderCache::GetShadowMapUIDs() const
{
	std::vector<int> uids;
	uids.reserve(rc_lightGPUs.size());
	for (const auto& [uid, lgpu] : rc_lightGPUs)
	{
		if (lgpu.shadowDepthMap)
		{
			uids.push_back(uid);
		}
	}
	return uids;
}

void RenderCache::RemoveLight(const int lightUID)
{
	// Clear shadow maps within LightGPU if present
	auto it = rc_lightGPUs.find(lightUID);
	if (it != rc_lightGPUs.end())
	{
		it->second.shadowDepthMap.reset();
		it->second.shadowColorMap.reset();
	}
	rc_shadowIntensityLayerIndex.erase(lightUID);
}

bool RenderCache::HasAttachment(const AttachmentName name) const
{
	return rc_attachments.find(name) != rc_attachments.end();
}

// ---------------------------------------------------------------------------
// Mesh GPU resources (query-only)
// ---------------------------------------------------------------------------

MeshGPU* RenderCache::GetMeshGPU(const int objectId)
{
	auto it = rc_meshGPUs.find(objectId);
	return (it != rc_meshGPUs.end()) ? &it->second : nullptr;
}

const MeshGPU* RenderCache::GetMeshGPU(const int objectId) const
{
	auto it = rc_meshGPUs.find(objectId);
	return (it != rc_meshGPUs.end()) ? &it->second : nullptr;
}

void RenderCache::RemoveMeshGPU(const int objectId)
{
	rc_meshGPUs.erase(objectId);
}

void RenderCache::UseMeshGPU(const int objectId, MeshGPU meshGPU)
{
	rc_meshGPUs[objectId] = std::move(meshGPU);
	NEURUS_LOG("[RenderCache] Registered MeshGPU for objectId=" << objectId);
}

// ---------------------------------------------------------------------------
// Clean / CleanScreenSpace
// ---------------------------------------------------------------------------

void RenderCache::Clean()
{
	rc_attachments.clear();
	rc_shadowIntensityArray.reset();
	rc_shadowIntensityLayerIndex.clear();
	rc_meshGPUs.clear();
	rc_environmentGPUs.clear();
	rc_lightGPUs.clear();
}

void RenderCache::CleanScreenSpace()
{
	rc_attachments.clear();
	rc_shadowIntensityArray.reset();
	rc_shadowIntensityLayerIndex.clear();
	// rc_meshGPUs preserved — mesh GPU buffers survive resize
	// rc_environmentGPUs preserved — IBL cubemaps survive resize
	// rc_lightGPUs preserved — shadow depth maps survive resize
}

// ---------------------------------------------------------------------------
// Environment GPU resources (registration/query)
// ---------------------------------------------------------------------------

EnvironmentGPU* RenderCache::GetEnvironmentGPU(const int envId)
{
	auto it = rc_environmentGPUs.find(envId);
	return (it != rc_environmentGPUs.end()) ? &it->second : nullptr;
}

const EnvironmentGPU* RenderCache::GetEnvironmentGPU(const int envId) const
{
	auto it = rc_environmentGPUs.find(envId);
	return (it != rc_environmentGPUs.end()) ? &it->second : nullptr;
}

void RenderCache::RemoveEnvironmentGPU(const int envId)
{
	rc_environmentGPUs.erase(envId);
}

void RenderCache::UseEnvironmentGPU(const int envId, EnvironmentGPU envGPU)
{
	rc_environmentGPUs[envId] = std::move(envGPU);
	NEURUS_LOG("[RenderCache] Registered EnvironmentGPU for envId=" << envId);
}

// ---------------------------------------------------------------------------
// Per-light GPU resource registration
// ---------------------------------------------------------------------------

void RenderCache::UseLightGPU(const int lightUID, LightGPU lightGPU)
{
	rc_lightGPUs[lightUID] = std::move(lightGPU);
	NEURUS_LOG("[RenderCache] Registered LightGPU for lightUID=" << lightUID);
}

LightGPU* RenderCache::GetLightGPU(const int lightUID)
{
	auto it = rc_lightGPUs.find(lightUID);
	return (it != rc_lightGPUs.end()) ? &it->second : nullptr;
}

const LightGPU* RenderCache::GetLightGPU(const int lightUID) const
{
	auto it = rc_lightGPUs.find(lightUID);
	return (it != rc_lightGPUs.end()) ? &it->second : nullptr;
}

void RenderCache::RemoveLightGPU(const int lightUID)
{
	rc_lightGPUs.erase(lightUID);
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
