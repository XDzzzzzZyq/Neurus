#version 450
// ---------------------------------------------------------------------------
// Shadow Depth Fragment Shader — Point Light Cubemap Depth Pass
//
// Computes linear Euclidean distance from the point light to the fragment,
// normalised by far_plane, and writes it to gl_FragDepth.
//
// The fragment shader runs solely to set gl_FragDepth; no colour attachments
// are written (depth-only dynamic rendering pass).
// ---------------------------------------------------------------------------

layout(location = 0) in vec3 fragWorldPos;

// --- Light data UBO (set = 0, binding = 0) ---
//     Re-declared here with only the members needed (same layout as vertex shader
//     to guarantee std140 compatibility, even though we only read lightWorldPos + farPlane).
layout(set = 0, binding = 0) uniform LightData
{
	mat4 faceViewProj[6];   // offset 0,   384 bytes
	vec3 lightWorldPos;     // offset 384, 12 bytes
	float farPlane;         // offset 396,  4 bytes
};

void main()
{
	float dist = length(fragWorldPos - lightWorldPos);
	gl_FragDepth = dist / farPlane;
}
