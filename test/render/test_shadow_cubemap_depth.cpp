/**
 * @file test_shadow_cubemap_depth.cpp
 * @brief Shadow occlusion test — pixel-by-pixel mathematical verification.
 *
 * Scene: Light at (0,0,0), cube occluder at (0,0,3) size 1, plane at z=6.
 * Camera at (0,0,0.5) with 90° FOV looking at (0,0,6).
 *
 * Shadow projection: cube at z=3 casts shadow on plane at z=6.
 * InShadow math: ray from (0,0,0) through (px,py,6), enters cube at z=2.5
 *   cx = px * 2.5/6 → shadowed if |cx|≤0.5 and |cy|≤0.5
 *   Shadow region: |px|≤1.2, |py|≤1.2 → 2.4×2.4 square on plane
 */

#include <gtest/gtest.h>
#include "shared/TestVulkanShared.h"
#include "render/passes/ShadowDepthPass.h"
#include "render/passes/ShadowEvalPass.h"
#include "render/passes/GeometryPass.h"
#include "render/passes/AttachmentManager.h"
#include "render/passes/PassContext.h"
#include "render/passes/RenderPassManager.h"
#include "render/buffers/VertexBuffer.h"
#include "render/buffers/IndexBuffer.h"
#include "render/Image.h"
#include "render/Screenshot.h"
#include "Log.h"
#include <gbuffer.vert.h>
#include <gbuffer.frag.h>
#include <stb_image.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

using namespace neurus;

struct TestVertex { float px, py, pz, nx, ny, nz, u, v; };

static std::vector<TestVertex> MakeCubeVerts(float size)
{
	const float h = size * 0.5f;
	std::vector<TestVertex> v;
	v.insert(v.end(), {{-h,-h, h, 0,0,1, 0,0},{-h, h, h, 0,0,1, 0,1},{ h, h, h, 0,0,1, 1,1},{ h,-h, h, 0,0,1, 1,0}});
	v.insert(v.end(), {{ h,-h,-h, 0,0,-1, 0,0},{ h, h,-h, 0,0,-1, 0,1},{-h, h,-h, 0,0,-1, 1,1},{-h,-h,-h, 0,0,-1, 1,0}});
	v.insert(v.end(), {{ h,-h, h, 1,0,0, 0,0},{ h, h, h, 1,0,0, 0,1},{ h, h,-h, 1,0,0, 1,1},{ h,-h,-h, 1,0,0, 1,0}});
	v.insert(v.end(), {{-h,-h,-h,-1,0,0, 0,0},{-h, h,-h,-1,0,0, 0,1},{-h, h, h,-1,0,0, 1,1},{-h,-h, h,-1,0,0, 1,0}});
	v.insert(v.end(), {{-h, h, h, 0,1,0, 0,0},{-h, h,-h, 0,1,0, 0,1},{ h, h,-h, 0,1,0, 1,1},{ h, h, h, 0,1,0, 1,0}});
	v.insert(v.end(), {{-h,-h,-h, 0,-1,0, 0,0},{-h,-h, h, 0,-1,0, 0,1},{ h,-h, h, 0,-1,0, 1,1},{ h,-h,-h, 0,-1,0, 1,0}});
	return v;
}

static std::vector<uint32_t> MakeCubeIdx()
{
	std::vector<uint32_t> idx;
	for (uint32_t f = 0; f < 6; ++f) { uint32_t b = f * 4; idx.insert(idx.end(), {b, b+1, b+2, b, b+2, b+3}); }
	return idx;
}

static std::vector<TestVertex> MakePlaneVertsZ6(float halfSize)
{
	const float h = halfSize;
	return {
		{-h,-h,6.f, 0,0,-1, 0,0}, {h,-h,6.f, 0,0,-1, 1,0},
		{ h, h,6.f, 0,0,-1, 1,1}, {-h, h,6.f, 0,0,-1, 0,1}};
}

static std::vector<uint32_t> MakePlaneIdx() { return {0,1,2, 0,2,3}; }

inline bool InShadow(float px, float py)
{
	const float tEnter = 2.5f / 6.0f;
	return (std::abs(px * tEnter) <= 0.5f) && (std::abs(py * tEnter) <= 0.5f);
}

static std::vector<uint8_t> ReadR8Raw(const vk::raii::Device& device,
	const vk::raii::PhysicalDevice& pd, vk::Queue queue, uint32_t qfi, Image& att)
{
	auto ext = att.Extent();
	{
		vk::CommandPoolCreateInfo pCI(vk::CommandPoolCreateFlagBits::eTransient, qfi);
		vk::raii::CommandPool cp(device, pCI);
		vk::raii::CommandBuffers cb(device, {*cp, vk::CommandBufferLevel::ePrimary, 1});
		cb[0].begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
		auto oldL = att.CurrentLayout();
		vk::AccessFlags sa = {}; vk::PipelineStageFlags ss = vk::PipelineStageFlagBits::eTopOfPipe;
		if (oldL != vk::ImageLayout::eUndefined) { sa = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite; ss = vk::PipelineStageFlagBits::eAllCommands; }
		vk::ImageMemoryBarrier b(sa, vk::AccessFlagBits::eTransferRead, oldL, vk::ImageLayout::eTransferSrcOptimal,
			VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, *att.ImageHandle(),
			vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		cb[0].pipelineBarrier(ss, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, b);
		cb[0].end();
		queue.submit(vk::SubmitInfo({},{},{},1,&(*cb[0])));
		queue.waitIdle();
		att.SetCurrentLayout(vk::ImageLayout::eTransferSrcOptimal);
	}
	return att.ReadImageToBuffer(device, pd, queue, qfi, vk::ImageLayout::eTransferSrcOptimal);
}

// ===========================================================================
// Test Fixture
// ===========================================================================

class ShadowCubemapDepthTest : public VulkanTestShared
{
protected:
	static constexpr uint32_t kRes = 256;
	static constexpr float kFar = 25.0f, kBias = 0.005f, kFov = 90.0f;

	void SetUp() override
	{
		VulkanTestShared::SetUp();
		if (!m_hasVulkan) return;
		auto& pd = PhysicalDevice();

		m_attMgr = std::make_unique<AttachmentManager>(*m_device, pd);
		m_attMgr->Create({kRes, kRes});
		m_rpm = std::make_unique<RenderPassManager>();

		m_geoPass = std::make_unique<GeometryPass>(*m_device, pd, m_queue, m_graphicsQueueFamily, *m_attMgr, *m_rpm,
			gbuffer_vert_spv, sizeof(gbuffer_vert_spv), gbuffer_frag_spv, sizeof(gbuffer_frag_spv));

		m_shadowPass = std::make_unique<ShadowDepthPass>(*m_device, pd, m_queue, m_graphicsQueueFamily, kRes, kFar);
		m_shadowPass->SetLightPosition(glm::vec3(0.f));

		m_evalPass = std::make_unique<ShadowEvalPass>(*m_device, pd, m_attMgr.get(), 1u);
		m_evalPass->SetLight(m_shadowPass->ShadowCubemap(), glm::vec3(0.f), kFar, kBias);

		auto cubeVerts = MakeCubeVerts(1.f);
		auto cubeInds = MakeCubeIdx();
		for (auto& v : cubeVerts) v.pz += 3.f;
		m_cubeVBO = std::make_unique<VertexBuffer>(*m_device, pd, m_queue, m_graphicsQueueFamily, cubeVerts.data(), cubeVerts.size()*sizeof(TestVertex), sizeof(TestVertex), (uint32_t)cubeVerts.size());
		m_cubeIBO = std::make_unique<IndexBuffer>(*m_device, pd, m_queue, m_graphicsQueueFamily, cubeInds.data(), cubeInds.size()*sizeof(uint32_t), (uint32_t)cubeInds.size());

		auto planeVerts = MakePlaneVertsZ6(6.f);
		auto planeInds = MakePlaneIdx();
		m_planeVBO = std::make_unique<VertexBuffer>(*m_device, pd, m_queue, m_graphicsQueueFamily, planeVerts.data(), planeVerts.size()*sizeof(TestVertex), sizeof(TestVertex), (uint32_t)planeVerts.size());
		m_planeIBO = std::make_unique<IndexBuffer>(*m_device, pd, m_queue, m_graphicsQueueFamily, planeInds.data(), planeInds.size()*sizeof(uint32_t), (uint32_t)planeInds.size());

		GeometryRenderItem cubeItem{}; cubeItem.vertexBuffer=m_cubeVBO->buffer(); cubeItem.indexBuffer=m_cubeIBO->buffer(); cubeItem.indexCount=m_cubeIBO->GetIndexCount(); cubeItem.indexType=m_cubeIBO->GetIndexType(); cubeItem.pushConstants.model=glm::mat4(1.f); cubeItem.pushConstants.normalMatrix=glm::mat4(1.f);
		GeometryRenderItem planeItem{}; planeItem.vertexBuffer=m_planeVBO->buffer(); planeItem.indexBuffer=m_planeIBO->buffer(); planeItem.indexCount=m_planeIBO->GetIndexCount(); planeItem.indexType=m_planeIBO->GetIndexType(); planeItem.pushConstants.model=glm::mat4(1.f); planeItem.pushConstants.normalMatrix=glm::mat4(1.f);
		m_geoItems = {cubeItem};
		m_shadowItems = {cubeItem}; // Self-shadow test: cube casts shadow on itself

		m_viewMat = glm::lookAt(glm::vec3(0,0,0.5f), glm::vec3(0,0,6.f), glm::vec3(0,1,0));
		m_projMat = glm::perspective(glm::radians(kFov), 1.f, 0.1f, 100.f);
	}

	void TearDown() override
	{
		m_geoItems.clear(); m_shadowItems.clear(); m_evalPass.reset(); m_shadowPass.reset(); m_geoPass.reset();
		m_rpm.reset(); m_attMgr.reset(); m_planeIBO.reset(); m_planeVBO.reset(); m_cubeIBO.reset(); m_cubeVBO.reset();
		VulkanTestShared::TearDown();
	}

	void TransitionGbufferToColor()
	{
		auto& cmd = BeginCmd();
		for (auto a : {AttachmentName::Position, AttachmentName::Normal, AttachmentName::Albedo, AttachmentName::MetallicRoughness})
			m_attMgr->GetAttachment(a).TransitionLayout(cmd, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
		m_attMgr->GetAttachment(AttachmentName::Depth).TransitionLayout(cmd, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);
		EndSubmitWait(cmd);
	}

	void RenderGeometry() { auto& cmd=BeginCmd(); PassContext ctx; ctx.renderExtent=vk::Extent2D{kRes,kRes}; ctx.viewProj=m_projMat*m_viewMat; ctx.view=m_viewMat; ctx.renderItems=&m_geoItems; m_geoPass->Record(cmd,ctx); EndSubmitWait(cmd); }
	void RenderShadowDepth() { auto& cmd=BeginCmd(); PassContext ctx; ctx.renderExtent=vk::Extent2D{kRes,kRes}; ctx.renderItems=&m_shadowItems; m_shadowPass->Record(cmd,ctx); EndSubmitWait(cmd); }
	void RenderShadowEval() { auto& cmd=BeginCmd(); PassContext ctx; ctx.renderExtent=vk::Extent2D{kRes,kRes}; ctx.frameIndex=0; m_evalPass->Record(cmd,ctx); EndSubmitWait(cmd); }

	std::unique_ptr<AttachmentManager> m_attMgr;
	std::unique_ptr<RenderPassManager> m_rpm;
	std::unique_ptr<GeometryPass> m_geoPass;
	std::unique_ptr<ShadowDepthPass> m_shadowPass;
	std::unique_ptr<ShadowEvalPass> m_evalPass;
	std::unique_ptr<VertexBuffer> m_cubeVBO, m_planeVBO;
	std::unique_ptr<IndexBuffer> m_cubeIBO, m_planeIBO;
	std::vector<GeometryRenderItem> m_geoItems, m_shadowItems;
	glm::mat4 m_viewMat{1.f}, m_projMat{1.f};
};

// ===========================================================================
// Test 1: Pixel-by-pixel Shadow Verification
// ===========================================================================

TEST_F(ShadowCubemapDepthTest, CubemapDepth_MathVerification)
{
	if (!m_hasVulkan) { GTEST_SKIP() << "No Vulkan GPU."; }
	TransitionGbufferToColor(); RenderGeometry(); RenderShadowDepth(); RenderShadowEval();

	auto raw = ReadR8Raw(*m_device, PhysicalDevice(), m_queue, m_graphicsQueueFamily, m_attMgr->GetAttachment(AttachmentName::ShadowIntensity));
	ASSERT_FALSE(raw.empty()); ASSERT_EQ(raw.size(), kRes*kRes);

	uint8_t smin=raw[0], smax=raw[0]; for (auto v:raw) { smin=std::min(smin,v); smax=std::max(smax,v); }
	NEURUS_LOG("[ShadowMath] min=" << (int)smin << " max=" << (int)smax);

	const float tanFov = std::tan(glm::radians(kFov/2));
	const float camDist = 6.f-0.5f;
	uint32_t mismatches=0, shadowedCount=0, litCount=0;

	for (uint32_t y=0; y<kRes; ++y)
	{
		for (uint32_t x=0; x<kRes; ++x)
		{
			uint8_t sv = raw[y*kRes+x];
			float nx = ((float)x+0.5f)*2/kRes-1, ny = ((float)y+0.5f)*2/kRes-1;
			float px=nx*tanFov*camDist, py=ny*tanFov*camDist;
			bool expSh = InShadow(px, py);
			uint8_t exp = expSh?255:0;
			if (expSh) ++shadowedCount; else ++litCount;
			if (std::abs((int)sv-(int)exp)>1) { if (mismatches<5) NEURUS_LOG("[ShadowMath] mismatch ("<<x<<","<<y<<"): actual="<<(int)sv<<" expected="<<(int)exp<<" px="<<px); ++mismatches; }
		}
	}
	NEURUS_LOG("[ShadowMath] shadowed="<<shadowedCount<<" lit="<<litCount<<" mismatches="<<mismatches);
	EXPECT_GT(shadowedCount, 1000u) << "No shadowed pixels";
	EXPECT_GT(litCount, 1000u) << "No lit pixels";
	EXPECT_LE((float)mismatches/(shadowedCount+litCount), 0.05f) << mismatches << " mismatches";
}

// ===========================================================================
// Test 2: Reference Image Regression
// ===========================================================================

TEST_F(ShadowCubemapDepthTest, CubemapDepth_ReferenceImage)
{
	if (!m_hasVulkan) { GTEST_SKIP() << "No Vulkan GPU."; }
	TransitionGbufferToColor(); RenderGeometry(); RenderShadowDepth(); RenderShadowEval();

	auto& sa = m_attMgr->GetAttachment(AttachmentName::ShadowIntensity);
	{ auto& cmd=BeginCmd(); sa.TransitionLayout(cmd, sa.CurrentLayout()!=vk::ImageLayout::eUndefined?sa.CurrentLayout():vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral); EndSubmitWait(cmd); }

	const std::string ref = "../../../test/render/reference/shadow/ShadowIntensity_Occlusion.png";
	const std::string tmp = ref+".tmp";
	std::filesystem::create_directories("../../../test/render/reference/shadow/");
	ASSERT_TRUE(Screenshot::CaptureAttachment(*m_device, PhysicalDevice(), m_queue, m_graphicsQueueFamily, sa, tmp, false));

	if (!std::ifstream(ref).good()) { std::rename(tmp.c_str(), ref.c_str()); GTEST_SKIP() << "Reference generated."; }
	int tw,th,tc,rw,rh,rc;
	unsigned char *tp=stbi_load(tmp.c_str(),&tw,&th,&tc,4), *rp=stbi_load(ref.c_str(),&rw,&rh,&rc,4);
	ASSERT_TRUE(tp&&rp); ASSERT_EQ(tw,rw); ASSERT_EQ(th,rh);
	int bad=0; size_t n=(size_t)tw*th*4;
	for (size_t i=0; i<n; i+=4) { for (int c=0;c<4;++c) if (std::abs((int)tp[i+c]-(int)rp[i+c])>2) {++bad; break;} }
	stbi_image_free(tp); stbi_image_free(rp); std::remove(tmp.c_str());
	EXPECT_EQ(bad, 0) << bad << " pixel(s) differ";
}
