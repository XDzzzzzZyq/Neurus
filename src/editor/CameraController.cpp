/**
 * @file CameraController.cpp
 * @brief Implementation of MMB-based camera navigation: orbit, dolly, pan, zoom.
 *
 * MMB Convention:
 *   - MMB alone     = Orbit (rotate around target)
 *   - Ctrl + MMB    = Dolly (move camera forward/back along view direction)
 *   - Shift + MMB   = Pan (translate camera parallel to view plane)
 *   - Scroll        = Zoom (move camera toward/away from target)
 */

#include "editor/CameraController.h"
#include "scene/Camera.h"

#include <algorithm>
#include <cmath>

namespace neurus {

// ---------------------------------------------------------------------------
// Speed
// ---------------------------------------------------------------------------

float CameraController::GetSpeedMultiplier(const InputState& input) const
{
	if (input.ctrlHeld) return kSlowMultiplier;
	return 1.0f;
}

// ---------------------------------------------------------------------------
// Event notification
// ---------------------------------------------------------------------------

void CameraController::NotifyCameraChanged(const Camera& camera)
{
	// Stub - will enqueue CameraTransformChanged event in the future
	(void)camera;
}

// ---------------------------------------------------------------------------
// Update - main entry point called each frame
// ---------------------------------------------------------------------------

void CameraController::Update(Camera& camera, const InputState& input)
{
	const float speed = GetSpeedMultiplier(input);

	// Scroll wheel → Zoom (always active, no modifier required)
	if (std::abs(input.scrollDelta) > 0.001f)
	{
		Zoom(camera, input, speed);
		NotifyCameraChanged(camera);
	}

	// MMB held → dispatch based on modifier keys
	if (input.middleMouseHeld)
	{
		if (input.ctrlHeld)
		{
			Dolly(camera, input, speed); // Ctrl + MMB = Dolly (Ctrl wins over Shift)
		}
		else if (input.shiftHeld)
		{
			Pan(camera, input, speed);   // Shift + MMB = Pan
		}
		else
		{
			Orbit(camera, input, speed); // MMB alone = Orbit
		}

		NotifyCameraChanged(camera);
	}
}

// ---------------------------------------------------------------------------
// Orbit - rotate camera around look-at target (MMB drag)
// ---------------------------------------------------------------------------

void CameraController::Orbit(Camera& camera, const InputState& input, float speed)
{
	const glm::vec3 pos = camera.GetPosition();
	const glm::vec3& target = camera.cam_tar;

	glm::vec3 dir = pos - target;
	const float radius = glm::length(dir);
	if (radius < 1e-6f) return;

	dir /= radius; // normalize

	// Compute spherical coordinates (Y-up)
	const float elevation = std::asin(dir.y);                     // [-PI/2, PI/2]
	const float azimuth   = std::atan2(dir.x, dir.z);             // [-PI, PI]

	// Apply mouse delta (inverted for natural drag feel)
	const float deltaAzimuth   = -input.mouseDeltaX * kOrbitSensitivity * speed;
	const float deltaElevation = -input.mouseDeltaY * kOrbitSensitivity * speed;

	// Clamp elevation to avoid flipping at +/-89 degrees
	constexpr float kMaxElevation = glm::radians(89.0f);
	const float newElevation = std::clamp(elevation + deltaElevation, -kMaxElevation, kMaxElevation);
	const float newAzimuth   = azimuth + deltaAzimuth;

	// Convert back to Cartesian
	const float cosPhi = std::cos(newElevation);
	const glm::vec3 newDir(
		cosPhi * std::sin(newAzimuth),
		std::sin(newElevation),
		cosPhi * std::cos(newAzimuth)
	);

	camera.SetCamPos(target + newDir * radius);
	// Target stays fixed - camera continues looking at same point
}

// ---------------------------------------------------------------------------
// Zoom - move camera toward/away from target (scroll wheel)
// ---------------------------------------------------------------------------

void CameraController::Zoom(Camera& camera, const InputState& input, float speed)
{
	const glm::vec3 pos = camera.GetPosition();
	const glm::vec3& target = camera.cam_tar;

	const glm::vec3 dir = pos - target;
	const float radius = glm::length(dir);
	if (radius < 1e-6f) return;

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

// ---------------------------------------------------------------------------
// Dolly - move camera forward/back along view direction (Ctrl+MMB drag)
// ---------------------------------------------------------------------------

void CameraController::Dolly(Camera& camera, const InputState& input, float speed)
{
	const glm::vec3 pos = camera.GetPosition();
	const glm::vec3& target = camera.cam_tar;

	// Forward direction: from camera position toward look-at target
	glm::vec3 dir = target - pos;
	const float len = glm::length(dir);
	if (len < 1e-6f) return;
	dir /= len;

	// Positive mouseDeltaY = move forward (toward target)
	const float dollyAmount = input.mouseDeltaY * kDollySensitivity * speed;
	const glm::vec3 offset = dir * dollyAmount;

	camera.SetCamPos(pos + offset);
	// Target stays fixed - Dolly moves only camera along view axis
}

// ---------------------------------------------------------------------------
// Pan - translate camera parallel to view plane (Shift+MMB drag)
// ---------------------------------------------------------------------------

void CameraController::Pan(Camera& camera, const InputState& input, float speed)
{
	const glm::vec3 pos = camera.GetPosition();
	const glm::vec3& target = camera.cam_tar;

	// Forward: target → camera (viewing direction is camera looking at target)
	glm::vec3 forward = pos - target;
	if (glm::length(forward) < 1e-6f) return;
	forward = glm::normalize(forward);

	// Right: cross(forward, world-up)
	constexpr glm::vec3 kWorldUp(0.0f, 1.0f, 0.0f);
	glm::vec3 right = glm::cross(forward, kWorldUp);

	// Handle degenerate case: camera looking straight up/down
	if (glm::length(right) < 1e-6f)
	{
		right = glm::vec3(1.0f, 0.0f, 0.0f);
	}
	right = glm::normalize(right);

	// Up: cross(right, forward) - camera-local up (not world up)
	const glm::vec3 up = glm::cross(right, forward);

	// Compute translation: horizontal mouse = right, vertical mouse = up
	const float panSensitivity = kPanSensitivity * speed;
	const glm::vec3 delta = input.mouseDeltaX * panSensitivity * right
	                        - input.mouseDeltaY * panSensitivity * up;

	camera.SetCamPos(pos + delta);
	camera.SetTarPos(target + delta);
}

} // namespace neurus
