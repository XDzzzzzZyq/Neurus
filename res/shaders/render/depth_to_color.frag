#version 450
// ---------------------------------------------------------------------------
// Depth to color pass - writes linear depth to color output for readback.
// Used by test_shadow_cubemap_depth_verify.cpp when depth-aspect
// vkCmdCopyImageToBuffer is not functional on this GPU.
// ---------------------------------------------------------------------------

layout(location = 0) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform LightData
{
	mat4 faceViewProj[6];
	vec3 lightWorldPos;
	float farPlane;
};

void main()
{
	float dist = length(fragWorldPos - lightWorldPos);
	float depth = dist / farPlane;
	gl_FragDepth = depth;
	outColor = vec4(depth, depth, depth, 1.0);
}
