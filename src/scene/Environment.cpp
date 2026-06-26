/**
 * @file Environment.cpp
 * @brief Implementation of Environment - IBL environment map source object.
 */

#include "scene/Environment.h"
#include "render/Image.h"
#include "render/Texture.h"
#include "core/Log.h"

#include <vector>

namespace neurus
{

// -----------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------

Environment::Environment()
{
	o_type = ObjectID::GOType::GO_ENVIR;
	o_name = "Environment";
}

Environment::~Environment() = default;

// -----------------------------------------------------------------------
// File path
// -----------------------------------------------------------------------

void Environment::SetEquirectPath(const std::string& path)
{
	m_equirectPath = path;
	m_dirty = true;
}

const std::string& Environment::GetEquirectPath() const
{
	return m_equirectPath;
}

// -----------------------------------------------------------------------
// Intensity
// -----------------------------------------------------------------------

void Environment::SetIntensity(float i)
{
	m_intensity = i;
}

float Environment::GetIntensity() const
{
	return m_intensity;
}

// -----------------------------------------------------------------------
// Rotation
// -----------------------------------------------------------------------

void Environment::SetRotation(float r)
{
	m_rotation = r;
}

float Environment::GetRotation() const
{
	return m_rotation;
}

// -----------------------------------------------------------------------
// Dirty tracking
// -----------------------------------------------------------------------

bool Environment::IsDirty() const
{
	return m_dirty;
}

void Environment::ClearDirty()
{
	m_dirty = false;
}

// -----------------------------------------------------------------------
// IBL texture ownership
// -----------------------------------------------------------------------

void Environment::BuildIBLTextures(const vk::raii::Device& device,
                                 const vk::raii::PhysicalDevice& physicalDevice)
{
	// Already initialised — nothing to do
	if (m_diffuseTexture && m_specularTexture)
	{
		return;
	}

	NEURUS_LOG("[Environment] BuildIBLTextures: creating cubemap Images + samplers + Textures");

	// --- Cubemap Image creation ---
	constexpr uint32_t kDiffuseRes  = 64;
	constexpr uint32_t kSpecularRes = 2048;
	constexpr uint32_t kSpecularMips = 8;

	const vk::ImageUsageFlags cubeUsage =
	    vk::ImageUsageFlagBits::eStorage
	    | vk::ImageUsageFlagBits::eSampled
	    | vk::ImageUsageFlagBits::eTransferSrc;

	auto diffuseImage = std::make_unique<Image>(
	    device, physicalDevice,
	    vk::Extent2D{kDiffuseRes, kDiffuseRes},
	    vk::Format::eR32G32B32A32Sfloat,
	    cubeUsage,
	    /*mipLevels=*/1,
	    Image::ImageType::eCube,
	    "Env_DiffuseCubemap");

	auto specularImage = std::make_unique<Image>(
	    device, physicalDevice,
	    vk::Extent2D{kSpecularRes, kSpecularRes},
	    vk::Format::eR32G32B32A32Sfloat,
	    cubeUsage,
	    /*mipLevels=*/kSpecularMips,
	    Image::ImageType::eCube,
	    "Env_SpecularCubemap");

	// --- Create cubemap samplers ---
	auto diffuseSampler = CreateCubemapSampler(device, 1);
	auto specularSampler = CreateCubemapSampler(device, kSpecularMips);

	// --- Wrap in Textures ---
	m_diffuseTexture = std::make_unique<Texture>(
	    Texture::FromImage(std::move(diffuseImage), std::move(diffuseSampler)));
	m_specularTexture = std::make_unique<Texture>(
	    Texture::FromImage(std::move(specularImage), std::move(specularSampler)));

	NEURUS_LOG("[Environment] Cubemaps ready: diffuse " << kDiffuseRes << "px (1 mip), specular "
	          << kSpecularRes << "px (" << kSpecularMips << " mips)");
}

Image* Environment::GetCubemapDiffuse() const
{
	return m_diffuseTexture ? m_diffuseTexture->GetImage() : nullptr;
}

Image* Environment::GetCubemapSpecular() const
{
	return m_specularTexture ? m_specularTexture->GetImage() : nullptr;
}

Texture* Environment::GetDiffuseTexture() const
{
	return m_diffuseTexture.get();
}

Texture* Environment::GetSpecularTexture() const
{
	return m_specularTexture.get();
}

// -----------------------------------------------------------------------
// Static factories
// -----------------------------------------------------------------------

vk::raii::Sampler Environment::CreateCubemapSampler(const vk::raii::Device& device,
                                                      uint32_t mipLevels)
{
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
		static_cast<float>(mipLevels),
		vk::BorderColor::eFloatTransparentBlack,
		VK_FALSE);

	return vk::raii::Sampler(device, samplerCI);
}

std::unique_ptr<Image> Environment::GenerateFallbackImage(
    const vk::raii::Device& device,
    const vk::raii::PhysicalDevice& physicalDevice,
    vk::Queue queue,
    uint32_t queueFamilyIndex)
{
	constexpr uint32_t eqWidth  = 64;
	constexpr uint32_t eqHeight = 32;
	constexpr float pinkR = 1.0f;
	constexpr float pinkG = 0.0f;
	constexpr float pinkB = 0.5f;

	std::vector<float> pixels(static_cast<size_t>(eqWidth) * eqHeight * 4, 0.0f);
	for (size_t i = 0; i < pixels.size(); i += 4)
	{
		pixels[i + 0] = pinkR;
		pixels[i + 1] = pinkG;
		pixels[i + 2] = pinkB;
		pixels[i + 3] = 1.0f;
	}

	ImageData imageData(pixels.data(), eqWidth, eqHeight, vk::Format::eR32G32B32A32Sfloat);
	auto image = Image::FromImageData(device, physicalDevice, queue, queueFamilyIndex,
	                                  imageData, "Env_FallbackEquirect");

	NEURUS_LOG("[Environment] Created pink-purple fallback equirect (" << eqWidth << "x" << eqHeight << ")");
	return image;
}

} // namespace neurus
