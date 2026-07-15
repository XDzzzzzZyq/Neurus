/**
 * @file UploadManager.cpp
 * @brief CPU-to-GPU upload service — scaffold.
 *
 * Task 4 scaffold: constructor stores refs, creates CommandPool, stub methods
 * return empty GPU structs.  Real upload logic is added in Tasks 5–7.
 */

#include "UploadManager.h"

#include "Image.h"
#include "Texture.h"
#include "resources/MeshGPU.h"
#include "resources/EnvironmentGPU.h"
#include "resources/LightGPU.h"
#include "resources/LightingGPU.h"

#include "asset/ImageData.h"
#include "asset/MeshData.h"
#include "asset/PixelFormat.h"
#include "buffers/IndexBuffer.h"
#include "buffers/VertexBuffer.h"
#include "core/Log.h"
#include "passes/IBLPass.h"
#include "scene/Environment.h"
#include "scene/Light.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"

namespace neurus {

UploadManager::UploadManager(const vk::raii::Device& device,
                             const vk::raii::PhysicalDevice& physicalDevice,
                             uint32_t queueFamilyIndex)
	: um_device(&device)
	, um_physicalDevice(&physicalDevice)
	, um_queueFamilyIndex(queueFamilyIndex)
{
	um_queue = um_device->getQueue(queueFamilyIndex, 0);

	vk::CommandPoolCreateInfo poolInfo({}, queueFamilyIndex);
	poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
	um_commandPool = vk::raii::CommandPool(device, poolInfo);

	NEURUS_LOG("[UploadManager] Created with queue family " << queueFamilyIndex);
}

UploadManager::~UploadManager()
{
	NEURUS_LOG("[UploadManager] Destroyed");
}

void UploadManager::WaitIdle()
{
	um_device->waitIdle();
}

MeshGPU UploadManager::UploadMesh(const Mesh& mesh)
{
	// --- Validate mesh data ---
	if (!mesh.o_mesh)
	{
		NEURUS_ERR("[UploadManager] UploadMesh: mesh has no MeshData (null o_mesh)");
		return MeshGPU{};
	}

	const auto& rawMesh = mesh.o_mesh->GetMeshData();
	const size_t vertexCount = rawMesh.dataArray.size() / 14;  // 14 floats per vertex (source format)
	const size_t indexCount = rawMesh.indexArray.size();

	if (vertexCount == 0 || indexCount == 0)
	{
		NEURUS_ERR("[UploadManager] UploadMesh: empty mesh data ("
		           << vertexCount << " verts, " << indexCount << " indices)");
		return MeshGPU{};
	}

	// --- Strip vertex data: 14 floats → 8 floats (pos+normal+uv) ---
	// Matches GeometryPass::BufferLayout (32-byte stride).
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

	// --- Create GPU buffers ---
	auto vbo = std::make_unique<VertexBuffer>(
		*um_device, *um_physicalDevice, um_queue, um_queueFamilyIndex,
		strippedVertices.data(), vertexDataSize,
		vertexStride,
		static_cast<uint32_t>(vertexCount),
		"UploadMesh_VBO");

	auto ibo = std::make_unique<IndexBuffer>(
		*um_device, *um_physicalDevice, um_queue, um_queueFamilyIndex,
		rawMesh.indexArray.data(), indexDataSize,
		static_cast<uint32_t>(indexCount),
		"UploadMesh_IBO");

	NEURUS_LOG("[UploadManager] Uploaded mesh: " << vertexCount << " verts, " << indexCount << " indices");

	return MeshGPU{std::move(vbo), std::move(ibo),
	               static_cast<uint32_t>(vertexCount), static_cast<uint32_t>(indexCount)};
}

EnvironmentGPU UploadManager::UploadEnvironment(const Environment& env)
{
	// --- 0. Lazy-init IBLPass ---
	if (!um_iblPass)
	{
		um_iblPass = std::make_unique<IBLPass>(*um_device, *um_physicalDevice);
		NEURUS_LOG("[UploadManager] Lazily created IBLPass for environment uploads");
	}

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
		// Generate pink-purple fallback equirect (64x32)
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
		NEURUS_LOG("[UploadManager] Using pink-purple fallback equirect");
	}

	// --- 2. Upload equirect to GPU ---
	auto equirectImage = Image::FromImageData(*um_device, *um_physicalDevice,
	                                                  um_queue, um_queueFamilyIndex,
	                                                  equirectData,
	                                                  "Env_Equirect",
	                                                  vk::ImageUsageFlagBits::eStorage);
	if (equirectImage->State() == ImageState::Invalid)
	{
		NEURUS_ERR("[UploadManager] Failed to upload equirect image to GPU");
		return EnvironmentGPU{};
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
		*um_device, *um_physicalDevice,
		vk::Extent2D{kDiffuseRes, kDiffuseRes},
		vk::Format::eR32G32B32A32Sfloat,
		cubeUsage,
		/*mipLevels=*/1,
		Image::ImageType::eCube,
		"Env_DiffuseCubemap");

	auto specularImage = std::make_unique<Image>(
		*um_device, *um_physicalDevice,
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
	auto diffuseSampler = vk::raii::Sampler(*um_device, samplerCI);

	samplerCI.setMaxLod(static_cast<float>(kSpecularMips));
	auto specularSampler = vk::raii::Sampler(*um_device, samplerCI);

	// --- 5. Run IBL convolution ---
	um_iblPass->Generate(um_queue, um_queueFamilyIndex, *equirectImage, *diffuseImage, *specularImage);

	// --- 6. Wrap in Textures and return ---
	EnvironmentGPU gpu;
	gpu.diffuseTexture = std::make_unique<Texture>(
		Texture::FromImage(std::move(diffuseImage), std::move(diffuseSampler)));
	gpu.specularTexture = std::make_unique<Texture>(
		Texture::FromImage(std::move(specularImage), std::move(specularSampler)));

	NEURUS_LOG("[UploadManager] Uploaded environment: diffuse="
	           << kDiffuseRes << "^2 specular=" << kSpecularRes
	           << "^2 x" << kSpecularMips << " mips");

	return gpu;
}

LightGPU UploadManager::UploadLight(const Light& light)
{
	const LightType type = light.light_type;

	// --- Invalid / unhandled light type ---
	if (type == LightType::NONELIGHT)
	{
		NEURUS_ERR("[UploadManager] Unhandled light type: " << static_cast<int>(type));
		return LightGPU{};
	}

	const int uid = light.GetObjectID();

	// --- Sun light: 2D orthographic depth map ---
	if (type == LightType::SUNLIGHT)
	{
		constexpr vk::Extent2D kSunShadowRes{2048, 2048};
		LightGPU lgpu;
		lgpu.shadowDepthMap = std::make_unique<Image>(
			*um_device, *um_physicalDevice,
			kSunShadowRes,
			vk::Format::eD32Sfloat,
			vk::ImageUsageFlagBits::eDepthStencilAttachment |
				vk::ImageUsageFlagBits::eSampled |
				vk::ImageUsageFlagBits::eTransferSrc,
			1,
			Image::ImageType::e2D,
			("SunShadowDepth_Light_" + std::to_string(uid)).c_str());
		lgpu.shadowColorMap.reset();
		NEURUS_LOG("[UploadManager] Created shadow map for light type=" << static_cast<int>(type));
		return lgpu;
	}

	// --- Default: cubemap for point / spot / area lights ---
	{
		constexpr vk::Extent2D kShadowRes{1024, 1024};
		LightGPU lgpu;
		lgpu.shadowDepthMap = std::make_unique<Image>(
			*um_device, *um_physicalDevice,
			kShadowRes,
			vk::Format::eD32Sfloat,
			vk::ImageUsageFlagBits::eDepthStencilAttachment |
				vk::ImageUsageFlagBits::eSampled |
				vk::ImageUsageFlagBits::eTransferSrc,
			1,
			Image::ImageType::eCube,
			("ShadowDepthCubemap_Light_" + std::to_string(uid)).c_str());
		lgpu.shadowColorMap = std::make_unique<Image>(
			*um_device, *um_physicalDevice,
			kShadowRes,
			vk::Format::eR32G32B32A32Sfloat,
			vk::ImageUsageFlagBits::eColorAttachment |
				vk::ImageUsageFlagBits::eSampled |
				vk::ImageUsageFlagBits::eTransferSrc,
			1,
			Image::ImageType::eCube,
			("ShadowColorCubemap_Light_" + std::to_string(uid)).c_str());
		NEURUS_LOG("[UploadManager] Created shadow map for light type=" << static_cast<int>(type));
		return lgpu;
	}
}

// ---------------------------------------------------------------------------
// Variant-based UploadLighting (batch)
// ---------------------------------------------------------------------------

std::unordered_map<int, std::variant<PointLightStruct, SunLightStruct>>
UploadManager::UploadLighting(const std::unordered_map<int, std::shared_ptr<Light>>& lights)
{
	std::unordered_map<int, std::variant<PointLightStruct, SunLightStruct>> result;

	for (const auto& [uid, light] : lights)
	{
		if (!light || !light->is_viewport || !light->is_rendered) continue;
		result[uid] = UploadLighting(*light);
	}

	NEURUS_LOG("[UploadManager] UploadLighting(batch): " << result.size() << " lights converted");
	return result;
}

// ---------------------------------------------------------------------------
// Variant-based UploadLighting (single)
// ---------------------------------------------------------------------------

std::variant<PointLightStruct, SunLightStruct>
UploadManager::UploadLighting(const Light& light)
{
	if (light.light_type == LightType::POINTLIGHT)
	{
		PointLightStruct gpu = {};
		const auto& pos = light.GetPosition();

		gpu.posX = pos.x;
		gpu.posY = pos.y;
		gpu.posZ = pos.z;
		gpu.colorR = light.light_color.r;
		gpu.colorG = light.light_color.g;
		gpu.colorB = light.light_color.b;
		gpu.power = light.light_power;
		gpu.radius = light.light_radius;
		gpu.shadowMapIndex = -1;

		return gpu;
	}
	else if (light.light_type == LightType::SUNLIGHT)
	{
		SunLightStruct gpu = {};
		const auto& dir = light.GetDirection();

		gpu.directionX = dir.x;
		gpu.directionY = dir.y;
		gpu.directionZ = dir.z;
		gpu.colorR = light.light_color.r;
		gpu.colorG = light.light_color.g;
		gpu.colorB = light.light_color.b;
		gpu.power = light.light_power;
		gpu.shadowMapIndex = -1;

		return gpu;
	}

	// NONELIGHT — return default PointLightStruct (empty variant alternative)
	NEURUS_ERR("[UploadManager] Unhandled light type: " << static_cast<int>(light.light_type));
	return PointLightStruct{};
}

} // namespace neurus
