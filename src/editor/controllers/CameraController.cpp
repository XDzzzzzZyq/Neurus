/**
 * @file CameraController.cpp
 * @brief Event-driven camera navigation: orbit (rotate), dolly (push), pan (slide), zoom.
 *
 * Each event handler receives a discrete camera event, extracts the camera
 * pointer and delta values, and applies the corresponding transform math.
 * Speed control is external — Editor scales deltas before enqueuing events.
 */

#include "editor/controllers/CameraController.h"
#include "editor/events/EventBus.h"
#include "scene/Camera.h"

#include <algorithm>
#include <cmath>

namespace neurus {

// ---------------------------------------------------------------------------
// Event notification
// ---------------------------------------------------------------------------

void CameraController::NotifyCameraChanged(const Camera& camera)
{
	// Stub — will enqueue CameraTransformChanged event in the future
	(void)camera;
}

// ---------------------------------------------------------------------------
// Init — subscribe to camera events
// ---------------------------------------------------------------------------

void CameraController::Init(class EventQueue& bus)
{
	bus.subscribe<CameraZoomEvent>([this](const CameraZoomEvent& e) { OnCameraZoom(e); });
	bus.subscribe<CameraRotateEvent>([this](const CameraRotateEvent& e) { OnCameraRotate(e); });
	bus.subscribe<CameraPushEvent>([this](const CameraPushEvent& e) { OnCameraPush(e); });
	bus.subscribe<CameraSlideEvent>([this](const CameraSlideEvent& e) { OnCameraSlide(e); });
}

// ---------------------------------------------------------------------------
// OnCameraZoom — scroll wheel → move camera toward/away from target
// ---------------------------------------------------------------------------

void CameraController::OnCameraZoom(const CameraZoomEvent& e)
{
	Camera& camera = *e.cam;

	const glm::vec3 pos = camera.GetPosition();
	const glm::vec3& target = camera.cam_tar;

	const glm::vec3 dir = pos - target;
	const float radius = glm::length(dir);
	if (radius < 1e-6f) return;

	// Zoom factor: scroll up → closer, scroll down → farther
	// Speed = 1.0f (Editor scales e.scroll_dir before enqueuing)
	const float factor = std::pow(0.8f, e.scroll_dir * kZoomSensitivity);
	const float newRadius = radius * factor;

	// Clamp to prevent going through target or flying to infinity
	constexpr float kMinRadius = 0.01f;
	constexpr float kMaxRadius = 1000.0f;
	const float clampedRadius = std::clamp(newRadius, kMinRadius, kMaxRadius);

	const glm::vec3 newDir = glm::normalize(dir) * clampedRadius;
	camera.SetCamPos(target + newDir);

	NotifyCameraChanged(camera);
}

// ---------------------------------------------------------------------------
// OnCameraRotate — orbit camera around look-at target
// ---------------------------------------------------------------------------

void CameraController::OnCameraRotate(const CameraRotateEvent& e)
{
	Camera& camera = *e.cam;

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
	// Speed = 1.0f (Editor scales deltas before enqueuing)
	const float deltaAzimuth   = -e.mouse_delta_x * kOrbitSensitivity;
	const float deltaElevation = -e.mouse_delta_y * kOrbitSensitivity;

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
	// Target stays fixed — camera continues looking at same point

	NotifyCameraChanged(camera);
}

// ---------------------------------------------------------------------------
// OnCameraPush — dolly camera forward/back along view direction (Ctrl+MMB)
// ---------------------------------------------------------------------------

void CameraController::OnCameraPush(const CameraPushEvent& e)
{
	Camera& camera = *e.cam;

	const glm::vec3 pos = camera.GetPosition();
	const glm::vec3& target = camera.cam_tar;

	// Forward direction: from camera position toward look-at target
	glm::vec3 dir = target - pos;
	const float len = glm::length(dir);
	if (len < 1e-6f) return;
	dir /= len;

	// Positive mouse_delta_y = move forward (toward target)
	// Speed = 1.0f (Editor scales deltas before enqueuing)
	const float dollyAmount = e.mouse_delta_y * kDollySensitivity;
	const glm::vec3 offset = dir * dollyAmount;

	camera.SetCamPos(pos + offset);
	// Target stays fixed — Dolly moves only camera along view axis

	NotifyCameraChanged(camera);
}

// ---------------------------------------------------------------------------
// OnCameraSlide — pan camera parallel to view plane (Shift+MMB)
// ---------------------------------------------------------------------------

void CameraController::OnCameraSlide(const CameraSlideEvent& e)
{
	Camera& camera = *e.cam;

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

	// Up: cross(right, forward) — camera-local up (not world up)
	const glm::vec3 up = glm::cross(right, forward);

	// Compute translation: horizontal mouse = right, vertical mouse = up
	// Speed = 1.0f (Editor scales deltas before enqueuing)
	const glm::vec3 delta = e.mouse_delta_x * kPanSensitivity * right
	                        - e.mouse_delta_y * kPanSensitivity * up;

	camera.SetCamPos(pos + delta);
	camera.SetTarPos(target + delta);

	NotifyCameraChanged(camera);
}

} // namespace neurus
