/**
 * @file DebugLine.cpp
 * @brief Implementation of DebugLine vertex management.
 */

#include "DebugLine.h"

namespace neurus
{

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

DebugLine::DebugLine()
	: ObjectID()
{
	o_type = GOType::GO_DL;
	o_name = "DebugLine";
}

// ---------------------------------------------------------------------------
// Vertex management
// ---------------------------------------------------------------------------

void DebugLine::PushDebugLine(const glm::vec3& start, const glm::vec3& end)
{
	m_vertices.push_back(start);
	m_vertices.push_back(end);
}

void DebugLine::PushDebugLines(const std::vector<glm::vec3>& vertices)
{
	m_vertices.insert(
		m_vertices.end(),
		vertices.begin(),
		vertices.end()
	);
}

void DebugLine::ClearVertices()
{
	m_vertices.clear();
}

} // namespace neurus
