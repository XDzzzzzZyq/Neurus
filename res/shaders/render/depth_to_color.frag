#version 450
// ---------------------------------------------------------------------------
// Depth to color pass - writes linear depth to color output for readback.
// Used by test_shadow_cubemap_depth_verify.cpp when depth-aspect
// vkCmdCopyImageToBuffer is not functional on this GPU.
//
// lightWorldPos and farPlane come from push constants (per-light).
// ---------------------------------------------------------------------------

layout(location = 0) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants
{
	layout(offset = 0)  vec3 lightWorldPos;
	layout(offset = 12) float farPlane;
};

void main()
{
	float dist = length(fragWorldPos - lightWorldPos);
	float depth = dist / farPlane;
	gl_FragDepth = depth;
	outColor = vec4(depth, depth, depth, 1.0);
}
