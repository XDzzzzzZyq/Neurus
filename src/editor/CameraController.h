/**
 * @file CameraController.h
 * @brief Camera manipulation controller for MMB-based interactive viewpoint control.
 *
 * CameraController translates mouse input into camera transform updates using the
 * Middle Mouse Button (MMB) convention, a standard pattern in 3D viewport navigation.
 *
 * MMB Convention:
 *   - MMB alone     = Orbit (rotate around target)
 *   - Ctrl + MMB    = Dolly (move camera forward/back along view direction)
 *   - Shift + MMB   = Pan (translate camera parallel to view plane)
 *   - Scroll        = Zoom (move camera toward/away from target)
 *
 * Speed modifiers:
 *   - Ctrl held: 0.25x sensitivity (slow/precise)
 *   - No ctrl: 1.0x (normal)
 *
 * Coordinate System:
 *   - Right-handed Y-up world space
 *   - Camera forward: derived from target - position
 *
 * Architecture:
 *   - Receives InputState each frame via Update()
 *   - Modifies active Camera's position and look-at target directly
 *   - NotifyCameraChanged() stub for future event notification
 *
 * @note CameraController does not own the camera - it operates on a reference.
 * @note No renderer or event dependency. Uses only Camera and glm.
 */
#pragma once

#include <glm/glm.hpp>

namespace neurus {

class Camera;

// ---------------------------------------------------------------------------
// InputState - raw input data consumed by CameraController each frame
// ---------------------------------------------------------------------------

/**
 * @brief Raw input state consumed by CameraController::Update() each frame.
 *
 * Provides mouse and keyboard modifier state needed for MMB-based camera
 * manipulation. No WASD fields - all navigation is mouse-driven.
 *
 * Field meanings:
 *   - mouseDeltaX/Y: Cursor position delta since last frame (pixels)
 *   - scrollDelta: Scroll wheel delta (+1 up, -1 down per notch)
 *   - leftMouseHeld: LMB currently pressed (reserved for future use)
 *   - middleMouseHeld: MMB currently pressed - primary camera control button
 *   - rightMouseHeld: RMB currently pressed (reserved for future use)
 *   - shiftHeld: Shift modifier (Shift+MMB = Pan)
 *   - ctrlHeld: Ctrl modifier (Ctrl+MMB = Dolly; also 0.25x speed)
 *   - altHeld: Alt modifier (reserved for future use)
 */
struct InputState
{
	float mouseDeltaX = 0.0f;
	float mouseDeltaY = 0.0f;
	float scrollDelta = 0.0f;
	bool leftMouseHeld = false;
	bool middleMouseHeld = false;
	bool rightMouseHeld = false;
	bool shiftHeld = false;
	bool ctrlHeld = false;
	bool altHeld = false;
};

// ---------------------------------------------------------------------------
// CameraController
// ---------------------------------------------------------------------------

/**
 * @brief Controller for MMB-based interactive camera manipulation.
 *
 * CameraController implements the standard 3D viewport MMB navigation convention:
 * orbit, dolly, pan (via MMB with modifiers), and zoom (via scroll wheel).
 *
 * Supported Operations:
 *   - **Orbit** (MMB drag): Rotate camera around look-at target. Horizontal
 *     mouse movement rotates around world Y axis; vertical movement rotates
 *     around camera's local right axis.
 *   - **Dolly** (Ctrl+MMB drag vertical): Move camera forward/backward along
 *     the line-of-sight direction. Positive deltaY = move forward.
 *   - **Pan** (Shift+MMB drag): Translate camera and target perpendicular to
 *     the view direction using camera's right and up vectors.
 *   - **Zoom** (scroll wheel): Move camera toward or away from target along
 *     the line-of-sight direction. Always active, no modifier required.
 *
 * Speed Adjustment:
 *   - Normal: 1.0x sensitivity
 *   - Ctrl held: 0.25x (slow/precise)
 *
 * Usage:
 * @code
 *   CameraController controller;
 *   // ... per frame:
 *   InputState input = Input::GetInputState();
 *   controller.Update(camera, input);
 * @endcode
 *
 * @note CameraController does not own the camera - it operates on a reference.
 * @note No renderer or event dependency. Uses only Camera and glm.
 */
class CameraController
{
public:
	CameraController() = default;

	/**
	 * @brief Updates the camera transform based on input state.
	 *
	 * Reads mouse delta, scroll, and modifier keys from InputState and
	 * applies the corresponding camera operation based on MMB convention:
	 *   - MMB alone        → Orbit()
	 *   - Ctrl + MMB       → Dolly()
	 *   - Shift + MMB      → Pan()
	 *   - Scroll (always)  → Zoom()
	 *
	 * Multiple operations can be combined in one frame (e.g., scroll zoom
	 * while orbiting with MMB).
	 *
	 * @param camera Camera to modify (position and target updated in place).
	 * @param input Current frame's input state from the Input system.
	 *
	 * @note No-ops when camera position equals target (degenerate direction).
	 * @note Elevation is clamped to avoid gimbal-lock at +/-89 degrees.
	 */
	void Update(Camera& camera, const InputState& input);

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

	/** @brief Speed multiplier when Ctrl is held (slow/precise). */
	static constexpr float kSlowMultiplier = 0.25f;

	// --- Speed ---

	/**
	 * @brief Computes the effective speed multiplier from modifier keys.
	 * @param input Current input state.
	 * @return 1.0f (normal) or kSlowMultiplier (slow when Ctrl held).
	 */
	float GetSpeedMultiplier(const InputState& input) const;

	// --- Event notification ---

	/**
	 * @brief Notifies downstream systems of a camera transform change.
	 * @param camera Camera that was modified.
	 * @note Stub - will enqueue a CameraTransformChanged event in the future.
	 */
	void NotifyCameraChanged(const Camera& camera);

	// --- Camera operations ---

	/**
	 * @brief Orbits the camera around its look-at target (MMB drag).
	 *
	 * Horizontal mouse delta rotates azimuth around world Y axis.
	 * Vertical mouse delta rotates elevation around camera's local right axis.
	 * Elevation is clamped to +/-89 degrees to avoid gimbal lock.
	 *
	 * @param camera Camera to modify.
	 * @param input Current input state (reads mouseDeltaX/Y).
	 * @param speed Effective speed multiplier.
	 */
	void Orbit(Camera& camera, const InputState& input, float speed);

	/**
	 * @brief Zooms the camera toward or away from its look-at target (scroll wheel).
	 *
	 * Moves camera along the line-of-sight direction. scrollDelta > 0
	 * moves toward target; scrollDelta < 0 moves away.
	 *
	 * @param camera Camera to modify.
	 * @param input Current input state (reads scrollDelta).
	 * @param speed Effective speed multiplier.
	 */
	void Zoom(Camera& camera, const InputState& input, float speed);

	/**
	 * @brief Dollies the camera forward/backward along the view direction (Ctrl+MMB drag).
	 *
	 * Translates both camera position and target along the line-of-sight
	 * direction, maintaining the current framing. Positive mouseDeltaY
	 * moves forward, negative moves backward.
	 *
	 * @param camera Camera to modify (both position and target).
	 * @param input Current input state (reads mouseDeltaY).
	 * @param speed Effective speed multiplier.
	 */
	void Dolly(Camera& camera, const InputState& input, float speed);

	/**
	 * @brief Pans the camera parallel to the view plane (Shift+MMB drag).
	 *
	 * Translates both camera position and target using camera's right
	 * and up vectors. Horizontal mouse delta pans along right vector;
	 * vertical mouse delta pans along up vector.
	 *
	 * @param camera Camera to modify (both position and target).
	 * @param input Current input state (reads mouseDeltaX/Y).
	 * @param speed Effective speed multiplier.
	 */
	void Pan(Camera& camera, const InputState& input, float speed);
};

} // namespace neurus
