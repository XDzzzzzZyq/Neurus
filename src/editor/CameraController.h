/**
 * @file CameraController.h
 * @brief Camera manipulation controller for interactive viewpoint control.
 *
 * CameraController translates mouse input into camera transform updates,
 * implementing standard 3D viewport navigation: orbit, pan, and zoom.
 *
 * Architecture:
 * - Receives InputState each frame via Update()
 * - Modifies active Camera's position and look-at target directly
 * - Notifies Scene of camera changes via Scene::UpdateSceneStatus()
 *
 * Camera Modes:
 * - Orbit (RMB drag): Rotate camera around look-at target (tumble)
 * - Pan (MMB drag or Shift+RMB): Translate camera parallel to view plane
 * - Zoom (scroll wheel): Move camera toward/away from target (dolly)
 *
 * Coordinate System:
 * - Right-handed Y-up world space
 * - Camera forward: derived from target - position
 *
 * Speed Adjustment:
 * - Shift held: 2x sensitivity
 * - Ctrl held: 0.25x sensitivity
 *
 * @note CameraController does not own the camera - it operates on a reference.
 * @note Input system (T55) will provide IsMousePressed, GetDeltaMouse, GetScroll
 *       methods. InputState struct bridges until T55 is implemented.
 */
#pragma once

#include <glm/glm.hpp>

namespace neurus {

class Camera;
class Scene;

// ---------------------------------------------------------------------------
// InputState - raw input data consumed by CameraController each frame
// ---------------------------------------------------------------------------

/**
 * @brief Raw input state consumed by CameraController::Update() each frame.
 *
 * Provides the mouse and keyboard state needed for camera manipulation.
 * This struct is filled by the Input system (T55) and passed to controllers.
 *
 * Field meanings:
 * - mouseDeltaX/Y: Cursor position delta since last frame (pixels)
 * - scrollDelta: Scroll wheel delta (+1 up, -1 down per notch)
 * - rightMouseHeld: RMB currently pressed (orbit mode)
 * - middleMouseHeld: MMB currently pressed (pan mode)
 * - shiftHeld: Shift modifier (fast speed)
 * - ctrlHeld: Ctrl modifier (slow speed)
 */
struct InputState
{
	float mouseDeltaX = 0.0f;
	float mouseDeltaY = 0.0f;
	float scrollDelta = 0.0f;
	bool rightMouseHeld = false;
	bool middleMouseHeld = false;
	bool shiftHeld = false;
	bool ctrlHeld = false;
};

// ---------------------------------------------------------------------------
// CameraController
// ---------------------------------------------------------------------------

/**
 * @brief Controller for interactive camera manipulation via mouse input.
 *
 * CameraController translates mouse input into camera transform updates,
 * implementing standard 3D viewport navigation patterns.
 *
 * Supported Operations:
 * - **Orbit** (RMB drag): Rotate camera around target point. Horizontal
 *   mouse movement rotates around world Y axis; vertical movement rotates
 *   around camera's local right axis.
 * - **Pan** (MMB drag or Shift+RMB): Translate camera and target
 *   perpendicular to the view direction using camera's right and up vectors.
 * - **Zoom** (scroll wheel): Move camera toward or away from target along
 *   the line-of-sight direction.
 *
 * Speed Adjustment:
 * - Normal: 1.0x sensitivity
 * - Shift held: 2.0x (fast)
 * - Ctrl held: 0.25x (slow)
 * - Both held: Shift wins (2.0x)
 *
 * Scene Notification:
 * - Each successful modification calls Scene::UpdateSceneStatus(CameraChanged).
 *
 * Usage:
 * @code
 *   CameraController controller;
 *   // ... per frame:
 *   InputState input = inputSystem.GetState();
 *   controller.Update(*activeCamera, *scene, input);
 * @endcode
 *
 * @note CameraController does not own the camera - it operates on a reference.
 * @note No renderer dependency. Uses only Camera, Scene, and glm.
 */
class CameraController
{
public:
	CameraController() = default;

	/**
	 * @brief Updates the camera transform based on input state.
	 *
	 * Reads mouse delta, scroll, and modifier keys from InputState and
	 * applies the corresponding camera operation (orbit, pan, or zoom).
	 * Multiple operations can be combined (e.g., orbit + zoom in one frame).
	 *
	 * @param camera Camera to modify (position and target updated in place).
	 * @param scene Scene to notify of CameraChanged status.
	 * @param input Current frame's input state from the Input system.
	 *
	 * @note No-ops when camera position equals target (degenerate direction).
	 * @note Elevation is clamped to avoid gimbal-lock at ±89°.
	 */
	void Update(Camera& camera, Scene& scene, const InputState& input);

private:
	// --- Sensitivity constants ---

	/** @brief Base sensitivity for orbit rotation (radians per pixel). */
	static constexpr float kOrbitSensitivity = 0.005f;

	/** @brief Base sensitivity for pan translation (world units per pixel). */
	static constexpr float kPanSensitivity = 0.01f;

	/** @brief Base sensitivity for zoom (dolly factor per scroll notch). */
	static constexpr float kZoomSensitivity = 1.0f;

	/** @brief Speed multiplier when Shift is held. */
	static constexpr float kFastMultiplier = 2.0f;

	/** @brief Speed multiplier when Ctrl is held. */
	static constexpr float kSlowMultiplier = 0.25f;

	// --- Speed ---

	/**
	 * @brief Computes the effective speed multiplier from modifier keys.
	 * @param input Current input state.
	 * @return 1.0 (normal), 2.0 (fast), or 0.25 (slow).
	 */
	float GetSpeedMultiplier(const InputState& input) const;

	// --- Camera operations ---

	/**
	 * @brief Orbits the camera around its look-at target.
	 * @param camera Camera to modify.
	 * @param input Current input state (reads mouseDeltaX/Y).
	 * @param speed Effective speed multiplier.
	 */
	void Orbit(Camera& camera, const InputState& input, float speed);

	/**
	 * @brief Pans the camera parallel to the view plane.
	 * @param camera Camera to modify (both position and target).
	 * @param input Current input state (reads mouseDeltaX/Y).
	 * @param speed Effective speed multiplier.
	 */
	void Pan(Camera& camera, const InputState& input, float speed);

	/**
	 * @brief Zooms the camera toward or away from its look-at target.
	 * @param camera Camera to modify.
	 * @param input Current input state (reads scrollDelta).
	 * @param speed Effective speed multiplier.
	 */
	void Zoom(Camera& camera, const InputState& input, float speed);
};

} // namespace neurus
