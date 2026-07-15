/**
 * @file test_input.cpp
 * @brief Unit tests for the Input static translation helpers.
 *
 * Verifies that the three translation methods (GetMousePos, GetModifiers,
 * GetMouseButton) correctly convert Qt input types to engine types.
 * All tests are pure CPU — no GPU or Qt event loop required.
 */

#include <gtest/gtest.h>

#include "editor/Input.h"

using namespace neurus;

// ===========================================================================
// GetMousePos — Qt QPointF → glm::vec2
// ===========================================================================

TEST(InputTest, GetMousePos_ConvertsCorrectly)
{
	const QPointF pos(100.5f, 200.25f);
	const glm::vec2 v = Input::GetMousePos(pos);

	EXPECT_FLOAT_EQ(v.x, 100.5f);
	EXPECT_FLOAT_EQ(v.y, 200.25f);
}

TEST(InputTest, GetMousePos_OriginReturnsZero)
{
	const glm::vec2 v = Input::GetMousePos(QPointF(0.0f, 0.0f));

	EXPECT_FLOAT_EQ(v.x, 0.0f);
	EXPECT_FLOAT_EQ(v.y, 0.0f);
}

TEST(InputTest, GetMousePos_NegativeValues)
{
	const glm::vec2 v = Input::GetMousePos(QPointF(-50.0f, -75.0f));

	EXPECT_FLOAT_EQ(v.x, -50.0f);
	EXPECT_FLOAT_EQ(v.y, -75.0f);
}

// ===========================================================================
// GetModifiers — Qt::KeyboardModifiers → Input::Modifiers bitmask
// ===========================================================================

TEST(InputTest, GetModifiers_NoModifiersReturnsNone)
{
	const auto mods = Input::GetModifiers(Qt::NoModifier);
	EXPECT_EQ(mods, Input::Mod_None);
}

TEST(InputTest, GetModifiers_ShiftReturnsShiftFlag)
{
	const auto mods = Input::GetModifiers(Qt::ShiftModifier);
	EXPECT_EQ(mods, Input::Mod_Shift);
}

TEST(InputTest, GetModifiers_CtrlReturnsCtrlFlag)
{
	const auto mods = Input::GetModifiers(Qt::ControlModifier);
	EXPECT_EQ(mods, Input::Mod_Ctrl);
}

TEST(InputTest, GetModifiers_AltReturnsAltFlag)
{
	const auto mods = Input::GetModifiers(Qt::AltModifier);
	EXPECT_EQ(mods, Input::Mod_Alt);
}

TEST(InputTest, GetModifiers_CombinedModifiers)
{
	const auto mods = Input::GetModifiers(Qt::ShiftModifier | Qt::ControlModifier);
	EXPECT_EQ(static_cast<int>(mods), Input::Mod_Shift | Input::Mod_Ctrl);
	EXPECT_TRUE(mods & Input::Mod_Shift);
	EXPECT_TRUE(mods & Input::Mod_Ctrl);
	EXPECT_FALSE(mods & Input::Mod_Alt);
}

TEST(InputTest, GetModifiers_AllModifiers)
{
	const auto mods = Input::GetModifiers(Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
	EXPECT_EQ(static_cast<int>(mods), Input::Mod_Shift | Input::Mod_Ctrl | Input::Mod_Alt);
	EXPECT_TRUE(mods & Input::Mod_Shift);
	EXPECT_TRUE(mods & Input::Mod_Ctrl);
	EXPECT_TRUE(mods & Input::Mod_Alt);
}

// ===========================================================================
// GetMouseButton — Qt::MouseButton → Input::MouseButton
// ===========================================================================

TEST(InputTest, GetMouseButton_LeftReturnsLeft)
{
	EXPECT_EQ(Input::GetMouseButton(Qt::LeftButton), Input::MouseButton::Left);
}

TEST(InputTest, GetMouseButton_RightReturnsRight)
{
	EXPECT_EQ(Input::GetMouseButton(Qt::RightButton), Input::MouseButton::Right);
}

TEST(InputTest, GetMouseButton_MiddleReturnsMiddle)
{
	EXPECT_EQ(Input::GetMouseButton(Qt::MiddleButton), Input::MouseButton::Middle);
}
