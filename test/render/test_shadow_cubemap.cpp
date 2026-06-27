/**
 * @file test_shadow_cubemap.cpp
 * @brief GPU test: renders cube+plane scene into ShadowDepthPass cubemap,
 *        reads back raw float depth data for all 6 faces, and verifies every
 *        pixel against mathematically-computed expected depth values.
 *
 * Mathematical verification:
 *   - Cube unit at [-0.5, +0.5]^3 positioned at (0,3,0), plane at y=0 spanning [-10,10] in XZ
 *   - Point light at (0, 6, 0), farPlane from Light::point_shadow_far
 *   - Depth = dist(lightPos, worldPos) / farPlane written by fragment shader
 *   - For each pixel (px,py) on each face, ray-cast from light to determine expected depth
 *   - Compare pixel-by-pixel with tolerance +/-3/255 (~0.01176)
 *
 * Reference image regression:
 *   - First run: generates reference PNGs for all 6 faces -> GTEST_SKIP
 *   - Second run: compares pixel-by-pixel with +/-2 tolerance -> PASS
 */

#include <gtest/gtest.h>

#include "shared/TestVulkanShared.h"
#include "shared/TestSimpleShadow.h"

#include "render/passes/ShadowDepthPass.h"
#include "render/passes/GeometryPass.h"
#include "render/RenderContext.h"
#include "render/Image.h"
#include "render/Barrier.h"

#include "shared/TestReferenceImage.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

using namespace neurus;

// ---------------------------------------------------------------------------
// File-level helpers
// ---------------------------------------------------------------------------

/**
 * @brief Finds a host-visible, host-coherent memory type index.
 * @param pd         Physical device for memory property query.
 * @param memReqs    Memory requirements from a buffer allocation.
 * @return Memory type index, or UINT32_MAX if none found.
 */
static uint32_t FindHostVisibleMemType(const vk::raii::PhysicalDevice& pd,
                                        const vk::MemoryRequirements& memReqs)
{
	auto memProps = pd.getMemoryProperties();
	for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
	{
		if ((memReqs.memoryTypeBits & (1u << i)) &&
		    (memProps.memoryTypes[i].propertyFlags &
		     (vk::MemoryPropertyFlagBits::eHostVisible |
		      vk::MemoryPropertyFlagBits::eHostCoherent)))
			return i;
	}
	return UINT32_MAX;
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class ShadowCubemapTest : public VulkanTestShared
{
protected:
	static constexpr uint32_t kRes        = 256;
	// farPlane sourced from Light::point_shadow_far at verification time
	static constexpr float    kTolerance  = 3.0f / 255.0f;  // +/-3 U8 steps in [0,1] range

	void SetUp() override
	{
		VulkanTestShared::SetUp();
		if (!m_hasVulkan) return;

		auto& pd = PhysicalDevice();

		m_shadowDepthPass = std::make_unique<ShadowDepthPass>(
			*m_device, pd, m_queue, m_graphicsQueueFamily, kRes);

		m_renderCache = std::make_unique<RenderCache>(*m_device, pd);
	}

	void TearDown() override
	{
		VulkanTestShared::TearDown();
	}

	// --- Face direction and pixel categorization helpers ---

	/**
	 * @brief Computes world-space direction from light for pixel (sx,sy) on cubemap face.
	 *
	 * Mathematically derived from the lookAt matrices used to compute cubemap
	 * view-projection matrices. For each face, sx,sy in [-1,1] are NDC coordinates;
	 * the returned direction is the normalized world-space vector from the light
	 * through that pixel.
	 */
	static glm::vec3 FaceDirection(uint32_t faceIdx, float sx, float sy)
	{
		struct FC { glm::vec3 fwd, upVec; };
		static const FC kFaces[6] = {
			{{ 1, 0, 0}, {0,-1, 0}}, // 0: +X
			{{-1, 0, 0}, {0,-1, 0}}, // 1: -X
			{{ 0, 1, 0}, {0, 0, 1}}, // 2: +Y
			{{ 0,-1, 0}, {0, 0,-1}}, // 3: -Y
			{{ 0, 0, 1}, {0,-1, 0}}, // 4: +Z
			{{ 0, 0,-1}, {0,-1, 0}}, // 5: -Z
		};
		const auto& fc = kFaces[faceIdx];
		glm::vec3 right = glm::normalize(glm::cross(fc.fwd, fc.upVec));
		glm::vec3 actualUp = glm::normalize(glm::cross(right, fc.fwd));
		return glm::normalize(sx * right + sy * actualUp + fc.fwd);
	}

	// --- Pixel categorization for diagnostic summary ---

	enum class PixelCategory { Cube, Plane, Background };

	/**
	 * @brief Categorize a pixel as cube, plane, or background based on ray intersection.
	 * @param dir       Normalized world-space direction from light through pixel.
	 * @param lightPos  World-space position of the point light.
	 * @param cubePos   World-space position of the cube centre (default origin).
	 * @return PixelCategory indicating what geometry (if any) the ray hits first.
	 */
	static PixelCategory CategorizePixel(const glm::vec3& dir, const glm::vec3& lightPos,
	                                     const glm::vec3& cubePos = glm::vec3(0.0f))
	{
		// Plane y=0 intersection (bounded to [-10, 10] in XZ)
		const float t_plane = -lightPos.y / dir.y;
		bool hitsPlane = false;
		if (t_plane > 0.0f)
		{
			const glm::vec3 hp = lightPos + t_plane * dir;
			hitsPlane = (std::abs(hp.x) <= 10.0f && std::abs(hp.z) <= 10.0f);
		}

		// Cube AABB slab test (cube centered at cubePos, extent ±0.5)
		const float eps = 0.001f;
		float tMin = eps, tMax = 1e10f;
		bool parallelMiss = false;
		for (int axis = 0; axis < 3; ++axis)
		{
			const float centre = (&cubePos.x)[axis];
			const float lo = centre - 0.5f, hi = centre + 0.5f;
			const float origin = (&lightPos.x)[axis];
			const float d       = (&dir.x)[axis];
			if (std::abs(d) > 1e-7f)
			{
				const float t1 = (lo - origin) / d;
				const float t2 = (hi - origin) / d;
				tMin = std::max(tMin, std::min(t1, t2));
				tMax = std::min(tMax, std::max(t1, t2));
			}
			else if (origin < lo || origin > hi)
				parallelMiss = true;
		}
		const bool hitsCube = (!parallelMiss && tMin < tMax && tMin > eps);

		if (hitsCube && tMin < t_plane)
			return PixelCategory::Cube;
		if (hitsPlane)
			return PixelCategory::Plane;
		return PixelCategory::Background;
	}

	// --- Depth-to-u8 conversion for reference image regression ---

	/**
	 * @brief Converts a vector of float depth values [0,1] to uint8_t.
	 * @param depthData  Float depth data (one value per pixel).
	 * @return uint8_t data, clamped and quantized to [0, 255].
	 */
	static std::vector<uint8_t> DepthToU8(const std::vector<float>& depthData)
	{
		std::vector<uint8_t> u8Data(depthData.size());
		for (size_t i = 0; i < depthData.size(); ++i)
		{
			float v = depthData[i];
			v = std::max(0.0f, std::min(1.0f, v));
			u8Data[i] = static_cast<uint8_t>(v * 255.0f + 0.5f);
		}
		return u8Data;
	}

	// --- Expected depth computation ---

	/**
	 * @brief Computes expected depth for a specific cubemap face.
	 *
	 * Uses FaceDirection() to determine the world-space ray from the light
	 * for the given pixel, then intersects with the cube AABB and plane.
	 * @param faceIdx Cubemap face index 0-5.
	 * @param cubePos World-space position of the cube centre (default origin).
	 * @return Expected depth = t / farPlane where t is distance to first hit.
	 */
	static float ComputeExpectedDepth(uint32_t faceIdx,
	                                  uint32_t px, uint32_t py,
	                                  const glm::vec3& lightPos,
	                                  float farPlane, uint32_t res,
	                                  const glm::vec3& cubePos)
	{
		const float sx = (2.0f * static_cast<float>(px) + 1.0f) / static_cast<float>(res) - 1.0f;
		const float sy = (2.0f * static_cast<float>(py) + 1.0f) / static_cast<float>(res) - 1.0f;

		const glm::vec3 dir = glm::normalize(FaceDirection(faceIdx, sx, sy));

		// --- Plane y=0 intersection ---
		const float t_plane = -lightPos.y / dir.y;
		bool hitsPlaneInBounds = false;
		if (t_plane > 0.0f)
		{
			const glm::vec3 hitPoint = lightPos + t_plane * dir;
			if (std::abs(hitPoint.x) <= 10.0f && std::abs(hitPoint.z) <= 10.0f)
				hitsPlaneInBounds = true;
		}

		// --- Cube AABB slab test (cube centered at cubePos, extent ±0.5) ---
		const float eps = 0.001f;
		float t_min_cube = eps;
		float t_max_cube = 1e10f;

		const float cx = cubePos.x, cy = cubePos.y, cz = cubePos.z;

		if (std::abs(dir.x) > 1e-7f)
		{
			const float t1 = (cx - 0.5f - lightPos.x) / dir.x;
			const float t2 = (cx + 0.5f - lightPos.x) / dir.x;
			t_min_cube = std::max(t_min_cube, std::min(t1, t2));
			t_max_cube = std::min(t_max_cube, std::max(t1, t2));
		}
		else if (lightPos.x < cx - 0.5f || lightPos.x > cx + 0.5f)
			t_min_cube = 1e10f;

		if (std::abs(dir.y) > 1e-7f)
		{
			const float t1 = (cy - 0.5f - lightPos.y) / dir.y;
			const float t2 = (cy + 0.5f - lightPos.y) / dir.y;
			t_min_cube = std::max(t_min_cube, std::min(t1, t2));
			t_max_cube = std::min(t_max_cube, std::max(t1, t2));
		}
		else if (lightPos.y < cy - 0.5f || lightPos.y > cy + 0.5f)
			t_min_cube = 1e10f;

		if (std::abs(dir.z) > 1e-7f)
		{
			const float t1 = (cz - 0.5f - lightPos.z) / dir.z;
			const float t2 = (cz + 0.5f - lightPos.z) / dir.z;
			t_min_cube = std::max(t_min_cube, std::min(t1, t2));
			t_max_cube = std::min(t_max_cube, std::max(t1, t2));
		}
		else if (lightPos.z < cz - 0.5f || lightPos.z > cz + 0.5f)
			t_min_cube = 1e10f;

		const bool hitsCube = (t_min_cube < t_max_cube && t_min_cube > eps);

		if (hitsCube && t_min_cube < t_plane)
			return t_min_cube / farPlane;
		if (hitsPlaneInBounds)
			return t_plane / farPlane;
		return 1.0f;
	}

	// --- Render data ---
	std::unique_ptr<ShadowDepthPass> m_shadowDepthPass;
	std::unique_ptr<RenderCache> m_renderCache;
};

// ===========================================================================
// All Faces Cubemap Depth Verification Test
// ===========================================================================

TEST_F(ShadowCubemapTest, AllFacesDepth)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "No Vulkan-capable GPU found.";
	}

	auto& pd = PhysicalDevice();

	// -------------------------------------------------------------------
	// Step 1: Build scene geometry
	// -------------------------------------------------------------------
	auto shadowRes = neurus::test::LoadSimpleShadow(
		*m_device, pd, m_queue, m_graphicsQueueFamily);

	const auto& renderItems = shadowRes.renderItems;
	ASSERT_EQ(renderItems.size(), 2u) << "Expected 2 meshes (cube + plane)";

	// -------------------------------------------------------------------
	// Step 2: Create colour cubemap for verification readback
	// -------------------------------------------------------------------
	Image verifyCube(*m_device, pd,
		vk::Extent2D(kRes, kRes),
		vk::Format::eR32G32B32A32Sfloat,
		vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
		1u, Image::ImageType::eCube,
		"VerificationCubemap");

	// Transition colour cubemap to colour-attachment layout
	{
		auto& cmd = BeginCmd();
		vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6);
		Barrier::Transition(*cmd, verifyCube, ImageState::ColorAttachment, range);
		EndSubmitWait(cmd);
	}

	// -------------------------------------------------------------------
	// Step 3: Light position is read from ctx.scene->light_list at Record() time.
	// -------------------------------------------------------------------
	const glm::vec3 lightPos = shadowRes.scene->light_list.begin()->second->GetPosition();
	const glm::vec3 viewPos = shadowRes.cubePos;

	// -------------------------------------------------------------------
	// Step 4: Render all 6 faces into depth cubemap + optional colour output.
	// -------------------------------------------------------------------
	{
		auto& cmd = BeginCmd();

		RenderContext ctx{};
		ctx.renderExtent = vk::Extent2D(kRes, kRes);
		ctx.renderItems  = &renderItems;
		ctx.lightUID     = shadowRes.scene->light_list.begin()->first;
		ctx.scene        = shadowRes.scene.get();
		ctx.optionalColorView   = verifyCube.ArrayView();
		ctx.optionalColorFormat = vk::Format::eR32G32B32A32Sfloat;
		m_shadowDepthPass->Record(*cmd, *m_renderCache, ctx);

		// Transition colour cubemap for readback (must happen after rendering)
		vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6);
		Barrier::Transition(*cmd, verifyCube, ImageState::TransferSrc, range);

		EndSubmitWait(cmd);
	}

	// -------------------------------------------------------------------
	// Step 5: Read back each face layer and verify against expected depth
	// -------------------------------------------------------------------
	const vk::DeviceSize bufSize = static_cast<vk::DeviceSize>(kRes) * kRes * 4 * sizeof(float);
	bool anyReferenceGenerated = false;

	for (uint32_t face = 0; face < 6; ++face)
	{
		NEURUS_LOG("[AllFacesDepth] Reading back face " << face);

		// 5a. Copy face layer -> staging buffer
		vk::BufferCreateInfo stagingCI({}, bufSize, vk::BufferUsageFlagBits::eTransferDst);
		vk::raii::Buffer stagingBuf(*m_device, stagingCI);

		auto memReqs = stagingBuf.getMemoryRequirements();
		uint32_t memType = FindHostVisibleMemType(pd, memReqs);
		ASSERT_LT(memType, UINT32_MAX) << "No host-visible+coherent memory type";

		vk::MemoryAllocateInfo allocInfo(memReqs.size, memType);
		vk::raii::DeviceMemory stagingMem(*m_device, allocInfo);
		stagingBuf.bindMemory(*stagingMem, 0);

		{
			auto& cmd = BeginCmd();
			vk::BufferImageCopy copyRegion{};
			copyRegion.bufferOffset      = 0;
			copyRegion.bufferRowLength   = 0;
			copyRegion.bufferImageHeight = 0;
			copyRegion.imageSubresource  = vk::ImageSubresourceLayers(
				vk::ImageAspectFlagBits::eColor, 0, face, 1);
			copyRegion.imageOffset = vk::Offset3D(0, 0, 0);
			copyRegion.imageExtent = vk::Extent3D(kRes, kRes, 1);
			cmd.copyImageToBuffer(
				*verifyCube.ImageHandle(),
				vk::ImageLayout::eTransferSrcOptimal,
				*stagingBuf,
				copyRegion);
			EndSubmitWait(cmd);
		}

		// 5b. Map and extract depth data (R channel of float RGBA)
		void* mapped = stagingMem.mapMemory(0, bufSize);
		std::vector<float> depthData(kRes * kRes);
		{
			const float* rgbaFloats = static_cast<const float*>(mapped);
			for (uint32_t i = 0; i < kRes * kRes; ++i)
				depthData[i] = rgbaFloats[i * 4];
		}

		// Debug: depth range summary
		{
			float minVal = depthData[0], maxVal = depthData[0];
			int zeroCount = 0, oneCount = 0;
			for (const float v : depthData)
			{
				minVal = std::min(minVal, v);
				maxVal = std::max(maxVal, v);
				if (v == 0.0f) zeroCount++;
				if (v >= 0.999f && v <= 1.001f) oneCount++;
			}
			std::cout << "[Face " << face << "] Depth: min=" << minVal
			          << " max=" << maxVal << " zeros=" << zeroCount
			          << " ones~=" << oneCount << " total=" << depthData.size()
			          << std::endl;
		}

		stagingMem.unmapMemory();

		// 5c. Pixel-by-pixel mathematical verification
		int cubePixels  = 0;
		int planePixels = 0;
		int bgPixels    = 0;
		int badPixels   = 0;

		for (uint32_t py = 0; py < kRes; ++py)
		{
			for (uint32_t px = 0; px < kRes; ++px)
			{
				const float actual   = depthData[py * kRes + px];
				const float expected =
					ComputeExpectedDepth(face, px, py, lightPos, Light::point_shadow_far, kRes, viewPos);

				// Categorize pixel
				const float sxV = (2.f * static_cast<float>(px) + 1.f) / static_cast<float>(kRes) - 1.f;
				const float syV = (2.f * static_cast<float>(py) + 1.f) / static_cast<float>(kRes) - 1.f;
				const glm::vec3 dir = glm::normalize(FaceDirection(face, sxV, syV));
				auto cat = CategorizePixel(dir, lightPos, viewPos);

				if (cat == PixelCategory::Cube)       cubePixels++;
				else if (cat == PixelCategory::Plane)  planePixels++;
				else                                   bgPixels++;

				const float diff = std::abs(actual - expected);
				if (diff > kTolerance)
				{
					badPixels++;
					if (badPixels <= 10)
					{
						const char* catStr = (cat == PixelCategory::Cube) ? "cube"
						                   : (cat == PixelCategory::Plane) ? "plane" : "bg";
						std::cout << "BAD PIXEL (face=" << face
						          << " px=" << px << " py=" << py
						          << "): actual=" << actual
						          << " expected=" << expected
						          << " diff=" << diff
						          << " category=" << catStr
						          << std::endl;
					}
				}
			}
		}

		std::cout << "[Face " << face << "] Cube:" << cubePixels
		          << " Plane:" << planePixels << " BG:" << bgPixels
		          << " Bad:" << badPixels
		          << " (tol=" << kTolerance << ")" << std::endl;

		EXPECT_LT(badPixels, 1)
			<< "Face " << face << ": " << badPixels
			<< " pixels exceed tolerance " << kTolerance;

		// Face-specific sanity checks
		if (face == 3)  // -Y: sees cube + plane
		{
			EXPECT_GT(cubePixels, 50)
				<< "Face -Y: cube should cover at least 50 pixels";
			EXPECT_GT(planePixels, 1000)
				<< "Face -Y: plane should cover most non-cube pixels";
		}
		else if (face == 2)  // +Y: looks up, sees nothing
		{
			EXPECT_EQ(cubePixels, 0)   << "Face +Y: should see no cube pixels";
			EXPECT_EQ(planePixels, 0)  << "Face +Y: should see no plane pixels";
			EXPECT_GT(bgPixels, 0)     << "Face +Y: all pixels should be background";
		}

		// 5d. Reference image regression
		{
			const std::string refPath =
				std::string(neurus::test::kReferenceDir)
				+ "shadow/CubemapDepth_Face" + std::to_string(face) + ".png";
			const std::string tmpPath = refPath + ".tmp";

			// Convert depth to u8
			std::vector<uint8_t> u8Data = DepthToU8(depthData);

			ImageData img(u8Data.data(), kRes, kRes, vk::Format::eR8Unorm);
			const bool captured = img.SavePNG(tmpPath);
			ASSERT_TRUE(captured) << "Failed to save depth map for face " << face;

			const int refResult = neurus::test::CheckReferenceOrGenerate(refPath, 2);
			if (refResult < 0)
			{
				anyReferenceGenerated = true;
			}
			else
			{
				EXPECT_EQ(refResult, 0)
					<< refResult << " pixel(s) differ for face " << face;
			}
		}
	}

	if (anyReferenceGenerated)
		GTEST_SKIP() << "One or more reference images generated. Re-run the test.";
}
