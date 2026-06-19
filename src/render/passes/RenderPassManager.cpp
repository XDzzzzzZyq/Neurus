#include "passes/RenderPassManager.h"

#include <cassert>
#include <stdexcept>

namespace neurus {

// ---------------------------------------------------------------------------
// Static helpers - pass configuration
// ---------------------------------------------------------------------------

/** @brief Returns color attachment load op for a pass type. */
static vk::AttachmentLoadOp colorLoadOpFor(RenderPassManager::PassType passType)
{
	switch (passType)
	{
	case RenderPassManager::PassType::G_BUFFER:
	case RenderPassManager::PassType::LIGHTING:
		return vk::AttachmentLoadOp::eClear;
	case RenderPassManager::PassType::SHADOW:
		return vk::AttachmentLoadOp::eDontCare;
	case RenderPassManager::PassType::COMPOSITE:
	case RenderPassManager::PassType::POST_FX:
		return vk::AttachmentLoadOp::eDontCare;
	}
	return vk::AttachmentLoadOp::eDontCare;
}

/** @brief Returns color attachment store op for a pass type. */
static vk::AttachmentStoreOp colorStoreOpFor(RenderPassManager::PassType passType)
{
	switch (passType)
	{
	case RenderPassManager::PassType::G_BUFFER:
	case RenderPassManager::PassType::LIGHTING:
	case RenderPassManager::PassType::COMPOSITE:
	case RenderPassManager::PassType::POST_FX:
		return vk::AttachmentStoreOp::eStore;
	case RenderPassManager::PassType::SHADOW:
		return vk::AttachmentStoreOp::eDontCare;
	}
	return vk::AttachmentStoreOp::eStore;
}

/** @brief Returns depth attachment load op for a pass type. */
static vk::AttachmentLoadOp depthLoadOpFor(RenderPassManager::PassType passType)
{
	switch (passType)
	{
	case RenderPassManager::PassType::G_BUFFER:
	case RenderPassManager::PassType::SHADOW:
		return vk::AttachmentLoadOp::eClear;
	case RenderPassManager::PassType::LIGHTING:
	case RenderPassManager::PassType::COMPOSITE:
	case RenderPassManager::PassType::POST_FX:
		return vk::AttachmentLoadOp::eDontCare;
	}
	return vk::AttachmentLoadOp::eDontCare;
}

/** @brief Returns depth attachment store op for a pass type. */
static vk::AttachmentStoreOp depthStoreOpFor(RenderPassManager::PassType passType)
{
	switch (passType)
	{
	case RenderPassManager::PassType::G_BUFFER:
	case RenderPassManager::PassType::SHADOW:
		return vk::AttachmentStoreOp::eStore;
	case RenderPassManager::PassType::LIGHTING:
	case RenderPassManager::PassType::COMPOSITE:
	case RenderPassManager::PassType::POST_FX:
		return vk::AttachmentStoreOp::eDontCare;
	}
	return vk::AttachmentStoreOp::eDontCare;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void RenderPassManager::BeginPass(vk::CommandBuffer cmdBuf,
                                  PassType passType,
                                  std::span<const vk::ImageView> colorImageViews,
                                  const vk::ImageView* pDepthImageView,
                                  std::span<const vk::ClearValue> clearValues,
                                  vk::Extent2D renderExtent)
{
	const uint32_t colorCount = static_cast<uint32_t>(colorImageViews.size());
	const uint32_t expectedColorCount = ColorAttachmentCount(passType);

	if (colorCount != expectedColorCount)
	{
		throw std::invalid_argument(
			"RenderPassManager::BeginPass: color attachment count mismatch "
			"(expected " + std::to_string(expectedColorCount) +
			", got " + std::to_string(colorCount) + ")");
	}

	const bool depthProvided = (pDepthImageView != nullptr);
	const bool depthExpected = HasDepth(passType);

	if (depthProvided != depthExpected)
	{
		throw std::invalid_argument(
			"RenderPassManager::BeginPass: depth attachment mismatch "
			"(expected " + std::string(depthExpected ? "present" : "absent") +
			", got " + std::string(depthProvided ? "present" : "absent") + ")");
	}

	// --- Build color attachment infos ---
	const auto colorLoadOp = colorLoadOpFor(passType);
	const auto colorStoreOp = colorStoreOpFor(passType);

	std::vector<vk::RenderingAttachmentInfo> colorAttachmentInfos;
	colorAttachmentInfos.reserve(colorCount);

	for (uint32_t i = 0; i < colorCount; ++i)
	{
		colorAttachmentInfos.push_back(vk::RenderingAttachmentInfo(
			colorImageViews[i],
			vk::ImageLayout::eColorAttachmentOptimal,  // imageLayout
			vk::ResolveModeFlagBits::eNone,             // resolveMode
			nullptr,                                     // resolveImageView
			vk::ImageLayout::eUndefined,                 // resolveImageLayout
			colorLoadOp,
			colorStoreOp,
			(i < static_cast<uint32_t>(clearValues.size())) ? clearValues[i] : vk::ClearValue{}));
	}

	// --- Build depth attachment info ---
	vk::RenderingAttachmentInfo depthAttachmentInfo;
	const vk::RenderingAttachmentInfo* pDepthInfo = nullptr;

	if (depthProvided)
	{
		const auto depthLoadOp = depthLoadOpFor(passType);
		const auto depthStoreOp = depthStoreOpFor(passType);

		// Depth clear value is the last clear value (after all color clear values)
		const size_t depthClearIndex = colorCount;

		depthAttachmentInfo = vk::RenderingAttachmentInfo(
			*pDepthImageView,
			vk::ImageLayout::eDepthStencilAttachmentOptimal,
			vk::ResolveModeFlagBits::eNone,
			nullptr,
			vk::ImageLayout::eUndefined,
			depthLoadOp,
			depthStoreOp,
			(depthClearIndex < clearValues.size()) ? clearValues[depthClearIndex]
			                                       : vk::ClearValue{});
		pDepthInfo = &depthAttachmentInfo;
	}

	// --- Build rendering info ---
	vk::RenderingInfo renderingInfo(
		{},                                                            // flags
		vk::Rect2D({0, 0}, renderExtent),                             // renderArea
		1,                                                             // layerCount
		0,                                                             // viewMask
		colorAttachmentInfos,                                          // colorAttachments
		pDepthInfo,                                                    // pDepthAttachment
		nullptr                                                        // pStencilAttachment
	);

	cmdBuf.beginRendering(renderingInfo);
}

void RenderPassManager::EndPass(vk::CommandBuffer cmdBuf)
{
	cmdBuf.endRendering();
}

// ---------------------------------------------------------------------------
// Static queries
// ---------------------------------------------------------------------------

uint32_t RenderPassManager::ColorAttachmentCount(PassType passType)
{
	switch (passType)
	{
	case PassType::G_BUFFER:   return 4;
	case PassType::LIGHTING:   return 1;
	case PassType::SHADOW:     return 0;
	case PassType::COMPOSITE:  return 1;
	case PassType::POST_FX:    return 1;
	}
	return 0;
}

bool RenderPassManager::HasDepth(PassType passType)
{
	switch (passType)
	{
	case PassType::G_BUFFER:   return true;
	case PassType::LIGHTING:   return false;
	case PassType::SHADOW:     return true;
	case PassType::COMPOSITE:  return false;
	case PassType::POST_FX:    return false;
	}
	return false;
}

std::vector<vk::ClearValue> RenderPassManager::PresetClearValues(PassType passType)
{
	std::vector<vk::ClearValue> result;
	const uint32_t colorCount = ColorAttachmentCount(passType);
	const bool hasDepth = HasDepth(passType);

	// Color clear values - all black (0, 0, 0, 0)
	const vk::ClearValue blackColor(vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}));
	result.insert(result.end(), colorCount, blackColor);

	// Depth clear value - far plane (1.0f)
	if (hasDepth)
	{
		const vk::ClearValue depthClear(vk::ClearDepthStencilValue(1.0f, 0));
		result.push_back(depthClear);
	}

	return result;
}

} // namespace neurus
