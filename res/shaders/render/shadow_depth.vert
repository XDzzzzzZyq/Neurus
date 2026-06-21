#version 450
// ---------------------------------------------------------------------------
// Shadow Depth Vertex Shader — Point Light Cubemap Depth Pass
//
// Transforms world-space vertices by the specified face's light view-projection
// matrix and passes world position to the fragment shader for linear depth
// calculation.
//
// Used with 6 separate dynamic rendering passes (one per cubemap face).
// ---------------------------------------------------------------------------

// --- Vertex input (matches MeshData GPU layout: pos(3), normal(3), uv(2)) ---
layout(location = 0) in vec3 inPosition;

// --- Inter-stage output ---
layout(location = 0) out vec3 fragWorldPos;

// --- Light data UBO (set = 0, binding = 0) ---
layout(set = 0, binding = 0) uniform LightData
{
	mat4 faceViewProj[6];   // 6 * 64 = 384 bytes
	vec3 lightWorldPos;     // world-space light position
	float farPlane;         // perspective far plane distance
};

// --- Per-draw push constants ---
layout(push_constant) uniform PushConstants
{
	mat4 model;        // world transform (identity for static geometry)
	int faceIndex;     // cubemap face 0-5 (+X, -X, +Y, -Y, +Z, -Z)
};

void main()
{
	vec4 worldPos = model * vec4(inPosition, 1.0);
	fragWorldPos = worldPos.xyz;
	gl_Position = faceViewProj[faceIndex] * worldPos;
}
