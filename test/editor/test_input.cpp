/**
 * @file test_input.cpp
 * @brief Unit tests for the Input static capture system.
 *
 * Verifies the pending→curr→prev triple-buffer pipeline correctly detects
 * key/button transitions (pressed, clicked, released), mouse position/delta,
 * scroll, modifier keys, and InputState population for CameraController.
 * All tests are pure CPU — no GPU or Qt event loop required.
 */

#include <gtest/gtest.h>

#include <QKeyEvent>   // Qt::Key constants

#include "editor/Input.h"

using namespace neurus;

// ---------------------------------------------------------------------------
// Helpers: wrap Qt::Key values for clarity in tests
// ---------------------------------------------------------------------------
namespace {
	constexpr int kKeyA      = 0x41;         // Qt::Key_A
	constexpr int kKeyW      = 0x57;         // Qt::Key_W
	constexpr int kKeyShift  = 0x01000020;   // Qt::Key_Shift
	constexpr int kKeyCtrl   = 0x01000021;   // Qt::Key_Control
	constexpr int kKeyAlt    = 0x01000023;   // Qt::Key_Alt
	constexpr int kKeyEscape = 0x01000000;   // Qt::Key_Escape
}

// ---------------------------------------------------------------------------
// Test Fixture — resets Input state before every test
// ---------------------------------------------------------------------------

class InputTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		// Reset by simulating: press nothing, update twice to flush
		// any residual state from previous tests
		Input::UpdateState();
		Input::UpdateState();
	}

	void TearDown() override
	{
		// TearDown is a no-op; the next SetUp will reset.
	}
};

// ===========================================================================
// Keyboard — Pressed
// ===========================================================================

TEST_F(InputTest, IsKeyPressed_FalseBeforeAnyEvent)
{
	EXPECT_FALSE(Input::IsKeyPressed(kKeyA));
}

TEST_F(InputTest, IsKeyPressed_TrueAfterRecordAndUpdate)
{
	Input::RecordKeyPress(kKeyA);
	Input::UpdateState();

	EXPECT_TRUE(Input::IsKeyPressed(kKeyA));
}

TEST_F(InputTest, IsKeyPressed_PersistsAcrossFrames)
{
	Input::RecordKeyPress(kKeyA);
	Input::UpdateState();
	EXPECT_TRUE(Input::IsKeyPressed(kKeyA));

	// Next frame — key still held (no release event)
	Input::UpdateState();
	EXPECT_TRUE(Input::IsKeyPressed(kKeyA));
}

TEST_F(InputTest, IsKeyPressed_FalseAfterRelease)
{
	Input::RecordKeyPress(kKeyA);
	Input::UpdateState();

	Input::RecordKeyRelease(kKeyA);
	Input::UpdateState();

	EXPECT_FALSE(Input::IsKeyPressed(kKeyA));
}

// ===========================================================================
// Keyboard — Clicked (up→down transition, one frame only)
// ===========================================================================

TEST_F(InputTest, IsKeyClicked_TrueOnTransitionFrame)
{
	Input::RecordKeyPress(kKeyA);
	Input::UpdateState();

	EXPECT_TRUE(Input::IsKeyClicked(kKeyA));
}

TEST_F(InputTest, IsKeyClicked_FalseAfterFirstFrame)
{
	Input::RecordKeyPress(kKeyA);
	Input::UpdateState();
	// Click consumed

	Input::UpdateState(); // Second frame — no new transition
	EXPECT_FALSE(Input::IsKeyClicked(kKeyA));
}

TEST_F(InputTest, IsKeyClicked_FalseWhileHeld)
{
	Input::RecordKeyPress(kKeyA);
	Input::UpdateState();
	EXPECT_TRUE(Input::IsKeyClicked(kKeyA));

	Input::UpdateState(); // Still held
	EXPECT_FALSE(Input::IsKeyClicked(kKeyA));

	Input::UpdateState(); // Still held
	EXPECT_FALSE(Input::IsKeyClicked(kKeyA));
}

TEST_F(InputTest, IsKeyClicked_FalseWithoutTransition)
{
	Input::UpdateState();
	EXPECT_FALSE(Input::IsKeyClicked(kKeyA));
}

// ===========================================================================
// Keyboard — Released (down→up transition, one frame only)
// ===========================================================================

TEST_F(InputTest, IsKeyReleased_TrueOnTransitionFrame)
{
	Input::RecordKeyPress(kKeyA);
	Input::UpdateState(); // now pressed

	Input::RecordKeyRelease(kKeyA);
	Input::UpdateState(); // now released

	EXPECT_TRUE(Input::IsKeyReleased(kKeyA));
}

TEST_F(InputTest, IsKeyReleased_FalseAfterFirstFrame)
{
	Input::RecordKeyPress(kKeyA);
	Input::UpdateState();

	Input::RecordKeyRelease(kKeyA);
	Input::UpdateState(); // release consumed here
	EXPECT_TRUE(Input::IsKeyReleased(kKeyA));

	Input::UpdateState(); // next frame — no new transition
	EXPECT_FALSE(Input::IsKeyReleased(kKeyA));
}

TEST_F(InputTest, IsKeyReleased_FalseWithoutPriorPress)
{
	// Release without ever pressing
	Input::RecordKeyRelease(kKeyA);
	Input::UpdateState();

	EXPECT_FALSE(Input::IsKeyReleased(kKeyA));
	EXPECT_FALSE(Input::IsKeyPressed(kKeyA));
}

TEST_F(InputTest, IsKeyReleased_FalseWhileStillPressed)
{
	Input::RecordKeyPress(kKeyA);
	Input::UpdateState();

	EXPECT_FALSE(Input::IsKeyReleased(kKeyA)); // not released, still held
	EXPECT_TRUE(Input::IsKeyPressed(kKeyA));
}

// ===========================================================================
// Keyboard — Multiple keys simultaneously
// ===========================================================================

TEST_F(InputTest, MultipleKeysIndependentState)
{
	Input::RecordKeyPress(kKeyA);
	Input::RecordKeyPress(kKeyW);
	Input::UpdateState();

	EXPECT_TRUE(Input::IsKeyPressed(kKeyA));
	EXPECT_TRUE(Input::IsKeyPressed(kKeyW));
	EXPECT_TRUE(Input::IsKeyClicked(kKeyA));
	EXPECT_TRUE(Input::IsKeyClicked(kKeyW));
}

TEST_F(InputTest, MultipleKeysIndependentRelease)
{
	Input::RecordKeyPress(kKeyA);
	Input::RecordKeyPress(kKeyW);
	Input::UpdateState();

	// Release only A
	Input::RecordKeyRelease(kKeyA);
	Input::UpdateState();

	EXPECT_FALSE(Input::IsKeyPressed(kKeyA));
	EXPECT_TRUE(Input::IsKeyReleased(kKeyA));
	EXPECT_TRUE(Input::IsKeyPressed(kKeyW));   // W still held
	EXPECT_FALSE(Input::IsKeyReleased(kKeyW)); // W not released
}

// ===========================================================================
// Modifier keys — Shift, Ctrl, Alt
// ===========================================================================

TEST_F(InputTest, ShiftModifierDetected)
{
	Input::RecordKeyPress(kKeyShift);
	Input::UpdateState();

	EXPECT_TRUE(Input::IsShiftHeld());
	EXPECT_FALSE(Input::IsCtrlHeld());
	EXPECT_FALSE(Input::IsAltHeld());
}

TEST_F(InputTest, CtrlModifierDetected)
{
	Input::RecordKeyPress(kKeyCtrl);
	Input::UpdateState();

	EXPECT_TRUE(Input::IsCtrlHeld());
	EXPECT_FALSE(Input::IsShiftHeld());
	EXPECT_FALSE(Input::IsAltHeld());
}

TEST_F(InputTest, AltModifierDetected)
{
	Input::RecordKeyPress(kKeyAlt);
	Input::UpdateState();

	EXPECT_TRUE(Input::IsAltHeld());
	EXPECT_FALSE(Input::IsShiftHeld());
	EXPECT_FALSE(Input::IsCtrlHeld());
}

TEST_F(InputTest, AllModifiersDetected)
{
	Input::RecordKeyPress(kKeyShift);
	Input::RecordKeyPress(kKeyCtrl);
	Input::RecordKeyPress(kKeyAlt);
	Input::UpdateState();

	EXPECT_TRUE(Input::IsShiftHeld());
	EXPECT_TRUE(Input::IsCtrlHeld());
	EXPECT_TRUE(Input::IsAltHeld());
}

TEST_F(InputTest, ModifiersClearedOnRelease)
{
	Input::RecordKeyPress(kKeyShift);
	Input::UpdateState();
	EXPECT_TRUE(Input::IsShiftHeld());

	Input::RecordKeyRelease(kKeyShift);
	Input::UpdateState();
	EXPECT_FALSE(Input::IsShiftHeld());
}

// ===========================================================================
// Mouse — Position / Delta
// ===========================================================================

TEST_F(InputTest, MousePositionDefaultsToZero)
{
	EXPECT_FLOAT_EQ(Input::GetMouseX(), 0.0f);
	EXPECT_FLOAT_EQ(Input::GetMouseY(), 0.0f);
}

TEST_F(InputTest, MousePositionUpdatedAfterRecordAndUpdate)
{
	Input::RecordMouseMove(100.0f, 200.0f);
	Input::UpdateState();

	EXPECT_FLOAT_EQ(Input::GetMouseX(), 100.0f);
	EXPECT_FLOAT_EQ(Input::GetMouseY(), 200.0f);
}

TEST_F(InputTest, MouseDeltaZeroWhenNoMovement)
{
	Input::RecordMouseMove(50.0f, 50.0f);
	Input::UpdateState(); // curr=50, prev=0, delta=50
	EXPECT_FLOAT_EQ(Input::GetDeltaMouseX(), 50.0f);

	Input::UpdateState(); // no new movement — pending still at 50, curr=50, prev=50
	EXPECT_FLOAT_EQ(Input::GetDeltaMouseX(), 0.0f);
	EXPECT_FLOAT_EQ(Input::GetDeltaMouseY(), 0.0f);
}

TEST_F(InputTest, MouseDeltaComputesCorrectly)
{
	Input::RecordMouseMove(10.0f, 20.0f);
	Input::UpdateState();
	EXPECT_FLOAT_EQ(Input::GetDeltaMouseX(), 10.0f);
	EXPECT_FLOAT_EQ(Input::GetDeltaMouseY(), 20.0f);

	Input::RecordMouseMove(30.0f, 50.0f);
	Input::UpdateState();
	EXPECT_FLOAT_EQ(Input::GetDeltaMouseX(), 20.0f); // 30 - 10
	EXPECT_FLOAT_EQ(Input::GetDeltaMouseY(), 30.0f); // 50 - 20
}

TEST_F(InputTest, MouseDeltaNegativeValues)
{
	Input::RecordMouseMove(100.0f, 100.0f);
	Input::UpdateState();

	Input::RecordMouseMove(80.0f, 60.0f);
	Input::UpdateState();

	EXPECT_FLOAT_EQ(Input::GetDeltaMouseX(), -20.0f);
	EXPECT_FLOAT_EQ(Input::GetDeltaMouseY(), -40.0f);
}

// ===========================================================================
// Mouse Buttons
// ===========================================================================

TEST_F(InputTest, MouseButtonPressed_FalseByDefault)
{
	EXPECT_FALSE(Input::IsMouseButtonPressed(Input::MouseButton::Left));
	EXPECT_FALSE(Input::IsMouseButtonPressed(Input::MouseButton::Right));
	EXPECT_FALSE(Input::IsMouseButtonPressed(Input::MouseButton::Middle));
}

TEST_F(InputTest, MouseButtonPressed_TrueAfterRecord)
{
	Input::RecordMousePress(Input::MouseButton::Left);
	Input::UpdateState();

	EXPECT_TRUE(Input::IsMouseButtonPressed(Input::MouseButton::Left));
}

TEST_F(InputTest, MouseButtonClicked_OneFrameOnly)
{
	Input::RecordMousePress(Input::MouseButton::Right);
	Input::UpdateState();
	EXPECT_TRUE(Input::IsMouseButtonClicked(Input::MouseButton::Right));

	Input::UpdateState(); // Still held, no new transition
	EXPECT_FALSE(Input::IsMouseButtonClicked(Input::MouseButton::Right));
}

TEST_F(InputTest, MouseButtonReleased_OneFrameOnly)
{
	Input::RecordMousePress(Input::MouseButton::Middle);
	Input::UpdateState();

	Input::RecordMouseRelease(Input::MouseButton::Middle);
	Input::UpdateState();
	EXPECT_TRUE(Input::IsMouseButtonReleased(Input::MouseButton::Middle));

	Input::UpdateState(); // No new transition
	EXPECT_FALSE(Input::IsMouseButtonReleased(Input::MouseButton::Middle));
}

TEST_F(InputTest, MouseButtonsAreIndependent)
{
	Input::RecordMousePress(Input::MouseButton::Left);
	Input::RecordMousePress(Input::MouseButton::Right);
	Input::UpdateState();

	EXPECT_TRUE(Input::IsMouseButtonPressed(Input::MouseButton::Left));
	EXPECT_TRUE(Input::IsMouseButtonPressed(Input::MouseButton::Right));
	EXPECT_FALSE(Input::IsMouseButtonPressed(Input::MouseButton::Middle));

	Input::RecordMouseRelease(Input::MouseButton::Left);
	Input::UpdateState();

	EXPECT_FALSE(Input::IsMouseButtonPressed(Input::MouseButton::Left));
	EXPECT_TRUE(Input::IsMouseButtonPressed(Input::MouseButton::Right)); // Still held
}

// ===========================================================================
// Scroll
// ===========================================================================

TEST_F(InputTest, ScrollDeltaDefaultZero)
{
	EXPECT_FLOAT_EQ(Input::GetScrollDelta(), 0.0f);
}

TEST_F(InputTest, ScrollDeltaAccumulatesInFrame)
{
	Input::RecordScroll(1.0f);
	Input::RecordScroll(2.0f);
	Input::UpdateState();

	EXPECT_FLOAT_EQ(Input::GetScrollDelta(), 3.0f);
}

TEST_F(InputTest, ScrollDeltaResetsEachFrame)
{
	Input::RecordScroll(5.0f);
	Input::UpdateState();
	EXPECT_FLOAT_EQ(Input::GetScrollDelta(), 5.0f);

	// Next frame — no scroll events
	Input::UpdateState();
	EXPECT_FLOAT_EQ(Input::GetScrollDelta(), 0.0f);
}

TEST_F(InputTest, ScrollDeltaNegative)
{
	Input::RecordScroll(-3.0f);
	Input::UpdateState();
	EXPECT_FLOAT_EQ(Input::GetScrollDelta(), -3.0f);
}

// ===========================================================================
// GetInputState — CameraController integration
// ===========================================================================

TEST_F(InputTest, GetInputState_DefaultAllZeroOrFalse)
{
	const InputState state = Input::GetInputState();

	EXPECT_FLOAT_EQ(state.mouseDeltaX, 0.0f);
	EXPECT_FLOAT_EQ(state.mouseDeltaY, 0.0f);
	EXPECT_FLOAT_EQ(state.scrollDelta, 0.0f);
	EXPECT_FALSE(state.rightMouseHeld);
	EXPECT_FALSE(state.middleMouseHeld);
	EXPECT_FALSE(state.shiftHeld);
	EXPECT_FALSE(state.ctrlHeld);
}

TEST_F(InputTest, GetInputState_PopulatedFromCurrentSnapshot)
{
	Input::RecordMouseMove(42.0f, 99.0f);
	Input::RecordMousePress(Input::MouseButton::Right);
	Input::RecordKeyPress(kKeyShift);
	Input::UpdateState();

	// Simulate movement for non-zero delta + record scroll in the same frame
	Input::RecordMouseMove(50.0f, 100.0f);
	Input::RecordScroll(1.5f);
	Input::UpdateState();

	const InputState state = Input::GetInputState();

	EXPECT_FLOAT_EQ(state.mouseDeltaX, 8.0f);    // 50 - 42
	EXPECT_FLOAT_EQ(state.mouseDeltaY, 1.0f);    // 100 - 99
	EXPECT_FLOAT_EQ(state.scrollDelta, 1.5f);
	EXPECT_TRUE(state.rightMouseHeld);
	EXPECT_FALSE(state.middleMouseHeld);
	EXPECT_TRUE(state.shiftHeld);
	EXPECT_FALSE(state.ctrlHeld);
}

TEST_F(InputTest, GetInputState_AfterReleaseReturnsFalse)
{
	Input::RecordMousePress(Input::MouseButton::Right);
	Input::RecordKeyPress(kKeyCtrl);
	Input::UpdateState();

	// Release everything
	Input::RecordMouseRelease(Input::MouseButton::Right);
	Input::RecordKeyRelease(kKeyCtrl);
	Input::UpdateState();

	const InputState state = Input::GetInputState();

	EXPECT_FALSE(state.rightMouseHeld);
	EXPECT_FALSE(state.ctrlHeld);
}

// ===========================================================================
// Edge Cases
// ===========================================================================

TEST_F(InputTest, OutOfRangeKeyCodeIsSafe)
{
	// Should not crash or assert
	Input::RecordKeyPress(300);
	Input::RecordKeyPress(-1);
	Input::UpdateState();

	EXPECT_FALSE(Input::IsKeyPressed(300));
	EXPECT_FALSE(Input::IsKeyPressed(-1));
	EXPECT_FALSE(Input::IsKeyClicked(999));
	EXPECT_FALSE(Input::IsKeyReleased(-5));
}

TEST_F(InputTest, OutOfRangeMouseButtonIsSafe)
{
	const auto bad = static_cast<Input::MouseButton>(99);
	Input::RecordMousePress(bad);
	Input::RecordMouseRelease(bad);
	Input::UpdateState();

	EXPECT_FALSE(Input::IsMouseButtonPressed(bad));
	EXPECT_FALSE(Input::IsMouseButtonClicked(bad));
}

TEST_F(InputTest, RapidPressReleaseSameFrame)
{
	// Press and release within same frame → pending ends up false
	Input::RecordKeyPress(kKeyEscape);
	Input::RecordKeyRelease(kKeyEscape);
	Input::UpdateState();

	EXPECT_FALSE(Input::IsKeyPressed(kKeyEscape));
	// IsKeyClicked is false because curr is false (release overwrote press in pending)
	EXPECT_FALSE(Input::IsKeyClicked(kKeyEscape));
	EXPECT_FALSE(Input::IsKeyReleased(kKeyEscape));
}

TEST_F(InputTest, StateIsStableAcrossManyFrames)
{
	Input::RecordKeyPress(kKeyW);
	Input::UpdateState();

	for (int i = 0; i < 10; ++i)
	{
		Input::UpdateState();
		EXPECT_TRUE(Input::IsKeyPressed(kKeyW));
		EXPECT_FALSE(Input::IsKeyClicked(kKeyW));
		EXPECT_FALSE(Input::IsKeyReleased(kKeyW));
	}

	Input::RecordKeyRelease(kKeyW);
	Input::UpdateState();
	EXPECT_FALSE(Input::IsKeyPressed(kKeyW));
	EXPECT_TRUE(Input::IsKeyReleased(kKeyW));

	for (int i = 0; i < 10; ++i)
	{
		Input::UpdateState();
		EXPECT_FALSE(Input::IsKeyPressed(kKeyW));
		EXPECT_FALSE(Input::IsKeyReleased(kKeyW));
	}
}
