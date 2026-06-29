/**
 * @file Transform.cpp
 * @brief Implementation of Transform3D spatial query methods.
 *
 * Provides the direction vector computation from Euler angle rotation.
 */

#include "scene/Transform.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

namespace neurus {

glm::vec3 Transform3D::GetDirection() const
{
	// Build rotation matrix matching GetModelMatrix() order: Rx * Ry * Rz
	const glm::vec3 rad = glm::radians(m_rotation);
	glm::mat4 rot{1.0f};
	rot = glm::rotate(rot, rad.x, glm::vec3(1.0f, 0.0f, 0.0f)); // Pitch (X)
	rot = glm::rotate(rot, rad.y, glm::vec3(0.0f, 1.0f, 0.0f)); // Yaw   (Y)
	rot = glm::rotate(rot, rad.z, glm::vec3(0.0f, 0.0f, 1.0f)); // Roll  (Z)

	// Local forward = (0, 0, -1), rotate by the 3x3 rotation part
	const glm::vec3 forward = glm::vec3(rot * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
	return glm::normalize(forward);
}

} // namespace neurus
