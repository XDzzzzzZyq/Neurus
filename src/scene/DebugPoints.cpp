/**
 * @file DebugPoints.cpp
 * @brief Implementation of DebugPoints point management.
 */

#include "DebugPoints.h"

namespace neurus
{

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

DebugPoints::DebugPoints()
	: ObjectID()
{
	o_type = GOType::GO_DP;
	o_name = "DebugPoints";
}

// ---------------------------------------------------------------------------
// Point management
// ---------------------------------------------------------------------------

void DebugPoints::PushDebugPoint(const glm::vec3& point)
{
	m_points.push_back(point);
}

void DebugPoints::PushDebugPoints(const std::vector<glm::vec3>& points)
{
	m_points.insert(
		m_points.end(),
		points.begin(),
		points.end()
	);
}

void DebugPoints::ClearPoints()
{
	m_points.clear();
}

} // namespace neurus
