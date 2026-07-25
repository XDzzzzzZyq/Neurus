#include "RenderCache.h"

#include "core/Log.h"

#include <algorithm>
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
	NEURUS_LOG("[RenderCache] Created");
}

void RenderCache::SetLightingGPU(std::unique_ptr<LightingGPU> lightingGPU)
{
	rc_lightingGPU = std::move(lightingGPU);
	NEURUS_LOG("[RenderCache] LightingGPU set");
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
	// Fast path: check the shadow index built by UpdateLighting (always in sync
	// with the SSBO shadowMapIndex field and current visibility state).
	auto it = rc_uidToShadowLayer.find(lightUID);
	if (it != rc_uidToShadowLayer.end())
	{
		return it->second;
	}

	// Slow path: light not yet registered (e.g. test context or first frame).
	// Lazily allocate the next available index.
	const uint32_t layer = static_cast<uint32_t>(rc_uidToShadowLayer.size());
	if (layer >= MAX_SHADOW_LAYERS)
	{
		NEURUS_ERR("[RenderCache] Shadow intensity layer overflow: lightUID="
		           << lightUID << " exceeds MAX_SHADOW_LAYERS=" << MAX_SHADOW_LAYERS);
		assert(false && "MAX_SHADOW_LAYERS exceeded");
		return 0;
	}

	rc_uidToShadowLayer[lightUID] = layer;
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
	const auto it = rc_uidToShadowLayer.find(lightUID);
	return (it != rc_uidToShadowLayer.end()) ? it->second : 0;
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
	rc_uidToShadowLayer.erase(lightUID);
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
	rc_meshGPUs.clear();
	rc_environmentGPUs.clear();
	rc_lightGPUs.clear();
	rc_uidToShadowLayer.clear();
	rc_lightingGPU.reset();
	rc_pipelineCache.Clear();
}

void RenderCache::CleanScreenSpace()
{
	rc_attachments.clear();
	rc_shadowIntensityArray.reset();
	// rc_uidTo*Index maps preserved — shadow indexing survives resize
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
// Pipeline cache
// ---------------------------------------------------------------------------

PipelineCache& RenderCache::GetPipelineCache()
{
	return rc_pipelineCache;
}

Pipeline* RenderCache::GetPipeline(const int uid)
{
	return rc_pipelineCache.Get(std::to_string(uid));
}

void RenderCache::UsePipeline(const int uid, Pipeline pipeline)
{
	rc_pipelineCache.Store(std::to_string(uid), std::move(pipeline));
}

void RenderCache::RemovePipeline(const int uid)
{
	rc_pipelineCache.Remove(std::to_string(uid));
}

// ---------------------------------------------------------------------------
// Lighting GPU resources
// ---------------------------------------------------------------------------

void RenderCache::UpdateLighting(const std::unordered_map<int,
                                 std::variant<PointLightStruct, SunLightStruct>>& lightDict)
{
	assert(rc_lightingGPU && "InitLightingGPU must be called before UpdateLighting");

	// --- Clear index maps ---
	rc_uidToSSBOIdx.clear();
	rc_uidToShadowLayer.clear();

	// --- Separate and sort by UID for deterministic SSBO ordering ---
	std::vector<std::pair<int, PointLightStruct>> pointEntries;
	std::vector<std::pair<int, SunLightStruct>> sunEntries;

	for (const auto& [uid, lightVariant] : lightDict)
	{
		if (std::holds_alternative<PointLightStruct>(lightVariant))
		{
			pointEntries.push_back({uid, std::get<PointLightStruct>(lightVariant)});
		}
		else if (std::holds_alternative<SunLightStruct>(lightVariant))
		{
			sunEntries.push_back({uid, std::get<SunLightStruct>(lightVariant)});
		}
	}

	std::sort(pointEntries.begin(), pointEntries.end(),
	          [](const auto& a, const auto& b) { return a.first < b.first; });
	std::sort(sunEntries.begin(), sunEntries.end(),
	          [](const auto& a, const auto& b) { return a.first < b.first; });

	// --- Build SSBO vectors and populate uid→index map ---
	std::vector<PointLightStruct> pointVec;
	for (size_t i = 0; i < pointEntries.size(); ++i)
	{
		rc_uidToSSBOIdx[pointEntries[i].first] = static_cast<uint32_t>(i);
		pointVec.push_back(pointEntries[i].second);
	}

	std::vector<SunLightStruct> sunVec;
	for (size_t i = 0; i < sunEntries.size(); ++i)
	{
		rc_uidToSSBOIdx[sunEntries[i].first] = static_cast<uint32_t>(i);
		sunVec.push_back(sunEntries[i].second);
	}

	// --- Assign shadow map indices (shared pool, point first then sun, max 4) ---
	uint32_t shadowIdx = 0;
	for (const auto& [uid, pl] : pointEntries)
	{
		if (shadowIdx >= MAX_SHADOW_LAYERS) break;
		rc_uidToShadowLayer[uid] = shadowIdx++;
	}
	for (const auto& [uid, sl] : sunEntries)
	{
		if (shadowIdx >= MAX_SHADOW_LAYERS) break;
		rc_uidToShadowLayer[uid] = shadowIdx++;
	}

	// --- Stamp shadowMapIndex on each struct ---
	for (size_t i = 0; i < pointEntries.size(); ++i)
	{
		const int uid = pointEntries[i].first;
		auto it = rc_uidToShadowLayer.find(uid);
		pointVec[i].shadowMapIndex = (it != rc_uidToShadowLayer.end())
			? static_cast<int32_t>(it->second) : -1;
	}
	for (size_t i = 0; i < sunEntries.size(); ++i)
	{
		const int uid = sunEntries[i].first;
		auto it = rc_uidToShadowLayer.find(uid);
		sunVec[i].shadowMapIndex = (it != rc_uidToShadowLayer.end())
			? static_cast<int32_t>(it->second) : -1;
	}

	// --- Upload to GPU ---
	rc_lightingGPU->UpdatePointLights(pointVec);
	rc_lightingGPU->UpdateSunLights(sunVec);

	NEURUS_LOG("[RenderCache] UpdateLighting: " << pointVec.size()
	           << " point lights, " << sunVec.size() << " sun lights, "
	           << rc_uidToShadowLayer.size() << " shadow casters");
}

void RenderCache::UpdateLight(int lightUID,
                              const std::variant<PointLightStruct, SunLightStruct>& light)
{
	assert(rc_lightingGPU && "InitLightingGPU must be called before UpdateLight");

	if (std::holds_alternative<PointLightStruct>(light))
	{
		auto it = rc_uidToSSBOIdx.find(lightUID);
		if (it == rc_uidToSSBOIdx.end()) return;

		PointLightStruct updated = std::get<PointLightStruct>(light);
		auto shadowIt = rc_uidToShadowLayer.find(lightUID);
		updated.shadowMapIndex = (shadowIt != rc_uidToShadowLayer.end())
			? static_cast<int32_t>(shadowIt->second) : -1;

		rc_lightingGPU->UpdatePointLight(updated, it->second);
	}
	else if (std::holds_alternative<SunLightStruct>(light))
	{
		auto it = rc_uidToSSBOIdx.find(lightUID);
		if (it == rc_uidToSSBOIdx.end()) return;

		SunLightStruct updated = std::get<SunLightStruct>(light);
		auto shadowIt = rc_uidToShadowLayer.find(lightUID);
		updated.shadowMapIndex = (shadowIt != rc_uidToShadowLayer.end())
			? static_cast<int32_t>(shadowIt->second) : -1;

		rc_lightingGPU->UpdateSunLight(updated, it->second);
	}
}

uint32_t RenderCache::GetShadowIndex(int lightUID) const
{
	auto it = rc_uidToShadowLayer.find(lightUID);
	return (it != rc_uidToShadowLayer.end()) ? it->second : 0;
}

LightingGPU* RenderCache::GetLightingGPU()
{
	return rc_lightingGPU.get();
}

const LightingGPU* RenderCache::GetLightingGPU() const
{
	return rc_lightingGPU.get();
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

	// --- ID ---
	case AttachmentName::IDBuffer:
		return { vk::Format::eR32Uint, kColorAttachmentUsage, e2D };

	// --- Gizmo ---
	case AttachmentName::GizmoHighlight:
		return { vk::Format::eR8Unorm,
		         kColorAttachmentUsage | vk::ImageUsageFlagBits::eStorage, e2D };

	// --- Composite ---
	case AttachmentName::ComposedOutput:
		return { vk::Format::eR16G16B16A16Sfloat,
		         kColorAttachmentUsage | vk::ImageUsageFlagBits::eStorage, e2D };

	// --- Anti-Aliasing ---
	case AttachmentName::FXAAOutput:
		return { vk::Format::eR16G16B16A16Sfloat,
		         kColorAttachmentUsage | vk::ImageUsageFlagBits::eStorage, e2D };
	case AttachmentName::FXAAOffsets:
		return { vk::Format::eR16G16Sfloat,
		         vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc, e2D };
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
	case AttachmentName::IDBuffer:          return "IDBuffer";
	case AttachmentName::GizmoHighlight:    return "GizmoHighlight";
	case AttachmentName::ComposedOutput:    return "ComposedOutput";
	case AttachmentName::FXAAOutput:        return "FXAAOutput";
	case AttachmentName::FXAAOffsets:       return "FXAAOffsets";
	}
	return "Unknown";
}

} // namespace neurus
