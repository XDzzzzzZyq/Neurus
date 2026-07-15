/**
 * @file CameraController.cpp
 * @brief Event-driven camera navigation: orbit (rotate), dolly (push), pan (slide), zoom.
 *
 * Stateless — all handlers are free functions in an anonymous namespace.
 * Each handler receives a discrete camera event, extracts the camera
 * pointer and delta values, and applies the corresponding transform math.
 * Speed control is external — Editor scales deltas before enqueuing events.
 */

#include "editor/controllers/CameraController.h"
#include "editor/events/CameraEvents.h"
#include "editor/events/EventBus.h"
#include "scene/Camera.h"

#include <algorithm>
#include <cmath>

namespace {

// ---------------------------------------------------------------------------
// Sensitivity constants
// ---------------------------------------------------------------------------

/** @brief Base sensitivity for orbit rotation (radians per pixel). */
constexpr float kOrbitSensitivity = 0.005f;

/** @brief Base sensitivity for pan translation (world units per pixel). */
constexpr float kPanSensitivity = 0.01f;

/** @brief Base sensitivity for zoom (dolly factor per scroll notch). */
constexpr float kZoomSensitivity = 1.0f;

/** @brief Base sensitivity for dolly translation (world units per pixel). */
constexpr float kDollySensitivity = 0.05f;

// ---------------------------------------------------------------------------
// Event notification
// ---------------------------------------------------------------------------

/**
 * @brief Notifies downstream systems of a camera transform change.
 * @param camera Camera that was modified.
 * @note Stub — will enqueue a CameraTransformChanged event in the future.
 */
void NotifyCameraChanged(const neurus::Camera& camera)
{
	(void)camera;
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------

/**
 * @brief Handles CameraZoomEvent — moves camera toward/away from target.
 */
void OnCameraZoom(const neurus::CameraZoomEvent& e)
{
	neurus::Camera& camera = *e.cam;

	const glm::vec3 pos = camera.GetPosition();
	const glm::vec3& target = camera.cam_tar;

	const glm::vec3 dir = pos - target;
	const float radius = glm::length(dir);
	if (radius < 1e-6f) return;

	// Zoom factor: scroll up → closer, scroll down → farther
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

/**
 * @brief Handles CameraRotateEvent — orbits camera around target.
 */
void OnCameraRotate(const neurus::CameraRotateEvent& e)
{
	neurus::Camera& camera = *e.cam;

	const glm::vec3 pos = camera.GetPosition();
	const glm::vec3& target = camera.cam_tar;

	glm::vec3 dir = pos - target;
	const float radius = glm::length(dir);
	if (radius < 1e-6f) return;

	dir /= radius; // normalize

	// Compute spherical coordinates (Z-up)
	const float elevation = std::asin(dir.z);                     // [-PI/2, PI/2]
	const float azimuth   = std::atan2(dir.x, dir.y);             // [-PI, PI] — angle from +Y (forward)

	// Apply mouse delta (inverted for natural drag feel)
	const float deltaAzimuth   = e.mouse_delta_x * kOrbitSensitivity;
	const float deltaElevation = e.mouse_delta_y * kOrbitSensitivity;

	// Clamp elevation to avoid flipping at +/-89 degrees
	constexpr float kMaxElevation = glm::radians(89.0f);
	const float newElevation = std::clamp(elevation + deltaElevation, -kMaxElevation, kMaxElevation);
	const float newAzimuth   = azimuth + deltaAzimuth;

	// Convert back to Cartesian
	const float cosPhi = std::cos(newElevation);
	const glm::vec3 newDir(
		cosPhi * std::sin(newAzimuth),
		cosPhi * std::cos(newAzimuth),
		std::sin(newElevation)
	);

	camera.SetCamPos(target + newDir * radius);

	NotifyCameraChanged(camera);
}

/**
 * @brief Handles CameraPushEvent — dollies camera along view direction (Ctrl+MMB).
 */
void OnCameraPush(const neurus::CameraPushEvent& e)
{
	neurus::Camera& camera = *e.cam;

	const glm::vec3 pos = camera.GetPosition();
	const glm::vec3& target = camera.cam_tar;

	// Forward direction: from camera position toward look-at target
	glm::vec3 dir = target - pos;
	const float len = glm::length(dir);
	if (len < 1e-6f) return;
	dir /= len;

	// Positive mouse_delta_y = move forward (toward target)
	const float dollyAmount = e.mouse_delta_y * kDollySensitivity;
	const glm::vec3 offset = dir * dollyAmount;

	camera.SetCamPos(pos + offset);

	NotifyCameraChanged(camera);
}

/**
 * @brief Handles CameraSlideEvent — pans camera parallel to view plane (Shift+MMB).
 */
void OnCameraSlide(const neurus::CameraSlideEvent& e)
{
	neurus::Camera& camera = *e.cam;

	const glm::vec3 pos = camera.GetPosition();
	const glm::vec3& target = camera.cam_tar;

	// Forward: target → camera (viewing direction is camera looking at target)
	glm::vec3 forward = pos - target;
	if (glm::length(forward) < 1e-6f) return;
	forward = glm::normalize(forward);

	// Right: cross(forward, world-up)
	constexpr glm::vec3 kWorldUp(0.0f, 0.0f, 1.0f);
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
	const glm::vec3 delta = e.mouse_delta_x * kPanSensitivity * right
	                        + e.mouse_delta_y * kPanSensitivity * up;

	camera.SetCamPos(pos + delta);
	camera.SetTarPos(target + delta);

	NotifyCameraChanged(camera);
}

/**
 * @brief Handles CameraResizeEvent — updates camera aspect ratio on viewport resize.
 */
void OnCameraResize(const neurus::CameraResizeEvent& e)
{
	neurus::Camera& camera = *e.cam;
	camera.ChangeCamRatio(static_cast<float>(e.width), static_cast<float>(e.height));
	NotifyCameraChanged(camera);
}

} // anonymous namespace

namespace neurus {

// ---------------------------------------------------------------------------
// Init — subscribe to camera events
// ---------------------------------------------------------------------------

void CameraController::Init(EventQueue& bus)
{
	bus.subscribe<CameraZoomEvent>([](const CameraZoomEvent& e) { OnCameraZoom(e); });
	bus.subscribe<CameraRotateEvent>([](const CameraRotateEvent& e) { OnCameraRotate(e); });
	bus.subscribe<CameraPushEvent>([](const CameraPushEvent& e) { OnCameraPush(e); });
	bus.subscribe<CameraSlideEvent>([](const CameraSlideEvent& e) { OnCameraSlide(e); });
	bus.subscribe<CameraResizeEvent>([](const CameraResizeEvent& e) { OnCameraResize(e); });
}

} // namespace neurus
