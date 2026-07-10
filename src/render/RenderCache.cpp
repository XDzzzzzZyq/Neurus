#include "RenderCache.h"

#include "Log.h"
#include "scene/Light.h"
#include "scene/Environment.h"
#include "asset/MeshData.h"
#include "asset/ImageData.h"
#include "asset/PixelFormat.h"
#include "buffers/VertexBuffer.h"
#include "buffers/IndexBuffer.h"
#include "Texture.h"
#include "passes/IBLPass.h"

#include <cassert>
#include <cstring>
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
// Mesh GPU resources (lazy creation)
// ---------------------------------------------------------------------------

MeshGPU& RenderCache::GetMeshGPU(const int objectId,
                                 const vk::Queue queue,
                                 const uint32_t queueFamilyIndex,
                                 const MeshData& meshData)
{
	auto it = rc_meshGPUs.find(objectId);
	if (it != rc_meshGPUs.end())
	{
		return it->second;
	}

	const auto& rawMesh = meshData.GetMeshData();
	const size_t srcVertexTotal = rawMesh.dataArray.size() / 14;  // 14 floats per vertex (source format)
	const size_t vertexCount = srcVertexTotal;
	const size_t indexCount = rawMesh.indexArray.size();

	if (vertexCount == 0 || indexCount == 0)
	{
		NEURUS_ERR("[RenderCache::GetMeshGPU] Empty mesh data for objectId=" << objectId);
		// Insert an empty MeshGPU and return it (caller checks for null buffers)
		auto [insertedIt, _] = rc_meshGPUs.emplace(objectId, MeshGPU{});
		return insertedIt->second;
	}

	// Strip vertex data from 14 floats (pos+normal+uv+tangent+bitangent)
	// down to 8 floats (pos+normal+uv) to match the pipeline vertex layout (32-byte stride).
	constexpr size_t kSrcStride = 14;
	constexpr size_t kDstStride = 8;
	std::vector<float> strippedVertices(vertexCount * kDstStride);
	for (size_t i = 0; i < vertexCount; ++i)
	{
		std::memcpy(&strippedVertices[i * kDstStride],
		            &rawMesh.dataArray[i * kSrcStride],
		            kDstStride * sizeof(float));
	}

	const uint32_t vertexStride = static_cast<uint32_t>(kDstStride * sizeof(float));
	const vk::DeviceSize vertexDataSize = strippedVertices.size() * sizeof(float);
	const vk::DeviceSize indexDataSize = indexCount * sizeof(uint32_t);

	auto [insertedIt, _] = rc_meshGPUs.emplace(objectId, MeshGPU{});
	MeshGPU& gpu = insertedIt->second;

	gpu.vertexBuffer = std::make_unique<VertexBuffer>(
		*rc_device, *rc_physicalDevice, queue, queueFamilyIndex,
		strippedVertices.data(), vertexDataSize,
		vertexStride,
		static_cast<uint32_t>(vertexCount),
		("MeshGPU_VBO_" + std::to_string(objectId)).c_str());

	gpu.indexBuffer = std::make_unique<IndexBuffer>(
		*rc_device, *rc_physicalDevice, queue, queueFamilyIndex,
		rawMesh.indexArray.data(), indexDataSize,
		static_cast<uint32_t>(indexCount),
		("MeshGPU_IBO_" + std::to_string(objectId)).c_str());

	gpu.vertexCount = static_cast<uint32_t>(vertexCount);
	gpu.indexCount = static_cast<uint32_t>(indexCount);

	NEURUS_LOG("[RenderCache::GetMeshGPU] objectId=" << objectId
	           << " vertices=" << gpu.vertexCount
	           << " indices=" << gpu.indexCount);

	return gpu;
}

void RenderCache::RemoveMeshGPU(const int objectId)
{
	rc_meshGPUs.erase(objectId);
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
	rc_meshGPUs.clear();
	rc_environmentGPUs.clear();
}

void RenderCache::CleanScreenSpace()
{
	rc_attachments.clear();
	rc_shadowColorMaps.clear();
	rc_shadowIntensityArray.reset();
	rc_shadowIntensityLayerIndex.clear();
	// rc_shadowMaps preserved — shadow cubemaps survive resize
	// rc_meshGPUs preserved — mesh GPU buffers survive resize
	// rc_environmentGPUs preserved — IBL cubemaps survive resize
}

// ---------------------------------------------------------------------------
// Environment GPU resources (lazy creation)
// ---------------------------------------------------------------------------

EnvironmentGPU& RenderCache::CreateEnvironmentGPU(const int envId,
                                                   const Environment& env,
                                                   const vk::Queue queue,
                                                   const uint32_t queueFamilyIndex,
                                                   IBLPass& iblPass)
{
	auto it = rc_environmentGPUs.find(envId);
	if (it != rc_environmentGPUs.end())
	{
		return it->second;
	}

	NEURUS_LOG("[RenderCache] Lazily creating EnvironmentGPU for envId=" << envId);

	// --- 1. Load equirectangular ImageData (CPU-side) ---
	// Use directly-set ImageData first (for tests/procedural), then try path loading,
	// fall back to pink-purple gradient
	ImageData equirectData = env.GetEquirectData();
	if (!equirectData.IsValid())
	{
		const std::string& eqPath = env.GetEquirectPath();
		if (!eqPath.empty())
		{
			equirectData = ImageData(eqPath);
		}
	}
	if (!equirectData.IsValid())
	{
		// Generate pink‑purple fallback equirect (64×32)
		constexpr uint32_t kFallbackW = 64;
		constexpr uint32_t kFallbackH = 32;
		std::vector<float> pixels(static_cast<size_t>(kFallbackW) * kFallbackH * 4, 0.0f);
		for (size_t i = 0; i < pixels.size(); i += 4)
		{
			pixels[i + 0] = 1.0f;   // R
			pixels[i + 1] = 0.0f;   // G
			pixels[i + 2] = 0.5f;   // B
			pixels[i + 3] = 1.0f;   // A
		}
		equirectData = ImageData(pixels.data(), kFallbackW, kFallbackH, PixelFormat::RGBA32F);
		NEURUS_LOG("[RenderCache] Using pink-purple fallback equirect for envId=" << envId);
	}

	// --- 2. Upload equirect to GPU ---
	auto equirectImage = Image::FromImageData(*rc_device, *rc_physicalDevice,
	                                           queue, queueFamilyIndex,
	                                           equirectData,
	                                           "Env_Equirect",
	                                           vk::ImageUsageFlagBits::eStorage);
	if (!equirectImage)
	{
		NEURUS_ERR("[RenderCache] Failed to upload equirect for envId=" << envId);
		// Insert empty EnvironmentGPU and return it
		auto [insertedIt, _] = rc_environmentGPUs.emplace(envId, EnvironmentGPU{});
		return insertedIt->second;
	}

	// --- 3. Create cubemap Images ---
	constexpr uint32_t kDiffuseRes  = 64;
	constexpr uint32_t kSpecularRes = 2048;
	constexpr uint32_t kSpecularMips = 8;

	const vk::ImageUsageFlags cubeUsage =
		vk::ImageUsageFlagBits::eStorage
		| vk::ImageUsageFlagBits::eSampled
		| vk::ImageUsageFlagBits::eTransferSrc;

	auto diffuseImage = std::make_unique<Image>(
		*rc_device, *rc_physicalDevice,
		vk::Extent2D{kDiffuseRes, kDiffuseRes},
		vk::Format::eR32G32B32A32Sfloat,
		cubeUsage,
		/*mipLevels=*/1,
		Image::ImageType::eCube,
		"Env_DiffuseCubemap");

	auto specularImage = std::make_unique<Image>(
		*rc_device, *rc_physicalDevice,
		vk::Extent2D{kSpecularRes, kSpecularRes},
		vk::Format::eR32G32B32A32Sfloat,
		cubeUsage,
		/*mipLevels=*/kSpecularMips,
		Image::ImageType::eCube,
		"Env_SpecularCubemap");

	// --- 4. Create cubemap samplers ---
	vk::SamplerCreateInfo samplerCI(
		{},
		vk::Filter::eLinear,
		vk::Filter::eLinear,
		vk::SamplerMipmapMode::eLinear,
		vk::SamplerAddressMode::eClampToEdge,
		vk::SamplerAddressMode::eClampToEdge,
		vk::SamplerAddressMode::eClampToEdge,
		0.0f,
		VK_FALSE,
		0.0f,
		VK_FALSE,
		vk::CompareOp::eAlways,
		0.0f,
		static_cast<float>(1),
		vk::BorderColor::eFloatTransparentBlack,
		VK_FALSE);
	auto diffuseSampler = vk::raii::Sampler(*rc_device, samplerCI);

	samplerCI.setMaxLod(static_cast<float>(kSpecularMips));
	auto specularSampler = vk::raii::Sampler(*rc_device, samplerCI);

	// --- 5. Run IBL convolution ---
	iblPass.Generate(*equirectImage, *diffuseImage, *specularImage);

	// --- 6. Wrap in Textures and store ---
	EnvironmentGPU gpu;
	gpu.diffuseTexture = std::make_unique<Texture>(
		Texture::FromImage(std::move(diffuseImage), std::move(diffuseSampler)));
	gpu.specularTexture = std::make_unique<Texture>(
		Texture::FromImage(std::move(specularImage), std::move(specularSampler)));

	auto [insertedIt, _] = rc_environmentGPUs.emplace(envId, std::move(gpu));

	NEURUS_LOG("[RenderCache] EnvironmentGPU created for envId=" << envId
	           << " diffuse=" << kDiffuseRes << "^2 specular="
	           << kSpecularRes << "^2 x" << kSpecularMips << " mips");

	return insertedIt->second;
}

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
