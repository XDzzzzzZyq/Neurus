/**
 * @file Input.h
 * @brief Pure static translation helpers for converting Qt input types to engine types.
 *
 * Architecture:
 * - Stateless static utility — no member variables, no QObject, no state machine.
 * - Three translation methods: Qt mouse position → glm::vec2, Qt modifiers → bitmask,
 *   Qt mouse button → Input::MouseButton.
 * - No more polling loop, no triple-buffer, no key state queries.
 *
 * @note Editor Layer: Input is part of the Editor system, not UI or Renderer.
 * @note Not thread-safe — must be used from the main (Qt) thread only.
 */
#pragma once

#include <glm/glm.hpp>
#include <QObject>
#include <QPoint>

namespace neurus {

/**
 * @brief Stateless static helper for translating Qt input primitives.
 *
 * The old Input class was a global-state polling system (triple-buffer key/mouse
 * arrays, per-frame UpdateState(), InputState struct). It has been stripped down
 * to three pure translation methods that convert Qt event data into engine types.
 *
 * The Viewport class emits Qt signals for input events; controllers and Editor
 * react directly to those signals. Input no longer owns any state.
 */
class Input
{
public:
	// -----------------------------------------------------------------------
	// Mouse button identifiers
	// -----------------------------------------------------------------------

	/** @brief Mouse button index for use with Qt mouse events. */
	enum MouseButton
	{
		Left = 0,   ///< Left mouse button
		Right = 1,  ///< Right mouse button
		Middle = 2  ///< Middle mouse button
	};

	// -----------------------------------------------------------------------
	// Translation helpers
	// -----------------------------------------------------------------------

	/**
	 * @brief Converts a Qt QPointF mouse position to a glm::vec2.
	 * @param pos Widget-local cursor position from a Qt mouse event.
	 * @return glm::vec2 with x = pos.x(), y = pos.y().
	 */
	static glm::vec2 GetMousePos(const QPointF& pos);

	// -----------------------------------------------------------------------
	// Modifier key flags (combinable bitmask)
	// -----------------------------------------------------------------------

	/** @brief Modifier key flags used in event structs and camera mode selection. */
	enum Modifiers
	{
		Mod_None  = 0x0,  ///< No modifier keys active.
		Mod_Shift = 0x1,  ///< Shift key(s) held.
		Mod_Ctrl  = 0x2,  ///< Control key(s) held.
		Mod_Alt   = 0x4   ///< Alt key(s) held.
	};

	/**
	 * @brief Translates Qt keyboard modifiers into a Modifiers bitmask.
	 * @param mods Qt::KeyboardModifiers from a mouse or key event.
	 * @return Bitmask of active modifier keys.
	 */
	static Modifiers GetModifiers(Qt::KeyboardModifiers mods);

	/**
	 * @brief Maps a Qt::MouseButton enum to Input::MouseButton.
	 * @param btn Qt mouse button identifier from a mouse event.
	 * @return Corresponding Input::MouseButton. Falls back to Left for
	 *         unrecognised buttons.
	 */
	static MouseButton GetMouseButton(Qt::MouseButton btn);
};

} // namespace neurus
