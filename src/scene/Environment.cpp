/**
 * @file Environment.cpp
 * @brief Implementation of Environment - IBL environment map source object.
 */

#include "scene/Environment.h"

namespace neurus
{

// -----------------------------------------------------------------------
// Construction
// -----------------------------------------------------------------------

Environment::Environment()
{
	o_type = ObjectID::GOType::GO_ENVIR;
	o_name = "Environment";
}

// -----------------------------------------------------------------------
// File path
// -----------------------------------------------------------------------

void Environment::SetEquirectPath(const std::string& path)
{
	m_equirectPath = path;
	m_dirty = true;
}

const std::string& Environment::GetEquirectPath() const
{
	return m_equirectPath;
}

// -----------------------------------------------------------------------
// Intensity
// -----------------------------------------------------------------------

void Environment::SetIntensity(float i)
{
	m_intensity = i;
}

float Environment::GetIntensity() const
{
	return m_intensity;
}

// -----------------------------------------------------------------------
// Rotation
// -----------------------------------------------------------------------

void Environment::SetRotation(float r)
{
	m_rotation = r;
}

float Environment::GetRotation() const
{
	return m_rotation;
}

// -----------------------------------------------------------------------
// Dirty tracking
// -----------------------------------------------------------------------

bool Environment::IsDirty() const
{
	return m_dirty;
}

void Environment::ClearDirty()
{
	m_dirty = false;
}

} // namespace neurus
