#include <gtest/gtest.h>

/**
 * @brief Tests for Swapchain.
 *
 * These tests require a Vulkan surface + device.
 * In CI environments without GPU, these will be skipped.
 */
class SwapchainTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_hasVulkan = false;
	}

	bool m_hasVulkan = false;
};

TEST_F(SwapchainTest, Placeholder_CompilationGuard)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "Swapchain tests require a Vulkan device + surface - not yet implemented in test fixture.";
	}

	SUCCEED();
}
