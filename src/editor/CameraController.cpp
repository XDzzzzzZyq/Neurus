/**
 * @file CameraController.cpp
 * @brief Implementation of camera orbit, pan, and zoom via mouse input.
 */

#include "editor/CameraController.h"
#include "scene/Camera.h"
#include "scene/Scene.h"

#include <algorithm>
#include <cmath>

namespace neurus {

// ---------------------------------------------------------------------------
// Update - main entry point
// ---------------------------------------------------------------------------

void CameraController::Update(Camera& camera, Scene& scene, const InputState& input)
{
	const float speed = GetSpeedMultiplier(input);

	// Scroll wheel → zoom (always active, independent of mouse buttons)
	if (std::abs(input.scrollDelta) > 0.0001f)
	{
		Zoom(camera, input, speed);
		scene.UpdateSceneStatus(Scene::CameraChanged, true);
	}

	// MMB or Shift+RMB → pan
	if (input.middleMouseHeld || (input.rightMouseHeld && input.shiftHeld))
	{
		if (std::abs(input.mouseDeltaX) > 0.0001f || std::abs(input.mouseDeltaY) > 0.0001f)
		{
			Pan(camera, input, speed);
			scene.UpdateSceneStatus(Scene::CameraChanged, true);
		}
		return; // Don't also orbit when Shift+RMB
	}

	// RMB alone → orbit
	if (input.rightMouseHeld)
	{
		if (std::abs(input.mouseDeltaX) > 0.0001f || std::abs(input.mouseDeltaY) > 0.0001f)
		{
			Orbit(camera, input, speed);
			scene.UpdateSceneStatus(Scene::CameraChanged, true);
		}
	}
}

// ---------------------------------------------------------------------------
// Speed
// ---------------------------------------------------------------------------

float CameraController::GetSpeedMultiplier(const InputState& input) const
{
	if (input.shiftHeld)
	{
		return kFastMultiplier;
	}
	if (input.ctrlHeld)
	{
		return kSlowMultiplier;
	}
	return 1.0f;
}

// ---------------------------------------------------------------------------
// Orbit - rotate camera around look-at target
// ---------------------------------------------------------------------------

void CameraController::Orbit(Camera& camera, const InputState& input, float speed)
{
	const glm::vec3 pos = camera.GetPosition();
	const glm::vec3& target = camera.cam_tar;

	glm::vec3 dir = pos - target;
	const float radius = glm::length(dir);

	// Degenerate: camera at target — cannot orbit
	if (radius < 1e-6f)
	{
		return;
	}

	dir /= radius; // normalize

	// Compute spherical coordinates (Y-up)
	const float elevation = std::asin(dir.y);                        // [-PI/2, PI/2]
	const float azimuth = std::atan2(dir.x, dir.z);                  // [-PI, PI]

	// Apply mouse delta with reversed sign for natural drag feel
	const float deltaAzimuth = -input.mouseDeltaX * kOrbitSensitivity * speed;
	const float deltaElevation = -input.mouseDeltaY * kOrbitSensitivity * speed;

	// Clamp elevation to avoid flipping at ±90°
	constexpr float kMaxElevation = glm::radians(89.0f);
	const float newElevation = std::clamp(elevation + deltaElevation, -kMaxElevation, kMaxElevation);
	const float newAzimuth = azimuth + deltaAzimuth;

	// Convert back to Cartesian
	const float cosPhi = std::cos(newElevation);
	const glm::vec3 newDir(
		cosPhi * std::sin(newAzimuth),
		std::sin(newElevation),
		cosPhi * std::cos(newAzimuth)
	);

	camera.SetCamPos(target + newDir * radius);
	// Target stays fixed — camera continues looking at same point
}

// ---------------------------------------------------------------------------
// Pan - translate camera and target parallel to view plane
// ---------------------------------------------------------------------------

void CameraController::Pan(Camera& camera, const InputState& input, float speed)
{
	const glm::vec3 pos = camera.GetPosition();
	const glm::vec3& target = camera.cam_tar;

	// Forward: target → camera (viewing direction is camera looking at target)
	glm::vec3 forward = pos - target;
	if (glm::length(forward) < 1e-6f)
	{
		return;
	}
	forward = glm::normalize(forward);

	// Right: cross(forward, world-up)
	constexpr glm::vec3 kWorldUp(0.0f, 1.0f, 0.0f);
	glm::vec3 right = glm::cross(forward, kWorldUp);

	// Handle degenerate case: camera looking straight up/down
	if (glm::length(right) < 1e-6f)
	{
		// Looking straight up → use world X as right
		right = glm::vec3(1.0f, 0.0f, 0.0f);
	}
	right = glm::normalize(right);

	// Up: cross(right, forward) — camera-local up (not world up)
	const glm::vec3 up = glm::cross(right, forward);

	// Compute translation: horizontal mouse = right, vertical mouse = up
	const float panSensitivity = kPanSensitivity * speed;
	const glm::vec3 delta = -input.mouseDeltaX * panSensitivity * right
	                       + input.mouseDeltaY * panSensitivity * up;

	camera.SetCamPos(pos + delta);
	camera.SetTarPos(target + delta);
}

// ---------------------------------------------------------------------------
// Zoom - dolly camera toward or away from target
// ---------------------------------------------------------------------------

void CameraController::Zoom(Camera& camera, const InputState& input, float speed)
{
	const glm::vec3 pos = camera.GetPosition();
	const glm::vec3& target = camera.cam_tar;

	const glm::vec3 dir = pos - target;
	const float radius = glm::length(dir);

	// Degenerate: camera at target — cannot zoom
	if (radius < 1e-6f)
	{
		return;
	}

	// Zoom factor: scroll up → closer, scroll down → farther
	const float factor = std::pow(0.8f, input.scrollDelta * kZoomSensitivity * speed);
	const float newRadius = radius * factor;

	// Clamp to prevent going through target or flying to infinity
	constexpr float kMinRadius = 0.01f;
	constexpr float kMaxRadius = 1000.0f;
	const float clampedRadius = std::clamp(newRadius, kMinRadius, kMaxRadius);

	const glm::vec3 newDir = glm::normalize(dir) * clampedRadius;
	camera.SetCamPos(target + newDir);
}

} // namespace neurus
