#pragma once

#include "../buffers/IndexBuffer.h"
#include "../buffers/VertexBuffer.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>

namespace neurus {

/**
 * @brief GPU-side mesh resources owned by the Renderer layer.
 *
 * Holds the device-local VertexBuffer and IndexBuffer for a mesh,
 * together with vertex and index counts.  Created lazily by
 * RenderCache::GetMeshGPU() and destroyed via RemoveMeshGPU() or
 * RenderCache::Clean().
 *
 * Non-copyable (GPU resource handles are move-only).  Has explicit
 * default-constructed "empty" state where both buffers are null and
 * counts are zero.
 */
struct MeshGPU
{
	std::unique_ptr<VertexBuffer> vertexBuffer;
	std::unique_ptr<IndexBuffer> indexBuffer;
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
};

/**
 * @brief Per-mesh push-constant block sent to the vertex shader.
 *
 * Packed as two mat4s (128 bytes total) to satisfy Vulkan's
 * 16-byte alignment requirement for push constants.
 */
struct alignas(16) MeshPushConstants
{
	glm::mat4 model;           ///< Local-to-world transform (offset 0)
	glm::mat4 normalMatrix;    ///< 3x3 in upper-left of mat4 (offset 64)
};

} // namespace neurus
