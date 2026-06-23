#version 450
#extension GL_EXT_multiview : require
// ---------------------------------------------------------------------------
// Shadow Depth Vertex Shader (Multiview) — Point Light Cubemap Depth Pass
//
// Uses gl_ViewIndex from VK_KHR_multiview to select the correct cubemap face
// view-projection matrix. All 6 faces are rendered in a single dynamic
// rendering pass via viewMask = 0x3F.
//
// Unlike the non-multiview shadow_depth.vert, this shader does NOT require
// a faceIndex push constant — the hardware provides the correct face index
// through gl_ViewIndex.
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

// --- Per-draw push constants (model matrix only, no faceIndex) ---
layout(push_constant) uniform PushConstants
{
	mat4 model;        // world transform (identity for static geometry)
};

void main()
{
	vec4 worldPos = model * vec4(inPosition, 1.0);
	fragWorldPos = worldPos.xyz;
	gl_Position = faceViewProj[gl_ViewIndex] * worldPos;
}
