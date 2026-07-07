#version 450
// ---------------------------------------------------------------------------
// Fullscreen pass-through vertex shader.
// Renders a single triangle covering the entire clip-space (-1..+1 in xy).
// No vertex attributes needed — gl_VertexIndex selects clip-space coordinates.
// Outputs world-space position via inverse projection for fragment shaders
// that need it (e.g. depth-to-color readback).
// ---------------------------------------------------------------------------

layout(push_constant) uniform PushConstants
{
	mat4 invProjView;  // inverse(proj * view) to reconstruct worldPos from clipPos
} pc;

layout(location = 0) out vec3 fragWorldPos;

void main()
{
	// Full-screen triangle from gl_VertexIndex (3 vertices cover NDC [-1,1]^2 fully)
	// See: https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/
	float x = float((gl_VertexIndex & 1) << 2) - 1.0;  // -1, 3, -1
	float y = float((gl_VertexIndex & 2) << 1) - 1.0;  // -1, -1, 3
	gl_Position = vec4(x, y, 0.0, 1.0);

	// Reconstruct world-space position of the far-plane corner
	vec4 clipPos = vec4(x, y, 1.0, 1.0);
	vec4 worldPos = pc.invProjView * clipPos;
	fragWorldPos = worldPos.xyz / worldPos.w;
}
