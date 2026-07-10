#pragma once

#include "buffers/IndexBuffer.h"
#include "buffers/VertexBuffer.h"

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

} // namespace neurus
