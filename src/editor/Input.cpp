/**
 * @file Input.cpp
 * @brief Implementation of Input static translation helpers.
 */

#include "editor/Input.h"

namespace neurus {

// ---------------------------------------------------------------------------
// Translation helpers
// ---------------------------------------------------------------------------

glm::vec2 Input::GetMousePos(const QPointF& pos)
{
	return glm::vec2(static_cast<float>(pos.x()), static_cast<float>(pos.y()));
}

Input::Modifiers Input::GetModifiers(const Qt::KeyboardModifiers mods)
{
	int mask = 0;
	if (mods & Qt::ShiftModifier)   mask |= Mod_Shift;
	if (mods & Qt::ControlModifier) mask |= Mod_Ctrl;
	if (mods & Qt::AltModifier)     mask |= Mod_Alt;
	return static_cast<Modifiers>(mask);
}

Input::MouseButton Input::GetMouseButton(const Qt::MouseButton btn)
{
	switch (btn)
	{
	case Qt::LeftButton:   return MouseButton::Left;
	case Qt::RightButton:  return MouseButton::Right;
	case Qt::MiddleButton: return MouseButton::Middle;
	default:               return MouseButton::Left;
	}
}

} // namespace neurus
