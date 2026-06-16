/**
 * @file Camera.cpp
 * @brief Implementation of Camera scene object.
 */

#include "scene/Camera.h"

namespace neurus {

// -----------------------------------------------------------------------
// Constructors
// -----------------------------------------------------------------------

Camera::Camera(float w, float h, float per, float n, float f)
	: cam_w(w)
	, cam_h(h)
	, cam_pers(per)
	, cam_near(n)
	, cam_far(f)
{
	o_type = ObjectID::GOType::GO_CAM;
}

Camera::Camera()
{
	o_type = ObjectID::GOType::GO_CAM;
}

// -----------------------------------------------------------------------
// View / Projection matrices
// -----------------------------------------------------------------------

glm::mat4 Camera::GetViewMatrix() const
{
	return glm::lookAt(GetPosition(), cam_tar, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::GetProjectionMatrix()
{
	if (m_frustumDirty)
	{
		const float aspect = cam_w / cam_h;
		m_cachedProjection = glm::perspective(
			glm::radians(cam_pers), aspect, cam_near, cam_far);
		m_frustumDirty = false;
	}
	return m_cachedProjection;
}

// -----------------------------------------------------------------------
// Parameter setters
// -----------------------------------------------------------------------

void Camera::ChangeCamRatio(float w, float h)
{
	cam_w = w;
	cam_h = h;
	m_frustumDirty = true;
}

void Camera::ChangeCamPersp(float persp)
{
	cam_pers = persp;
	m_frustumDirty = true;
}

void Camera::SetCamPos(const glm::vec3& pos)
{
	SetPosition(pos);
}

void Camera::SetTarPos(const glm::vec3& pos)
{
	cam_tar = pos;
}

// -----------------------------------------------------------------------
// Serialization
// -----------------------------------------------------------------------

void Camera::GenFloatData()
{
	cam_floatData.resize(8);

	const glm::vec3& pos = GetPosition();
	const glm::vec3& rot = GetRotation();

	cam_floatData[0] = pos.x;
	cam_floatData[1] = pos.y;
	cam_floatData[2] = pos.z;

	cam_floatData[3] = rot.x;
	cam_floatData[4] = rot.y;
	cam_floatData[5] = rot.z;

	cam_floatData[6] = cam_w / cam_h;
	cam_floatData[7] = glm::radians(cam_pers);
}

// -----------------------------------------------------------------------
// Transform access
// -----------------------------------------------------------------------

void* Camera::GetTransform()
{
	return static_cast<Transform*>(this);
}

} // namespace neurus
