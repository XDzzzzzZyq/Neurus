#pragma once

#include "scene/Camera.h"

namespace neurus {

struct CameraZoomEvent {
    Camera* cam;
    float scroll_dir;
};

struct CameraRotateEvent {
    Camera* cam;
    float mouse_delta_x, mouse_delta_y;
};

struct CameraPushEvent {
    Camera* cam;
    float mouse_delta_x, mouse_delta_y;
};

struct CameraSlideEvent {
    Camera* cam;
    float mouse_delta_x, mouse_delta_y;
};

struct CameraSpinEvent {
    Camera* cam;
    float mouse_delta_x, mouse_delta_y;
};

struct CameraResizeEvent {
    Camera* cam;
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
    Camera* cam;
};

/**
 * @brief Emitted when a continuous camera drag gesture ends (mouse release).
 *
 * CameraController records a single CameraTransformOp spanning the pose captured
 * at CameraDragBegin through the current pose, if the pose actually changed.
 */
struct CameraDragEnd {
    Camera* cam;
};

} // namespace neurus
