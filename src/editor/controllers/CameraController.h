/**
 * @file CameraController.h
 * @brief Camera manipulation controller — event-driven, no per-frame polling.
 *
 * CameraController translates discrete camera events (zoom, rotate, push, slide)
 * into camera transform updates. All navigation logic is triggered by events
 * enqueued from the Editor layer (which reads raw input and normalizes it).
 *
 * Event Mapping:
 *   - CameraZoomEvent   → Zoom  (scroll wheel)
 *   - CameraRotateEvent → Orbit (rotate around target)
 *   - CameraPushEvent   → Dolly (move camera forward/back along view direction)
 *   - CameraSlideEvent  → Pan   (translate camera parallel to view plane)
 *
 * Coordinate System:
 *   - Right-handed Y-up world space
 *   - Camera forward: derived from target - position
 *
 * Architecture:
 *   - bound to an EventQueue via Init() — no per-frame Update() polling
 *   - Operates on Camera* provided by each event
 *   - NotifyCameraChanged() stub for future event notification
 *
 * @note CameraController does not own the camera — it operates on a pointer.
 * @note Speed control is external — Editor scales deltas before enqueuing.
 */
#pragma once

#include "editor/controllers/Controllers.h"
#include "editor/events/CameraEvents.h"
#include "editor/Input.h"

namespace neurus {

class Camera;
class EventQueue;

// ---------------------------------------------------------------------------
// CameraController
// ---------------------------------------------------------------------------

/**
 * @brief Event-driven camera manipulation controller.
 *
 * CameraController subscribes to camera events on an EventQueue and transforms
 * each discrete event into a camera transform update. This replaces the
 * previous per-frame Update(Camera&, InputState&) polling model.
 *
 * Supported Operations:
 *   - **Orbit** (CameraRotateEvent): Rotate camera around look-at target.
 *   - **Dolly** (CameraPushEvent): Move camera forward/backward along
 *     the line-of-sight direction.
 *   - **Pan** (CameraSlideEvent): Translate camera and target perpendicular to
 *     the view direction.
 *   - **Zoom** (CameraZoomEvent): Move camera toward or away from target along
 *     the line-of-sight direction.
 *
 * Usage:
 * @code
 *   CameraController controller;
 *   controller.Init(GetEventQueue());
 *   // Editor enqueues events, EventQueue::Process() dispatches them
 * @endcode
 *
 * @note CameraController does not own the camera — it operates on a pointer
 *       provided by each event.
 * @note Speed is controlled externally by scaling delta values before enqueuing.
 */
class CameraController : public Controllers
{
public:
	CameraController() = default;

	/**
	 * @brief Subscribes to camera events on the given EventQueue.
	 *
	 * Registers four lambda handlers that forward each event to the
	 * corresponding private handler method. Must be called once during
	 * initialization, before any events are enqueued.
	 *
	 * @param bus EventQueue to subscribe to.
	 */
	void Init(class EventQueue& bus) override;

private:
	// --- Sensitivity constants ---

	/** @brief Base sensitivity for orbit rotation (radians per pixel). */
	static constexpr float kOrbitSensitivity = 0.005f;

	/** @brief Base sensitivity for pan translation (world units per pixel). */
	static constexpr float kPanSensitivity = 0.01f;

	/** @brief Base sensitivity for zoom (dolly factor per scroll notch). */
	static constexpr float kZoomSensitivity = 1.0f;

	/** @brief Base sensitivity for dolly translation (world units per pixel). */
	static constexpr float kDollySensitivity = 0.05f;

	// --- Event notification ---

	/**
	 * @brief Notifies downstream systems of a camera transform change.
	 * @param camera Camera that was modified.
	 * @note Stub — will enqueue a CameraTransformChanged event in the future.
	 */
	void NotifyCameraChanged(const Camera& camera);

	// --- Event handlers (called via Init() subscriptions) ---

	/**
	 * @brief Handles CameraZoomEvent — moves camera toward/away from target.
	 * @param e Zoom event carrying camera pointer and scroll direction.
	 */
	void OnCameraZoom(const CameraZoomEvent& e);

	/**
	 * @brief Handles CameraRotateEvent — orbits camera around target.
	 * @param e Rotate event carrying camera pointer and mouse deltas.
	 */
	void OnCameraRotate(const CameraRotateEvent& e);

	/**
	 * @brief Handles CameraPushEvent — dollies camera along view direction.
	 * @param e Push event carrying camera pointer and mouse deltas.
	 */
	void OnCameraPush(const CameraPushEvent& e);

	/**
	 * @brief Handles CameraSlideEvent — pans camera parallel to view plane.
	 * @param e Slide event carrying camera pointer and mouse deltas.
	 */
	void OnCameraSlide(const CameraSlideEvent& e);
};

} // namespace neurus
