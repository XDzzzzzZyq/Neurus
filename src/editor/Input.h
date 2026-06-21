/**
 * @file Input.h
 * @brief Static input capture system - bridges Qt events to per-frame queryable state.
 *
 * Input provides a unified, double-buffered interface for querying keyboard and
 * mouse state each frame. Qt events (pushed from VulkanWidget) write into a
 * *pending* buffer; UpdateState() snapshots pending→current and current→previous,
 * enabling "clicked" / "released" transition detection.
 *
 * Architecture:
 * - Pure static utility - no QObject, no constructor, no instance.
 * - Three-stage buffer pipeline: pending → curr → prev.
 * - UpdateState() (called once per frame) performs the swap and computes
 *   per-frame mouse delta.
 * - Query methods read from the *curr* snapshot.
 *
 * Usage Pattern:
 * @code
 *   // --- VulkanWidget (Qt event dispatch) ---
 *   void VulkanWidget::keyPressEvent(QKeyEvent* e) {
 *       Input::RecordKeyPress(e->key());
 *       QWidget::keyPressEvent(e);
 *   }
 *
 *   // --- Application render loop ---
 *   Input::UpdateState();
 *   if (Input::IsKeyClicked(Qt::Key_W)) { ... }
 *   InputState camInput = Input::GetInputState();
 *   cameraController.Update(camera, scene, camInput);
 * @endcode
 *
 * @note Editor Layer: Input is part of the Editor system, not UI or Renderer.
 * @note Not thread-safe - must be used from the main (Qt) thread only.
 * @note Key codes follow Qt::Key values; mouse buttons use Input::MouseButton.
 */
#pragma once

#include "scene/Camera.h"

namespace neurus {

// ---------------------------------------------------------------------------
// InputState - raw input data consumed by Editor::Edit() each frame
// ---------------------------------------------------------------------------

/**
 * @brief Raw input state consumed by Editor::Edit() each frame.
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

class Input
{
public:
    // -----------------------------------------------------------------------
    // Mouse button identifiers
    // -----------------------------------------------------------------------

    /** @brief Mouse button index for internal button-state arrays. */
    enum MouseButton
    {
        Left = 0,   ///< Left mouse button
        Right = 1,  ///< Right mouse button
        Middle = 2  ///< Middle mouse button
    };

    // -----------------------------------------------------------------------
    // Recording - called from VulkanWidget Qt event handlers
    // -----------------------------------------------------------------------

    /**
     * @brief Records a key press from a Qt key event.
     * @param qtKey Qt::Key value (e.g. Qt::Key_W, Qt::Key_Shift).
     */
    static void RecordKeyPress(int qtKey);

    /**
     * @brief Records a key release from a Qt key event.
     * @param qtKey Qt::Key value.
     */
    static void RecordKeyRelease(int qtKey);

    /**
     * @brief Records mouse movement.
     * @param x Cursor X in widget-local coordinates.
     * @param y Cursor Y in widget-local coordinates.
     */
    static void RecordMouseMove(float x, float y);

    /**
     * @brief Records a mouse button press.
     * @param button Which button was pressed (Input::Left/Right/Middle).
     */
    static void RecordMousePress(MouseButton button);

    /**
     * @brief Records a mouse button release.
     * @param button Which button was released.
     */
    static void RecordMouseRelease(MouseButton button);

    /**
     * @brief Records a scroll wheel event.
     * @param delta Scroll delta (angleDelta().y() / 120.0f typically yields
     *              ~±1 per notch).
     */
    static void RecordScroll(float delta);

    // -----------------------------------------------------------------------
    // Frame update - called once per frame before any input query
    // -----------------------------------------------------------------------

    /**
     * @brief Snapshots the accumulated input state for this frame.
     *
     * Must be called at the start of each frame:
     * - prev ← curr (current becomes previous)
     * - curr ← pending (pending events become the snapshot)
     * - Resets scroll accumulator in pending.
     * - Computes mouse delta from old vs new mouse position.
     *
     * @note Call BEFORE DrawFrame and BEFORE any controller Update() call.
     */
    static void UpdateState();

    // -----------------------------------------------------------------------
    // Keyboard queries
    // -----------------------------------------------------------------------

    /** @brief true if @p qtKey is currently held. */
    static bool IsKeyPressed(int qtKey);

    /** @brief true if @p qtKey transitioned up→down THIS frame. */
    static bool IsKeyClicked(int qtKey);

    /** @brief true if @p qtKey transitioned down→up THIS frame. */
    static bool IsKeyReleased(int qtKey);

    /** @brief true if either Shift key is held. */
    static bool IsShiftHeld();

    /** @brief true if either Ctrl key is held. */
    static bool IsCtrlHeld();

    /** @brief true if either Alt key is held. */
    static bool IsAltHeld();

    // -----------------------------------------------------------------------
    // Mouse queries
    // -----------------------------------------------------------------------

    /** @return Current mouse X in widget-local coordinates. */
    static float GetMouseX();

    /** @return Current mouse Y in widget-local coordinates. */
    static float GetMouseY();

    /** @return Mouse X delta since last UpdateState() call (pixels). */
    static float GetDeltaMouseX();

    /** @return Mouse Y delta since last UpdateState() call (pixels). */
    static float GetDeltaMouseY();

    /** @return Scroll delta accumulated since last UpdateState() call. */
    static float GetScrollDelta();

    /** @brief true if @p button is currently held. */
    static bool IsMouseButtonPressed(MouseButton button);

    /** @brief true if @p button was just pressed THIS frame. */
    static bool IsMouseButtonClicked(MouseButton button);

    /** @brief true if @p button was just released THIS frame. */
    static bool IsMouseButtonReleased(MouseButton button);

    // -----------------------------------------------------------------------
    // Convenience - fills the struct consumed by CameraController
    // -----------------------------------------------------------------------

    /**
     * @brief Builds an InputState snapshot from the current internal state.
     *
     * The returned struct is suitable for passing directly to
     * CameraController::Update().
     *
     * @return InputState populated with mouse delta, scroll, and modifier keys.
     */
    static InputState GetInputState();

    // -----------------------------------------------------------------------
    // Active camera for event generation
    // -----------------------------------------------------------------------

    /**
     * @brief Sets the active camera used for generating camera events.
     * @param cam Pointer to the active Camera (non-owning).
     */
    static void SetActiveCamera(Camera* cam);

    /** @return The currently set active camera, or nullptr if not set. */
    static Camera* GetActiveCamera();

private:
    // --- Constant dimensions ---
    static constexpr int kMaxKeys        = 256;
    static constexpr int kMaxMouseButtons = 3;

    // --- Key state buffers ---
    // pending: written by RecordKeyPress/RecordKeyRelease (accumulates across frame)
    // curr:    snapshot taken at last UpdateState()
    // prev:    snapshot taken at the UpdateState() before that
    static bool s_pendingKeys[kMaxKeys];
    static bool s_currKeys[kMaxKeys];
    static bool s_prevKeys[kMaxKeys];

    // --- Modifier key flags (separate triple-buffer - Qt codes exceed kMaxKeys) ---
    static bool s_pendingShift;
    static bool s_pendingCtrl;
    static bool s_pendingAlt;
    static bool s_currShift;
    static bool s_currCtrl;
    static bool s_currAlt;
    static bool s_prevShift;
    static bool s_prevCtrl;
    static bool s_prevAlt;

    // --- Mouse button state buffers ---
    static bool s_pendingMouseButtons[kMaxMouseButtons];
    static bool s_currMouseButtons[kMaxMouseButtons];
    static bool s_prevMouseButtons[kMaxMouseButtons];

    // --- Mouse position ---
    static float s_pendingMouseX;
    static float s_pendingMouseY;
    static float s_currMouseX;
    static float s_currMouseY;

    // --- Mouse delta (computed by UpdateState) ---
    static float s_deltaMouseX;
    static float s_deltaMouseY;

    // --- Scroll ---
    static float s_pendingScroll;
    static float s_currScroll;

    // --- Active camera for event generation ---
    static Camera* s_activeCamera;
};

} // namespace neurus