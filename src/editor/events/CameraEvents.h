#pragma once

namespace neurus {

/**
 * @brief Camera navigation events carry the camera's integer UID (not a raw
 * Camera* pointer — the camera could be deleted mid-drag; a UID stays a stable
 * identity). CameraController resolves the id against the current scene at
 * dispatch time.
 */

struct CameraZoomEvent {
	int camId;
	float scroll_dir;
};

struct CameraRotateEvent {
	int camId;
	float mouse_delta_x, mouse_delta_y;
};

struct CameraPushEvent {
	int camId;
	float mouse_delta_x, mouse_delta_y;
};

struct CameraSlideEvent {
	int camId;
	float mouse_delta_x, mouse_delta_y;
};

struct CameraSpinEvent {
	int camId;
	float mouse_delta_x, mouse_delta_y;
};

struct CameraResizeEvent {
	int camId;
	int width; int height;
};

/**
 * @brief Emitted when a continuous camera drag gesture begins (mouse press).
 *
 * Marks the start of an orbit/pan/dolly manipulation. CameraController captures
 * the camera's "before" pose on this event and mutates the camera live during
 * the drag without recording, so the whole gesture collapses to one undo entry
 * committed on CameraDragEnd.
 */
struct CameraDragBegin {
	int camId;
};

/**
 * @brief Emitted when a continuous camera drag gesture ends (mouse release).
 *
 * CameraController records a single CameraTransformOp spanning the pose captured
 * at CameraDragBegin through the current pose, if the pose actually changed.
 */
struct CameraDragEnd {
	int camId;
};

} // namespace neurus
