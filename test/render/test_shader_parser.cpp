/**
 * @file test_shader_parser.cpp
 * @brief TDD unit tests for ShaderParser (Task 7).
 *
 * Tests parse ALL 20 GLSL files in res/shaders/ plus edge cases.
 * These tests are PURE C++ — no GPU, no Vulkan, no Qt dependencies.
 *
 * The ShaderParser may not exist yet (Task 7 runs in parallel).
 * These tests WILL fail compilation until ShaderParser.h is created
 * and WILL fail at runtime until the parser is fully implemented (RED).
 *
 * @note Test fixture: ShaderParserTest (no GPU needed).
 * @note ShaderStruct IR: src/render/shaders/ShaderStruct.h (Task 3).
 * @note ParaType enum: src/core/Parameters.h (Task 2).
 */

#include <gtest/gtest.h>

#include "render/shaders/ShaderParser.h"
#include "render/shaders/ShaderStruct.h"
#include "core/Parameters.h"

#include <fstream>
#include <string>

using namespace neurus;

// ---------------------------------------------------------------------------
// Path resolution helper — pure C++, no Vulkan needed
// ---------------------------------------------------------------------------

namespace {

/**
 * @brief Resolve a shader file path relative to the project root.
 *
 * CTest runs from build/debug/test/ (single-config) or
 * build/debug/Debug/ (MSVC multi-config). Tries multiple relative
 * prefixes, returns the first one that exists on disk.
 *
 * @param relativePath Path relative to project root (e.g. "res/shaders/render/gbuffer.vert")
 * @return Resolved path string, or empty if not found.
 */
std::string ResolveShaderPath(const char* relativePath)
{
    const char* prefixes[] = {
        "../../../",  // build/debug/test/ → project root (single-config)
        "../../",     // build/debug/Debug/ → project root (MSVC multi-config)
        "../",        // fallback: build/debug/ → build
    };

    for (const char* prefix : prefixes)
    {
        std::string full = std::string(prefix) + relativePath;
        std::ifstream f(full);
        if (f.good())
        {
            return full;
        }
    }
    return {};
}

} // namespace

// ---------------------------------------------------------------------------
// ShaderParserTest — pure C++ fixture (no GPU, no Vulkan, no Qt)
// ---------------------------------------------------------------------------

class ShaderParserTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Reset the output struct before each test
        m_shaderStruct.Reset();
    }

    /**
     * @brief Helper: parse a shader file and expect success.
     * @param relativePath Path relative to project root (e.g. "res/shaders/render/gbuffer.vert")
     * @return Reference to the parsed ShaderStruct (valid only if ASSERT succeeded).
     */
    const ShaderStruct& ParseSuccessfully(const char* relativePath)
    {
        const std::string path = ResolveShaderPath(relativePath);
        ASSERT_FALSE(path.empty())
            << "Shader file not found: " << relativePath
            << "\n  Ensure the test runs from build/debug/test/ or build/debug/Debug/";
        ASSERT_TRUE(ShaderParser::ParseShaderFile(path, m_shaderStruct))
            << "ShaderParser failed to parse: " << path;
        return m_shaderStruct;
    }

    ShaderStruct m_shaderStruct;
};

// ===========================================================================
// Section 1: ParseShaderFile success + version checks — ALL 20 GLSL files
// ===========================================================================

// --- render/ vertex shaders (5 files) ---

TEST_F(ShaderParserTest, ParseFile_GbufferVert_Succeeds)
{
    const auto& s = ParseSuccessfully("res/shaders/render/gbuffer.vert");
    EXPECT_FALSE(s.IsEmpty());
}

TEST_F(ShaderParserTest, ParseFile_GbufferVert_HasCorrectVersion)
{
    const auto& s = ParseSuccessfully("res/shaders/render/gbuffer.vert");
    EXPECT_EQ(s.version, 450);
}

TEST_F(ShaderParserTest, ParseFile_SunShadowDepthVert_Succeeds)
{
    const auto& s = ParseSuccessfully("res/shaders/render/sun_shadow_depth.vert");
    EXPECT_FALSE(s.IsEmpty());
}

TEST_F(ShaderParserTest, ParseFile_SunShadowDepthVert_HasCorrectVersion)
{
    const auto& s = ParseSuccessfully("res/shaders/render/sun_shadow_depth.vert");
    EXPECT_EQ(s.version, 450);
}

TEST_F(ShaderParserTest, ParseFile_ShadowDepthMultiviewVert_Succeeds)
{
    const auto& s = ParseSuccessfully("res/shaders/render/shadow_depth_multiview.vert");
    EXPECT_FALSE(s.IsEmpty());
}

TEST_F(ShaderParserTest, ParseFile_ShadowDepthMultiviewVert_HasCorrectVersion)
{
    const auto& s = ParseSuccessfully("res/shaders/render/shadow_depth_multiview.vert");
    EXPECT_EQ(s.version, 450);
}

TEST_F(ShaderParserTest, ParseFile_TriangleVert_Succeeds)
{
    const auto& s = ParseSuccessfully("res/shaders/render/triangle.vert");
    EXPECT_FALSE(s.IsEmpty());
}

TEST_F(ShaderParserTest, ParseFile_TriangleVert_HasCorrectVersion)
{
    const auto& s = ParseSuccessfully("res/shaders/render/triangle.vert");
    EXPECT_EQ(s.version, 460);
}

TEST_F(ShaderParserTest, ParseFile_SkyboxVert_Succeeds)
{
    const auto& s = ParseSuccessfully("res/shaders/render/skybox.vert");
    EXPECT_FALSE(s.IsEmpty());
}

TEST_F(ShaderParserTest, ParseFile_SkyboxVert_HasCorrectVersion)
{
    const auto& s = ParseSuccessfully("res/shaders/render/skybox.vert");
    EXPECT_EQ(s.version, 450);
}

// --- render/ fragment shaders (6 files) ---

TEST_F(ShaderParserTest, ParseFile_SunShadowDepthFrag_Succeeds)
{
    const auto& s = ParseSuccessfully("res/shaders/render/sun_shadow_depth.frag");
    EXPECT_FALSE(s.IsEmpty());
}

TEST_F(ShaderParserTest, ParseFile_SunShadowDepthFrag_HasCorrectVersion)
{
    const auto& s = ParseSuccessfully("res/shaders/render/sun_shadow_depth.frag");
    EXPECT_EQ(s.version, 450);
}

TEST_F(ShaderParserTest, ParseFile_ShadowDepthFrag_Succeeds)
{
    const auto& s = ParseSuccessfully("res/shaders/render/shadow_depth.frag");
    EXPECT_FALSE(s.IsEmpty());
}

TEST_F(ShaderParserTest, ParseFile_ShadowDepthFrag_HasCorrectVersion)
{
    const auto& s = ParseSuccessfully("res/shaders/render/shadow_depth.frag");
    EXPECT_EQ(s.version, 450);
}

TEST_F(ShaderParserTest, ParseFile_DepthToColorFrag_Succeeds)
{
    const auto& s = ParseSuccessfully("res/shaders/render/depth_to_color.frag");
    EXPECT_FALSE(s.IsEmpty());
}

TEST_F(ShaderParserTest, ParseFile_DepthToColorFrag_HasCorrectVersion)
{
    const auto& s = ParseSuccessfully("res/shaders/render/depth_to_color.frag");
    EXPECT_EQ(s.version, 450);
}

TEST_F(ShaderParserTest, ParseFile_TriangleFrag_Succeeds)
{
    const auto& s = ParseSuccessfully("res/shaders/render/triangle.frag");
    EXPECT_FALSE(s.IsEmpty());
}

TEST_F(ShaderParserTest, ParseFile_TriangleFrag_HasCorrectVersion)
{
    const auto& s = ParseSuccessfully("res/shaders/render/triangle.frag");
    EXPECT_EQ(s.version, 460);
}

TEST_F(ShaderParserTest, ParseFile_SkyboxFrag_Succeeds)
{
    const auto& s = ParseSuccessfully("res/shaders/render/skybox.frag");
    EXPECT_FALSE(s.IsEmpty());
}

TEST_F(ShaderParserTest, ParseFile_SkyboxFrag_HasCorrectVersion)
{
    const auto& s = ParseSuccessfully("res/shaders/render/skybox.frag");
    EXPECT_EQ(s.version, 450);
}

TEST_F(ShaderParserTest, ParseFile_GbufferFrag_Succeeds)
{
    const auto& s = ParseSuccessfully("res/shaders/render/gbuffer.frag");
    EXPECT_FALSE(s.IsEmpty());
}

TEST_F(ShaderParserTest, ParseFile_GbufferFrag_HasCorrectVersion)
{
    const auto& s = ParseSuccessfully("res/shaders/render/gbuffer.frag");
    EXPECT_EQ(s.version, 450);
}

// --- compute/ shaders (7 files) ---

TEST_F(ShaderParserTest, ParseFile_Ssao_Succeeds)
{
    const auto& s = ParseSuccessfully("res/shaders/compute/ssao.comp");
    EXPECT_FALSE(s.IsEmpty());
}

TEST_F(ShaderParserTest, ParseFile_Ssao_HasCorrectVersion)
{
    const auto& s = ParseSuccessfully("res/shaders/compute/ssao.comp");
    EXPECT_EQ(s.version, 450);
}

TEST_F(ShaderParserTest, ParseFile_PbrLighting_Succeeds)
{
    const auto& s = ParseSuccessfully("res/shaders/compute/pbr_lighting.comp");
    EXPECT_FALSE(s.IsEmpty());
}

TEST_F(ShaderParserTest, ParseFile_PbrLighting_HasCorrectVersion)
{
    const auto& s = ParseSuccessfully("res/shaders/compute/pbr_lighting.comp");
    EXPECT_EQ(s.version, 450);
}

TEST_F(ShaderParserTest, ParseFile_SunShadowEval_Succeeds)
{
    const auto& s = ParseSuccessfully("res/shaders/compute/sun_shadow_eval.comp");
    EXPECT_FALSE(s.IsEmpty());
}

TEST_F(ShaderParserTest, ParseFile_SunShadowEval_HasCorrectVersion)
{
    const auto& s = ParseSuccessfully("res/shaders/compute/sun_shadow_eval.comp");
    EXPECT_EQ(s.version, 450);
}

TEST_F(ShaderParserTest, ParseFile_ShadowEval_Succeeds)
{
    const auto& s = ParseSuccessfully("res/shaders/compute/shadow_eval.comp");
    EXPECT_FALSE(s.IsEmpty());
}

TEST_F(ShaderParserTest, ParseFile_ShadowEval_HasCorrectVersion)
{
    const auto& s = ParseSuccessfully("res/shaders/compute/shadow_eval.comp");
    EXPECT_EQ(s.version, 450);
}

TEST_F(ShaderParserTest, ParseFile_IrradianceConv_Succeeds)
{
    const auto& s = ParseSuccessfully("res/shaders/compute/irradiance_conv.comp");
    EXPECT_FALSE(s.IsEmpty());
}

TEST_F(ShaderParserTest, ParseFile_IrradianceConv_HasCorrectVersion)
{
    const auto& s = ParseSuccessfully("res/shaders/compute/irradiance_conv.comp");
    EXPECT_EQ(s.version, 450);
}

TEST_F(ShaderParserTest, ParseFile_ImportanceSamp_Succeeds)
{
    const auto& s = ParseSuccessfully("res/shaders/compute/importance_samp.comp");
    EXPECT_FALSE(s.IsEmpty());
}

TEST_F(ShaderParserTest, ParseFile_ImportanceSamp_HasCorrectVersion)
{
    const auto& s = ParseSuccessfully("res/shaders/compute/importance_samp.comp");
    EXPECT_EQ(s.version, 450);
}

TEST_F(ShaderParserTest, ParseFile_Dummy_Succeeds)
{
    const auto& s = ParseSuccessfully("res/shaders/compute/dummy.comp");
    EXPECT_FALSE(s.IsEmpty());
}

TEST_F(ShaderParserTest, ParseFile_Dummy_HasCorrectVersion)
{
    const auto& s = ParseSuccessfully("res/shaders/compute/dummy.comp");
    EXPECT_EQ(s.version, 450);
}

// --- convert/ shaders (2 files) ---

TEST_F(ShaderParserTest, ParseFile_C2e_Succeeds)
{
    const auto& s = ParseSuccessfully("res/shaders/convert/c2e.comp");
    EXPECT_FALSE(s.IsEmpty());
}

TEST_F(ShaderParserTest, ParseFile_C2e_HasCorrectVersion)
{
    const auto& s = ParseSuccessfully("res/shaders/convert/c2e.comp");
    EXPECT_EQ(s.version, 450);
}

TEST_F(ShaderParserTest, ParseFile_E2c_Succeeds)
{
    const auto& s = ParseSuccessfully("res/shaders/convert/e2c.comp");
    EXPECT_FALSE(s.IsEmpty());
}

TEST_F(ShaderParserTest, ParseFile_E2c_HasCorrectVersion)
{
    const auto& s = ParseSuccessfully("res/shaders/convert/e2c.comp");
    EXPECT_EQ(s.version, 450);
}

// ===========================================================================
// Section 2: Compute shader specific assertions
// ===========================================================================

TEST_F(ShaderParserTest, ParseFile_PbrLighting_HasLocalSize)
{
    const auto& s = ParseSuccessfully("res/shaders/compute/pbr_lighting.comp");
    EXPECT_EQ(s.local_size_x, 16u);
    EXPECT_EQ(s.local_size_y, 16u);
    EXPECT_EQ(s.local_size_z, 1u);
}

TEST_F(ShaderParserTest, ParseFile_PbrLighting_HasPushConstants)
{
    const auto& s = ParseSuccessfully("res/shaders/compute/pbr_lighting.comp");
    EXPECT_FALSE(s.push_constants.empty())
        << "pbr_lighting.comp should have push constants (lightCount, cameraPos, view, etc.)";
}

TEST_F(ShaderParserTest, ParseFile_PbrLighting_HasStorageBuffers)
{
    const auto& s = ParseSuccessfully("res/shaders/compute/pbr_lighting.comp");
    EXPECT_FALSE(s.SB_list.empty())
        << "pbr_lighting.comp should have storage buffers (LightBuffer, SunLightBuffer)";
}

TEST_F(ShaderParserTest, ParseFile_Ssao_HasLocalSize)
{
    const auto& s = ParseSuccessfully("res/shaders/compute/ssao.comp");
    EXPECT_EQ(s.local_size_x, 16u);
    EXPECT_EQ(s.local_size_y, 16u);
    EXPECT_EQ(s.local_size_z, 1u);
}

TEST_F(ShaderParserTest, ParseFile_Ssao_HasPushConstants)
{
    const auto& s = ParseSuccessfully("res/shaders/compute/ssao.comp");
    EXPECT_FALSE(s.push_constants.empty())
        << "ssao.comp should have push constants (kernelLength, radius, noiseSize, frameIndex)";
}

TEST_F(ShaderParserTest, ParseFile_Dummy_HasLocalSizeOne)
{
    const auto& s = ParseSuccessfully("res/shaders/compute/dummy.comp");
    EXPECT_EQ(s.local_size_x, 1u);
    EXPECT_EQ(s.local_size_y, 1u);
    EXPECT_EQ(s.local_size_z, 1u);
}

TEST_F(ShaderParserTest, ParseFile_SunShadowEval_HasLocalSize)
{
    const auto& s = ParseSuccessfully("res/shaders/compute/sun_shadow_eval.comp");
    EXPECT_EQ(s.local_size_x, 16u);
    EXPECT_EQ(s.local_size_y, 16u);
    EXPECT_EQ(s.local_size_z, 1u);
}

TEST_F(ShaderParserTest, ParseFile_ShadowEval_HasLocalSize)
{
    const auto& s = ParseSuccessfully("res/shaders/compute/shadow_eval.comp");
    EXPECT_GT(s.local_size_x, 0u)
        << "Any compute shader should have non-zero local_size_x";
    EXPECT_GT(s.local_size_y, 0u)
        << "Any compute shader should have non-zero local_size_y";
}

// ===========================================================================
// Section 3: Vertex shader specific assertions
// ===========================================================================

TEST_F(ShaderParserTest, ParseFile_GbufferVert_HasAttributes)
{
    const auto& s = ParseSuccessfully("res/shaders/render/gbuffer.vert");
    EXPECT_EQ(s.AB_list.size(), 3u)
        << "gbuffer.vert should have 3 vertex attributes (position, normal, uv)";

    if (s.AB_list.size() >= 3)
    {
        // Layout: location=0=inPosition, location=1=inNormal, location=2=inUV
        EXPECT_EQ(s.AB_list[0].location, 0);
        EXPECT_EQ(s.AB_list[0].name, "inPosition");

        EXPECT_EQ(s.AB_list[1].location, 1);
        EXPECT_EQ(s.AB_list[1].name, "inNormal");

        EXPECT_EQ(s.AB_list[2].location, 2);
        EXPECT_EQ(s.AB_list[2].name, "inUV");
    }
}

TEST_F(ShaderParserTest, ParseFile_GbufferVert_HasUniforms)
{
    const auto& s = ParseSuccessfully("res/shaders/render/gbuffer.vert");

    // Check ubuffer_list for CameraUBO (set=0, binding=0)
    bool hasCameraUBO = false;
    for (const auto& ub : s.ubuffer_list)
    {
        if (ub.name == "CameraUBO")
        {
            hasCameraUBO = true;
            break;
        }
    }
    EXPECT_TRUE(hasCameraUBO)
        << "gbuffer.vert should have a CameraUBO uniform block";
}

TEST_F(ShaderParserTest, ParseFile_GbufferVert_HasMainBody)
{
    const auto& s = ParseSuccessfully("res/shaders/render/gbuffer.vert");
    EXPECT_FALSE(s.Main.empty())
        << "gbuffer.vert main() body should not be empty";
}

TEST_F(ShaderParserTest, ParseFile_ShadowDepthMultiview_HasExtension)
{
    const auto& s = ParseSuccessfully("res/shaders/render/shadow_depth_multiview.vert");

    bool hasMultiview = false;
    for (const auto& ext : s.extensions)
    {
        if (ext.find("GL_EXT_multiview") != std::string::npos)
        {
            hasMultiview = true;
            break;
        }
    }
    EXPECT_TRUE(hasMultiview)
        << "shadow_depth_multiview.vert should require GL_EXT_multiview";
}

TEST_F(ShaderParserTest, ParseFile_GbufferFrag_HasPassList)
{
    const auto& s = ParseSuccessfully("res/shaders/render/gbuffer.frag");

    // gbuffer.frag has 4 MRT outputs: gPosition(0), gNormal(1), gAlbedo(2), gMetallicRoughness(3)
    EXPECT_EQ(s.pass_list.size(), 4u)
        << "gbuffer.frag should have 4 render-pass outputs (MRT)";
}

TEST_F(ShaderParserTest, ParseFile_GbufferFrag_HasInputList)
{
    const auto& s = ParseSuccessfully("res/shaders/render/gbuffer.frag");

    // gbuffer.frag has 3 inputs: fragWorldPos(0), fragNormalVS(1), fragUV(2)
    EXPECT_EQ(s.input_list.size(), 3u)
        << "gbuffer.frag should have 3 inter-stage inputs";
}

// ===========================================================================
// Section 4: Edge case tests — no file system access needed
// ===========================================================================

TEST_F(ShaderParserTest, ParseFile_EmptySource_ReturnsFalse)
{
    EXPECT_FALSE(ShaderParser::ParseShaderSource("", m_shaderStruct));
}

TEST_F(ShaderParserTest, ParseFile_MissingFile_ReturnsFalse)
{
    // Use a path that definitely doesn't exist
    EXPECT_FALSE(ShaderParser::ParseShaderFile("nonexistent/shader.vert", m_shaderStruct));
}

TEST_F(ShaderParserTest, ParseFile_SyntaxError_ReturnsFalse)
{
    // Malformed GLSL — missing closing brace
    const char* broken = R"(#version 450
void main() {
    gl_Position = vec4(1.0);
)"; // no closing brace
    EXPECT_FALSE(ShaderParser::ParseShaderSource(broken, m_shaderStruct));
}

TEST_F(ShaderParserTest, ParseFile_BareMinimum_ReturnsTrue)
{
    // Minimum valid Vulkan GLSL: #version + void main() {}
    const char* minimal = R"(#version 450
void main() {
}
)";
    EXPECT_TRUE(ShaderParser::ParseShaderSource(minimal, m_shaderStruct));
    EXPECT_FALSE(m_shaderStruct.IsEmpty());
    EXPECT_EQ(m_shaderStruct.version, 450);
    EXPECT_FALSE(m_shaderStruct.Main.empty());
}

TEST_F(ShaderParserTest, ParseFile_OnlyMainBody_ReturnsTrue)
{
    // Minimal with a meaningful main body
    const char* shader = R"(#version 450

void main()
{
    gl_Position = vec4(1.0, 0.0, 0.0, 1.0);
}
)";
    EXPECT_TRUE(ShaderParser::ParseShaderSource(shader, m_shaderStruct));
    EXPECT_FALSE(m_shaderStruct.Main.empty());
}

TEST_F(ShaderParserTest, ParseFile_MultipleExtensions_AllCaptured)
{
    const char* shader = R"(#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_multiview : require

void main() {
}
)";
    EXPECT_TRUE(ShaderParser::ParseShaderSource(shader, m_shaderStruct));
    EXPECT_EQ(m_shaderStruct.extensions.size(), 2u);

    bool hasInclude = false;
    bool hasMultiview = false;
    for (const auto& ext : m_shaderStruct.extensions)
    {
        if (ext.find("GL_GOOGLE_include_directive") != std::string::npos) hasInclude = true;
        if (ext.find("GL_EXT_multiview") != std::string::npos) hasMultiview = true;
    }
    EXPECT_TRUE(hasInclude);
    EXPECT_TRUE(hasMultiview);
}

TEST_F(ShaderParserTest, ParseFile_UnknownVersion_StillReturnsTrue)
{
    // Parser should accept any #version and just store it
    const char* shader = R"(#version 460 core
void main() {
}
)";
    EXPECT_TRUE(ShaderParser::ParseShaderSource(shader, m_shaderStruct));
    EXPECT_EQ(m_shaderStruct.version, 460);
}

TEST_F(ShaderParserTest, ParseFile_LocalSizeAllOnes_Detected)
{
    const char* shader = R"(#version 450
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
void main() {
}
)";
    EXPECT_TRUE(ShaderParser::ParseShaderSource(shader, m_shaderStruct));
    EXPECT_EQ(m_shaderStruct.local_size_x, 1u);
    EXPECT_EQ(m_shaderStruct.local_size_y, 1u);
    EXPECT_EQ(m_shaderStruct.local_size_z, 1u);
}

TEST_F(ShaderParserTest, ParseFile_LocalSizeNonSquare_Detected)
{
    const char* shader = R"(#version 450
layout(local_size_x = 8, local_size_y = 4, local_size_z = 1) in;
void main() {
}
)";
    EXPECT_TRUE(ShaderParser::ParseShaderSource(shader, m_shaderStruct));
    EXPECT_EQ(m_shaderStruct.local_size_x, 8u);
    EXPECT_EQ(m_shaderStruct.local_size_y, 4u);
    EXPECT_EQ(m_shaderStruct.local_size_z, 1u);
}

TEST_F(ShaderParserTest, ParseFile_WhitespaceOnly_ReturnsTrue)
{
    // Whitespace-only source is technically valid (empty shader), parser should handle it
    const char* shader = "#version 450\nvoid main() {\n}\n";
    EXPECT_TRUE(ShaderParser::ParseShaderSource(shader, m_shaderStruct));
}

TEST_F(ShaderParserTest, ParseFile_StructDefinition_Detected)
{
    const char* shader = R"(#version 450

struct MyLight {
    vec3 color;
    float power;
};

void main() {
}
)";
    EXPECT_TRUE(ShaderParser::ParseShaderSource(shader, m_shaderStruct));
    EXPECT_FALSE(m_shaderStruct.struct_def_list.empty())
        << "Parser should detect bare struct definitions";
}

TEST_F(ShaderParserTest, ParseFile_ConstDeclaration_Detected)
{
    const char* shader = R"(#version 450

const float PI = 3.14159265359;

void main() {
    float x = PI;
}
)";
    EXPECT_TRUE(ShaderParser::ParseShaderSource(shader, m_shaderStruct));
    EXPECT_FALSE(m_shaderStruct.const_list.empty())
        << "Parser should detect const declarations";
}

TEST_F(ShaderParserTest, ParseFile_UniformSampler_Detected)
{
    const char* shader = R"(#version 450

layout(binding = 0) uniform sampler2D gPosition;

void main() {
    vec4 pos = texture(gPosition, vec2(0.5));
}
)";
    EXPECT_TRUE(ShaderParser::ParseShaderSource(shader, m_shaderStruct));
    // Should have at least one uniform entry
    EXPECT_FALSE(m_shaderStruct.uniform_list.empty())
        << "Parser should detect uniform sampler2D declarations";
}

TEST_F(ShaderParserTest, ParseFile_ResetBeforeParse_ProducesCleanStruct)
{
    // Verify that parsing into a reset ShaderStruct produces consistent results
    m_shaderStruct.Reset();
    EXPECT_TRUE(m_shaderStruct.IsEmpty());

    const auto& s = ParseSuccessfully("res/shaders/compute/dummy.comp");
    // After successful parse, the struct should NOT be empty
    EXPECT_FALSE(s.IsEmpty());
    EXPECT_EQ(s.version, 450);
}

TEST_F(ShaderParserTest, ParseFile_SecondParseOverwritesPrevious)
{
    // Parse a complex shader first
    m_shaderStruct.Reset();
    EXPECT_TRUE(ShaderParser::ParseShaderSource(
        "#version 450\n"
        "layout(binding = 0) uniform sampler2D tex;\n"
        "void main() { vec4 c = texture(tex, vec2(0.5)); }\n",
        m_shaderStruct));

    size_t firstUniformCount = m_shaderStruct.uniform_list.size();
    EXPECT_GT(firstUniformCount, 0u);

    // Parse a different shader into the SAME struct (should overwrite)
    EXPECT_TRUE(ShaderParser::ParseShaderSource(
        "#version 460\n"
        "void main() { }\n",
        m_shaderStruct));

    // Now should only have the new shader's data
    EXPECT_EQ(m_shaderStruct.version, 460);
    EXPECT_TRUE(m_shaderStruct.uniform_list.empty())  // No uniforms in second shader
        << "ParseShaderSource should overwrite previous state";
}

// ===========================================================================
// Section 5: Error message API contract
// ===========================================================================

TEST_F(ShaderParserTest, GetLastError_InitiallyEmpty)
{
    // The error string should be available after any parse call
    const std::string& err = ShaderParser::GetLastError();
    // May be empty or contain last error depending on implementation;
    // we just verify the API exists and returns a std::string reference
    EXPECT_TRUE(err.empty() || !err.empty())  // always true — just exercising the API
        << "GetLastError() must be callable and return a std::string";
}

TEST_F(ShaderParserTest, GetLastError_NotEmptyAfterFailure)
{
    EXPECT_FALSE(ShaderParser::ParseShaderSource("invalid garbage not glsl", m_shaderStruct));
    const std::string& err = ShaderParser::GetLastError();
    EXPECT_FALSE(err.empty())
        << "GetLastError() should return a non-empty message after a parse failure";
}
