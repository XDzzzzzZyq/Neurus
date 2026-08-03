/**
 * @file CameraController.h
 * @brief Camera manipulation controller — event-driven, no per-frame polling.
 *
 * CameraController translates discrete camera events (zoom, rotate, push, slide)
 * into camera transform updates. All navigation logic is triggered by events
 * enqueued from the Editor layer (which reads raw input and normalizes it).
 *
 * Stateless — all handler logic lives as free functions in the .cpp file.
 *
 * Event Mapping:
 *   - CameraZoomEvent   → Zoom  (scroll wheel)
 *   - CameraRotateEvent → Orbit (rotate around target)
 *   - CameraPushEvent   → Dolly (move camera forward/back along view direction)
 *   - CameraSlideEvent  → Pan   (translate camera parallel to view plane)
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

namespace neurus {

// ---------------------------------------------------------------------------
// CameraController
// ---------------------------------------------------------------------------

/**
 * @brief Event-driven camera manipulation controller.
 *
 * Stateless — subscribes to camera events on an EventQueue and dispatches
 * to free-function handlers defined in CameraController.cpp.
 *
 * Usage:
 * @code
 *   CameraController controller;
 *   controller.Init(eventQueue());
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
	 * Registers lambda handlers that forward each event to the
	 * corresponding free-function handler. Must be called once during
	 * initialization, before any events are enqueued.
	 *
	 * @param bus EventQueue to subscribe to.
	 * @param ops Operation sink (unused for now; camera gesture ops land in a
	 *        later Phase 1 step).
	 */
	void Init(EventQueue& bus, IOperationSink& ops) override;

};

} // namespace neurus
