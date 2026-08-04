/**
 * @file CameraController.h
 * @brief Camera manipulation controller — event-driven, no per-frame polling.
 *
 * CameraController translates discrete camera events (zoom, rotate, push, slide)
 * into camera transform updates. All navigation logic is triggered by events
 * enqueued from the Editor layer (which reads raw input and normalizes it).
 *
 * Continuous drags (orbit / pan / dolly) are bounded by a controller-owned
 * gesture: CameraDragBegin captures the "before" pose, the drag handlers mutate
 * the camera live WITHOUT recording, and CameraDragEnd submits a single
 * CameraTransformOp for the whole gesture. CameraTransformOp is non-mergeable,
 * so each drag is its own undo entry. Scroll zoom has no press/release, so it
 * keeps recording per-event as a separate CameraZoomOp type, which is mergeable
 * (keyed per camera) and coalesces the scroll burst into one undo entry.
 *
 * Event Mapping:
 *   - CameraDragBegin   → capture before pose (mouse press)
 *   - CameraDragEnd     → record one CameraTransformOp (mouse release)
 *   - CameraZoomEvent   → Zoom  (scroll wheel; recorded as CameraZoomOp, merged)
 *   - CameraRotateEvent → Orbit (live mutate during drag)
 *   - CameraPushEvent   → Dolly (live mutate during drag)
 *   - CameraSlideEvent  → Pan   (live mutate during drag)
 *   - CameraResizeEvent → Resize (update aspect ratio on viewport resize)
 *
 * Coordinate System:
 *   - Right-handed Z-up world space (+Y forward)
 *   - Camera forward: derived from target - position
 *
 * Architecture:
 *   - bound to an EventQueue via Init() — no per-frame Update() polling
 *   - Operates on Camera* provided by each event
 *
 * @note CameraController does not own the camera — it operates on a pointer.
 * @note Speed control is external — Editor scales deltas before enqueuing.
 */
#pragma once

#include "editor/controllers/Controllers.h"
#include "editor/events/EventBus.h"
#include "editor/operations/SceneOperations.h"

namespace neurus {

class Camera;

// ---------------------------------------------------------------------------
// CameraController
// ---------------------------------------------------------------------------

/**
 * @brief Event-driven camera manipulation controller.
 *
 * Holds a small gesture state (the pose captured at drag start + the target
 * camera) so a continuous orbit/pan/dolly collapses to one undo entry. Handler
 * math lives in free functions in CameraController.cpp; the gesture bookkeeping
 * lives here.
 *
 * Usage:
 * @code
 *   CameraController controller;
 *   controller.Init(eventQueue(), operationSink());
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
	 * Registers handlers for the drag gesture (begin/end), the continuous drag
	 * moves (rotate/push/slide), scroll zoom, and resize. Must be called once
	 * during initialization, before any events are enqueued.
	 *
	 * @param bus EventQueue to subscribe to.
	 * @param ops Operation sink; a drag gesture records one coarse-grained
	 *        CameraTransformOp on release, scroll zoom records a CameraZoomOp
	 *        per-event (merged into one undo entry).
	 */
	void Init(EventQueue& bus, IOperationSink& ops) override;

private:
	bool m_dragging = false;   ///< True between CameraDragBegin and CameraDragEnd.
	Camera* m_cam = nullptr;   ///< Camera captured at drag start (non-owning).
	CameraPose m_before{};     ///< Pose captured at drag start (undo endpoint).
};

} // namespace neurus
