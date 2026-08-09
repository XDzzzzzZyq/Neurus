#pragma once

#include <glm/glm.hpp>
#include "editor/Input.h"

namespace neurus {

/**
 * @brief Mouse movement event emitted every time the cursor moves over the viewport.
 */
struct MouseMoveEvent
{
	glm::vec2        position;   ///< Current cursor position in widget-local coords.
	glm::vec2        delta;      ///< Cursor delta since last position.
	Input::Modifiers modifiers;  ///< Bitmask of active modifier keys.
	bool             leftHeld;   ///< Left mouse button is currently held.
	bool             middleHeld; ///< Middle mouse button is currently held.
	bool             rightHeld;  ///< Right mouse button is currently held.
};

/** @brief Mouse button press event. */
struct MousePressEvent
{
	Input::MouseButton button;    ///< Which button was pressed.
	glm::vec2          position;  ///< Cursor position at press time.
	Input::Modifiers   modifiers; ///< Bitmask of active modifier keys.
};

/** @brief Mouse button release event. */
struct MouseReleaseEvent
{
	Input::MouseButton button;    ///< Which button was released.
	glm::vec2          position;  ///< Cursor position at release time.
	Input::Modifiers   modifiers; ///< Bitmask of active modifier keys.
};

/** @brief Mouse scroll event emitted on each wheel notch. */
struct MouseScrollEvent
{
	float             delta;     ///< Scroll delta (~+1 per notch up, ~-1 per notch down).
	glm::vec2         position;  ///< Cursor position at scroll time.
	Input::Modifiers  modifiers; ///< Bitmask of active modifier keys.
	bool              leftHeld;  ///< Left mouse button is currently held.
	bool              middleHeld;///< Middle mouse button is currently held.
	bool              rightHeld; ///< Right mouse button is currently held.
};

/**
 * @brief Pure UI->Editor intent: user clicked a scene-object row in the Outliner.
 *
 * Carries the object's integer UID only. The Editor wraps the intent and
 * forwards the dedicated ObjectSelected event to controllers.
 */
struct ObjectClicked
{
	int objectUid = 0; ///< Clicked object UID (0 = none).
	int modifiers = 0; ///< Input::Modifiers bitmask.
};

/** @brief Pure UI->Editor intent: user pressed Delete (Outliner or Viewport). */
struct DeleteRequested
{
};

} // namespace neurus
