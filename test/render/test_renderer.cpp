#include <gtest/gtest.h>

/**
 * @brief Tests for Renderer.
 *
 * These tests require a full Vulkan pipeline with swapchain.
 * In CI environments without GPU, these will be skipped.
 */
class RendererTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_hasVulkan = false;
	}

	bool m_hasVulkan = false;
};

TEST_F(RendererTest, Placeholder_CompilationGuard)
{
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "Renderer tests require a full Vulkan pipeline - not yet implemented in test fixture.";
	}

	SUCCEED();
}
