/**
 * @file Input.cpp
 * @brief Implementation of Input static translation helpers.
 *
 * Qt types are unwrapped by callers — Input.cpp only deals with raw C++ types.
 * The internal conversions match Qt::KeyboardModifier / Qt::MouseButton values,
 * but no Qt headers are exposed through Input.h.
 */

#include "editor/Input.h"

#include <QObject>   // Qt::KeyboardModifier, Qt::MouseButton

namespace neurus {

// ---------------------------------------------------------------------------
// GetMousePos — float pair → glm::vec2
// ---------------------------------------------------------------------------

glm::vec2 Input::GetMousePos(float x, float y)
{
	return glm::vec2(x, y);
}

// ---------------------------------------------------------------------------
// GetModifiers — uint32_t → Modifiers bitmask
// ---------------------------------------------------------------------------

Input::Modifiers Input::GetModifiers(uint32_t qtMods)
{
	auto mods = static_cast<Qt::KeyboardModifiers>(qtMods);

	int result = Mod_None;
	if (mods & Qt::ShiftModifier)   result |= Mod_Shift;
	if (mods & Qt::ControlModifier) result |= Mod_Ctrl;
	if (mods & Qt::AltModifier)     result |= Mod_Alt;
	return static_cast<Modifiers>(result);
}

// ---------------------------------------------------------------------------
// GetMouseButton — uint32_t → MouseButton
// ---------------------------------------------------------------------------

Input::MouseButton Input::GetMouseButton(uint32_t qtBtn)
{
	switch (static_cast<Qt::MouseButton>(qtBtn))
	{
	case Qt::LeftButton:   return MouseButton::Left;
	case Qt::RightButton:  return MouseButton::Right;
	case Qt::MiddleButton: return MouseButton::Middle;
	default:               return MouseButton::Left;  // fallback
	}
}

} // namespace neurus
