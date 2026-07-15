/**
 * @file test_input.cpp
 * @brief Unit tests for the Input static translation helpers.
 *
 * Verifies GetMousePos, GetModifiers, and GetMouseButton convert raw values
 * to engine types correctly. Qt types are unwrapped by the test code;
 * Input.h itself has zero Qt dependencies.
 */

#include <gtest/gtest.h>

#include <QObject>   // Qt::KeyboardModifiers, Qt::MouseButton
#include <QPoint>    // QPointF

#include "editor/Input.h"

using namespace neurus;

// ===========================================================================
// GetMousePos — float pair → glm::vec2
// ===========================================================================

TEST(InputTest, GetMousePos_ConvertsCorrectly)
{
	const glm::vec2 v = Input::GetMousePos(100.5f, 200.25f);

	EXPECT_FLOAT_EQ(v.x, 100.5f);
	EXPECT_FLOAT_EQ(v.y, 200.25f);
}

TEST(InputTest, GetMousePos_OriginReturnsZero)
{
	const glm::vec2 v = Input::GetMousePos(0.0f, 0.0f);

	EXPECT_FLOAT_EQ(v.x, 0.0f);
	EXPECT_FLOAT_EQ(v.y, 0.0f);
}

TEST(InputTest, GetMousePos_NegativeValues)
{
	const glm::vec2 v = Input::GetMousePos(-50.0f, -75.0f);

	EXPECT_FLOAT_EQ(v.x, -50.0f);
	EXPECT_FLOAT_EQ(v.y, -75.0f);
}

// ===========================================================================
// GetModifiers — uint32_t → Input::Modifiers bitmask
// ===========================================================================

TEST(InputTest, GetModifiers_NoModifiersReturnsNone)
{
	const auto mods = Input::GetModifiers(static_cast<uint32_t>(Qt::NoModifier));
	EXPECT_EQ(mods, Input::Mod_None);
}

TEST(InputTest, GetModifiers_ShiftReturnsShiftFlag)
{
	const auto mods = Input::GetModifiers(static_cast<uint32_t>(Qt::ShiftModifier));
	EXPECT_EQ(mods, Input::Mod_Shift);
}

TEST(InputTest, GetModifiers_CtrlReturnsCtrlFlag)
{
	const auto mods = Input::GetModifiers(static_cast<uint32_t>(Qt::ControlModifier));
	EXPECT_EQ(mods, Input::Mod_Ctrl);
}

TEST(InputTest, GetModifiers_AltReturnsAltFlag)
{
	const auto mods = Input::GetModifiers(static_cast<uint32_t>(Qt::AltModifier));
	EXPECT_EQ(mods, Input::Mod_Alt);
}

TEST(InputTest, GetModifiers_CombinedModifiers)
{
	const auto mods = Input::GetModifiers(
		static_cast<uint32_t>(Qt::ShiftModifier | Qt::ControlModifier));
	EXPECT_EQ(static_cast<int>(mods), Input::Mod_Shift | Input::Mod_Ctrl);
	EXPECT_TRUE(mods & Input::Mod_Shift);
	EXPECT_TRUE(mods & Input::Mod_Ctrl);
	EXPECT_FALSE(mods & Input::Mod_Alt);
}

TEST(InputTest, GetModifiers_AllModifiers)
{
	const auto mods = Input::GetModifiers(
		static_cast<uint32_t>(Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier));
	EXPECT_EQ(static_cast<int>(mods), Input::Mod_Shift | Input::Mod_Ctrl | Input::Mod_Alt);
	EXPECT_TRUE(mods & Input::Mod_Shift);
	EXPECT_TRUE(mods & Input::Mod_Ctrl);
	EXPECT_TRUE(mods & Input::Mod_Alt);
}

// ===========================================================================
// GetMouseButton — uint32_t → Input::MouseButton
// ===========================================================================

TEST(InputTest, GetMouseButton_LeftReturnsLeft)
{
	EXPECT_EQ(Input::GetMouseButton(static_cast<uint32_t>(Qt::LeftButton)), Input::MouseButton::Left);
}

TEST(InputTest, GetMouseButton_RightReturnsRight)
{
	EXPECT_EQ(Input::GetMouseButton(static_cast<uint32_t>(Qt::RightButton)), Input::MouseButton::Right);
}

TEST(InputTest, GetMouseButton_MiddleReturnsMiddle)
{
	EXPECT_EQ(Input::GetMouseButton(static_cast<uint32_t>(Qt::MiddleButton)), Input::MouseButton::Middle);
}
