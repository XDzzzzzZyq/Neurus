/**
 * @file TestVulkanFixture.h
 * @brief Shared test fixture base class for GPU-dependent tests.
 *
 * Consolidates ~60 lines of duplicated Vulkan bootstrap (Instance → PhysicalDevice
 * → Queue → Device → CommandPool → CommandBuffers) from 6 test files into one
 * reusable base class, plus common helpers (BeginCmd, EndSubmitWait, ResolveAssetPath).
 */

#pragma once

#define VK_USE_PLATFORM_WIN32_KHR

#include <gtest/gtest.h>

#include <vulkan/vulkan_raii.hpp>

#include <fstream>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Shared GPU test fixture — override SetUp/TearDown for non-standard setups
// ---------------------------------------------------------------------------

/**
 * @brief Base test fixture for GPU-dependent Neurus tests.
 *
 * SetUp creates:
 *   - vk::Instance (with surface + win32-surface extensions, debug-utils in Debug)
 *   - Enumerates PhysicalDevices, picks discrete GPU when available
 *   - Finds a graphics queue family
 *   - Creates a Device + Queue
 *   - Creates CommandPool + CommandBuffers (one-shot, reset-capable)
 *
 * TearDown calls m_device->waitIdle().
 *
 * Inheriting test fixtures SHOULD call VulkanTestFixture::SetUp() in their
 * own SetUp when they want the standard bootstrap, and
 * VulkanTestFixture::TearDown() in their TearDown.
 */
class VulkanTestFixture : public ::testing::Test
{
protected:
	void SetUp() override;
	void TearDown() override;

	// --- Vulkan availability ---
	bool HasVulkan() const { return m_hasVulkan; }

	// --- Physical device accessor (most common operation) ---
	vk::raii::PhysicalDevice& PhysicalDevice()
	{
		return m_physicalDevices[m_selectedPdIndex];
	}

	// --- Command buffer helpers ---
	vk::raii::CommandBuffer& BeginCmd();
	void EndSubmitWait(vk::raii::CommandBuffer& cmd);

	// --- Asset path resolution ---
	/**
	 * @brief Resolve an asset path relative to the project root.
	 *
	 * CTest runs from build/debug/Debug/ (MSVC) or build/debug/ (single-config).
	 * This tries multiple relative paths, falling back to the first candidate.
	 */
	static std::string ResolveAssetPath(const char* assetRelative);

	// --- Vulkan state (destructor order: reverse declaration) ---
	bool m_hasVulkan = false;
	vk::raii::Context m_context;
	std::unique_ptr<vk::raii::Instance> m_instance;
	vk::raii::PhysicalDevices m_physicalDevices = nullptr;
	uint32_t m_selectedPdIndex = 0;
	std::unique_ptr<vk::raii::Device> m_device;
	uint32_t m_graphicsQueueFamily = 0;
	vk::Queue m_queue = nullptr;
	std::unique_ptr<vk::raii::CommandPool> m_commandPool;
	vk::raii::CommandBuffers m_commandBuffers = nullptr;
};
