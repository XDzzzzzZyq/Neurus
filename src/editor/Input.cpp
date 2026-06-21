/**
 * @file Input.cpp
 * @brief Implementation of the static Input capture system with triple-buffer
 *        pipeline (pending → curr → prev) for reliable transition detection.
 */

#include "editor/Input.h"

#include <cstring>

namespace neurus {

// ---------------------------------------------------------------------------
// Static storage definitions
// ---------------------------------------------------------------------------

// --- Key state ---
bool Input::s_pendingKeys[Input::kMaxKeys] = {};
bool Input::s_currKeys[Input::kMaxKeys]    = {};
bool Input::s_prevKeys[Input::kMaxKeys]    = {};

// --- Mouse button state ---
bool Input::s_pendingMouseButtons[Input::kMaxMouseButtons] = {};
bool Input::s_currMouseButtons[Input::kMaxMouseButtons]    = {};
bool Input::s_prevMouseButtons[Input::kMaxMouseButtons]    = {};

// --- Mouse position ---
float Input::s_pendingMouseX = 0.0f;
float Input::s_pendingMouseY = 0.0f;
float Input::s_currMouseX    = 0.0f;
float Input::s_currMouseY    = 0.0f;

// --- Mouse delta ---
float Input::s_deltaMouseX = 0.0f;
float Input::s_deltaMouseY = 0.0f;

// --- Modifier key flags ---
bool Input::s_pendingShift = false;
bool Input::s_pendingCtrl  = false;
bool Input::s_pendingAlt   = false;
bool Input::s_currShift    = false;
bool Input::s_currCtrl     = false;
bool Input::s_currAlt      = false;
bool Input::s_prevShift    = false;
bool Input::s_prevCtrl     = false;
bool Input::s_prevAlt      = false;

// --- Scroll ---
float Input::s_pendingScroll = 0.0f;
float Input::s_currScroll    = 0.0f;

// --- Active camera ---
Camera* Input::s_activeCamera = nullptr;

// ---------------------------------------------------------------------------
// Recording - write to pending buffer
// ---------------------------------------------------------------------------

void Input::RecordKeyPress(const int qtKey)
{
	// Modifier keys - their Qt codes exceed kMaxKeys, handle via dedicated flags
	if (qtKey == 0x01000020) { s_pendingShift = true;  return; } // Qt::Key_Shift
	if (qtKey == 0x01000021) { s_pendingCtrl  = true;  return; } // Qt::Key_Control
	if (qtKey == 0x01000023) { s_pendingAlt   = true;  return; } // Qt::Key_Alt

	if (qtKey < 0 || qtKey >= kMaxKeys) return;
	s_pendingKeys[qtKey] = true;
}

void Input::RecordKeyRelease(const int qtKey)
{
	// Modifier keys - dedicated flags
	if (qtKey == 0x01000020) { s_pendingShift = false; return; } // Qt::Key_Shift
	if (qtKey == 0x01000021) { s_pendingCtrl  = false; return; } // Qt::Key_Control
	if (qtKey == 0x01000023) { s_pendingAlt   = false; return; } // Qt::Key_Alt

	if (qtKey < 0 || qtKey >= kMaxKeys) return;
	s_pendingKeys[qtKey] = false;
}

void Input::RecordMouseMove(const float x, const float y)
{
	s_pendingMouseX = x;
	s_pendingMouseY = y;
}

void Input::RecordMousePress(const MouseButton button)
{
	const int idx = static_cast<int>(button);
	if (idx < 0 || idx >= kMaxMouseButtons) return;
	s_pendingMouseButtons[idx] = true;
}

void Input::RecordMouseRelease(const MouseButton button)
{
	const int idx = static_cast<int>(button);
	if (idx < 0 || idx >= kMaxMouseButtons) return;
	s_pendingMouseButtons[idx] = false;
}

void Input::RecordScroll(const float delta)
{
	s_pendingScroll += delta;
}

// ---------------------------------------------------------------------------
// Frame update - swaps buffers and computes deltas
// ---------------------------------------------------------------------------

void Input::UpdateState()
{
	// --- Shift curr → prev (for transition detection) ---
	std::memcpy(s_prevKeys, s_currKeys, sizeof(s_prevKeys));
	std::memcpy(s_prevMouseButtons, s_currMouseButtons, sizeof(s_prevMouseButtons));
	s_prevShift = s_currShift;
	s_prevCtrl  = s_currCtrl;
	s_prevAlt   = s_currAlt;

	// --- Save old mouse position for delta computation (before overwriting curr) ---
	const float oldMouseX = s_currMouseX;
	const float oldMouseY = s_currMouseY;

	// --- Shift pending → curr ---
	std::memcpy(s_currKeys, s_pendingKeys, sizeof(s_currKeys));
	std::memcpy(s_currMouseButtons, s_pendingMouseButtons, sizeof(s_currMouseButtons));
	s_currMouseX = s_pendingMouseX;
	s_currMouseY = s_pendingMouseY;
	s_currScroll = s_pendingScroll;
	s_currShift = s_pendingShift;
	s_currCtrl  = s_pendingCtrl;
	s_currAlt   = s_pendingAlt;

	// --- Compute mouse delta ---
	s_deltaMouseX = s_currMouseX - oldMouseX;
	s_deltaMouseY = s_currMouseY - oldMouseY;

	// --- Reset scroll accumulator in pending (keys/buttons persist) ---
	s_pendingScroll = 0.0f;
}

// ---------------------------------------------------------------------------
// Keyboard queries - read from curr snapshot
// ---------------------------------------------------------------------------

bool Input::IsKeyPressed(const int qtKey)
{
	if (qtKey < 0 || qtKey >= kMaxKeys) return false;
	return s_currKeys[qtKey];
}

bool Input::IsKeyClicked(const int qtKey)
{
	if (qtKey < 0 || qtKey >= kMaxKeys) return false;
	return s_currKeys[qtKey] && !s_prevKeys[qtKey];
}

bool Input::IsKeyReleased(const int qtKey)
{
	if (qtKey < 0 || qtKey >= kMaxKeys) return false;
	return !s_currKeys[qtKey] && s_prevKeys[qtKey];
}

bool Input::IsShiftHeld()
{
	return s_currShift;
}

bool Input::IsCtrlHeld()
{
	return s_currCtrl;
}

bool Input::IsAltHeld()
{
	return s_currAlt;
}

// ---------------------------------------------------------------------------
// Mouse queries - read from curr snapshot
// ---------------------------------------------------------------------------

float Input::GetMouseX()       { return s_currMouseX; }
float Input::GetMouseY()       { return s_currMouseY; }
float Input::GetDeltaMouseX()  { return s_deltaMouseX; }
float Input::GetDeltaMouseY()  { return s_deltaMouseY; }
float Input::GetScrollDelta()  { return s_currScroll; }

bool Input::IsMouseButtonPressed(const MouseButton button)
{
	const int idx = static_cast<int>(button);
	if (idx < 0 || idx >= kMaxMouseButtons) return false;
	return s_currMouseButtons[idx];
}

bool Input::IsMouseButtonClicked(const MouseButton button)
{
	const int idx = static_cast<int>(button);
	if (idx < 0 || idx >= kMaxMouseButtons) return false;
	return s_currMouseButtons[idx] && !s_prevMouseButtons[idx];
}

bool Input::IsMouseButtonReleased(const MouseButton button)
{
	const int idx = static_cast<int>(button);
	if (idx < 0 || idx >= kMaxMouseButtons) return false;
	return !s_currMouseButtons[idx] && s_prevMouseButtons[idx];
}

// ---------------------------------------------------------------------------
// Convenience - CameraController InputState
// ---------------------------------------------------------------------------

InputState Input::GetInputState()
{
	InputState state;
	state.mouseDeltaX    = s_deltaMouseX;
	state.mouseDeltaY    = s_deltaMouseY;
	state.scrollDelta    = s_currScroll;
	state.rightMouseHeld  = s_currMouseButtons[static_cast<int>(MouseButton::Right)];
	state.middleMouseHeld = s_currMouseButtons[static_cast<int>(MouseButton::Middle)];
	state.shiftHeld       = IsShiftHeld();
	state.ctrlHeld        = IsCtrlHeld();
	state.leftMouseHeld   = s_currMouseButtons[static_cast<int>(MouseButton::Left)];
	state.altHeld         = IsAltHeld();
	return state;
}

} // namespace neurus
