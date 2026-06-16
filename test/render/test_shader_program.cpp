#include <gtest/gtest.h>

/**
 * @brief Tests for ShaderProgram.
 *
 * These tests require a Vulkan device + valid SPIR-V bytecode.
 * In CI environments without GPU, these will be skipped.
 */
class ShaderProgramTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_hasVulkan = false;
		// Full Vulkan init needed for pipeline tests - placeholder
	}

	bool m_hasVulkan = false;
};

TEST_F(ShaderProgramTest, Placeholder_CompilationGuard)
{
	// This test exists to ensure the test binary links correctly.
	// Full shader pipeline tests will be added when GPU test fixtures are complete.
	if (!m_hasVulkan)
	{
		GTEST_SKIP() << "Shader pipeline tests require a full Vulkan device - not yet implemented in test fixture.";
	}

	SUCCEED();
}
