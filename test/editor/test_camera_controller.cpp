/**
 * @file test_camera_controller.cpp
 * @brief TDD unit tests for CameraController MMB navigation (orbit, dolly, pan, zoom).
 *
 * Tests are written in RED state — they verify the expected behavior of the
 * new MMB-based camera controller. The implementation will be completed in
 * Task 4 to make these tests GREEN.
 *
 * MMB Convention (from decisions.md):
 *   - MMB alone       = Orbit (rotate around target)
 *   - Ctrl + MMB      = Dolly (move forward/back along view direction)
 *   - Shift + MMB     = Pan (translate parallel to view plane)
 *   - Scroll wheel    = Zoom (toward/away from target)
 *
 * Sensitivity constants (from CameraController.h):
 *   - kOrbitSensitivity = 0.005f  (radians per pixel)
 *   - kPanSensitivity   = 0.01f   (world units per pixel)
 *   - kZoomSensitivity  = 1.0f    (factor per scroll notch)
 *   - kDollySensitivity = 0.05f   (world units per pixel)
 *   - kSlowMultiplier   = 0.25f   (Ctrl held)
 */

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

#include "editor/CameraController.h"
#include "scene/Camera.h"

using namespace neurus;

namespace {

/**
 * @brief Computes Euclidean distance from camera to its look-at target.
 */
float DistanceToTarget(const Camera& cam)
{
	return glm::length(cam.GetPosition() - cam.cam_tar);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Test Fixture
// ---------------------------------------------------------------------------

class CameraControllerTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_camera = std::make_unique<Camera>();
		m_controller = std::make_unique<CameraController>();
	}

	void TearDown() override
	{
		// No-op
	}

	std::unique_ptr<Camera> m_camera;
	std::unique_ptr<CameraController> m_controller;
};

// ===========================================================================
// Orbit tests (MMB, no modifier)
// ===========================================================================

/**
 * @test Orbit_MMB_Right_Drag_AzimuthIncreases
 *
 * Camera at (5,0,0), target at origin. MMB drag right (positive mouseDeltaX)
 * should rotate azimuth around world Y axis, producing a non-zero Z component.
 * Target remains unchanged.
 */
TEST_F(CameraControllerTest, Orbit_MMB_Right_Drag_AzimuthIncreases)
{
	m_camera->SetCamPos(glm::vec3(5.0f, 0.0f, 0.0f));
	m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
	const glm::vec3 initialTar = m_camera->cam_tar;

	InputState input;
	input.middleMouseHeld = true;
	input.mouseDeltaX = 10.0f;

	m_controller->Update(*m_camera, input);

	// Camera Z should be non-zero (azimuth rotated right around target)
	EXPECT_NE(m_camera->GetPosition().z, 0.0f);
	// Target unchanged
	EXPECT_EQ(m_camera->cam_tar, initialTar);
}

/**
 * @test Orbit_MMB_Up_Drag_ElevationIncreases
 *
 * Camera at (5,0,0), target at origin. MMB drag up (positive mouseDeltaY)
 * should decrease elevation (drag up = look down, natural 3D convention).
 * Camera Y position decreases. Target remains unchanged.
 */
TEST_F(CameraControllerTest, Orbit_MMB_Up_Drag_ElevationIncreases)
{
	m_camera->SetCamPos(glm::vec3(5.0f, 0.0f, 0.0f));
	m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
	const float initialY = m_camera->GetPosition().y;
	const glm::vec3 initialTar = m_camera->cam_tar;

	InputState input;
	input.middleMouseHeld = true;
	input.mouseDeltaY = 10.0f;

	m_controller->Update(*m_camera, input);

	// Elevation decreased (drag up = look down) — camera Y should be lower
	EXPECT_LT(m_camera->GetPosition().y, initialY);
	// Target unchanged
	EXPECT_EQ(m_camera->cam_tar, initialTar);
}

/**
 * @test Orbit_Clamp_Elevation_89_Degrees
 *
 * Camera at (0,5,0) looking straight down at origin. A large mouseDeltaY
 * should NOT flip the camera past the poles. Elevation is clamped to ±89°.
 * No crash, position values remain finite.
 */
TEST_F(CameraControllerTest, Orbit_Clamp_Elevation_89_Degrees)
{
	m_camera->SetCamPos(glm::vec3(0.0f, 5.0f, 0.0f));
	m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));

	InputState input;
	input.middleMouseHeld = true;
	input.mouseDeltaY = 1000.0f;

	// Should not crash or assert
	EXPECT_NO_FATAL_FAILURE(
		m_controller->Update(*m_camera, input);
	);

	const glm::vec3 pos = m_camera->GetPosition();
	EXPECT_TRUE(std::isfinite(pos.x));
	EXPECT_TRUE(std::isfinite(pos.y));
	EXPECT_TRUE(std::isfinite(pos.z));

	// Elevation from target should not exceed ±89°
	const glm::vec3 dir = pos - m_camera->cam_tar;
	const float radius = glm::length(dir);
	if (radius > 1e-6f)
	{
		const float elevation = std::asin(dir.y / radius);
		EXPECT_LE(std::abs(elevation), glm::radians(89.1f));
	}
}

// ===========================================================================
// Zoom tests (scroll wheel)
// ===========================================================================

/**
 * @test Zoom_ScrollUp_Decreases_Distance
 *
 * Camera at (0,0,5), target at origin. Scroll up (positive scrollDelta)
 * should zoom in, decreasing the distance from camera to target.
 */
TEST_F(CameraControllerTest, Zoom_ScrollUp_Decreases_Distance)
{
	m_camera->SetCamPos(glm::vec3(0.0f, 0.0f, 5.0f));
	m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
	const float initialDist = DistanceToTarget(*m_camera);
	const glm::vec3 initialTar = m_camera->cam_tar;

	InputState input;
	input.scrollDelta = 1.0f;

	m_controller->Update(*m_camera, input);

	EXPECT_LT(DistanceToTarget(*m_camera), initialDist);
	EXPECT_EQ(m_camera->cam_tar, initialTar);
}

/**
 * @test Zoom_ScrollDown_Increases_Distance
 *
 * Camera at (0,0,5), target at origin. Scroll down (negative scrollDelta)
 * should zoom out, increasing the distance from camera to target.
 */
TEST_F(CameraControllerTest, Zoom_ScrollDown_Increases_Distance)
{
	m_camera->SetCamPos(glm::vec3(0.0f, 0.0f, 5.0f));
	m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
	const float initialDist = DistanceToTarget(*m_camera);
	const glm::vec3 initialTar = m_camera->cam_tar;

	InputState input;
	input.scrollDelta = -1.0f;

	m_controller->Update(*m_camera, input);

	EXPECT_GT(DistanceToTarget(*m_camera), initialDist);
	EXPECT_EQ(m_camera->cam_tar, initialTar);
}

/**
 * @test Zoom_Clamp_MinRadius
 *
 * Camera already at minimum distance (0.01) from target. Attempting to zoom
 * in further should not reduce distance below the minimum clamp. No crash.
 */
TEST_F(CameraControllerTest, Zoom_Clamp_MinRadius)
{
	m_camera->SetCamPos(glm::vec3(0.0f, 0.0f, 0.01f));
	m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));

	InputState input;
	input.scrollDelta = 1.0f; // Try to zoom in more

	EXPECT_NO_FATAL_FAILURE(
		m_controller->Update(*m_camera, input);
	);

	// Distance must stay at or above the minimum clamp
	EXPECT_GE(DistanceToTarget(*m_camera), 0.01f);
}

// ===========================================================================
// Dolly tests (Ctrl + MMB)
// ===========================================================================

/**
 * @test Dolly_CtrlMMB_MovesAlongForward
 *
 * Camera at (0,0,5), target at origin. Ctrl+MMB drag up (positive mouseDeltaY)
 * should move camera along the forward/view direction (Z axis). X unchanged.
 * Target stays fixed (dolly translates only camera, not target).
 */
TEST_F(CameraControllerTest, Dolly_CtrlMMB_MovesAlongForward)
{
	m_camera->SetCamPos(glm::vec3(0.0f, 0.0f, 5.0f));
	m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
	const float initialX = m_camera->GetPosition().x;
	const float initialZ = m_camera->GetPosition().z;
	const glm::vec3 initialTar = m_camera->cam_tar;

	InputState input;
	input.ctrlHeld = true;
	input.middleMouseHeld = true;
	input.mouseDeltaY = 10.0f;

	m_controller->Update(*m_camera, input);

	// Camera moved along forward axis (Z changed)
	EXPECT_NE(m_camera->GetPosition().z, initialZ);
	// X unchanged (movement is strictly along view direction)
	EXPECT_FLOAT_EQ(m_camera->GetPosition().x, initialX);
	// Target stays fixed
	EXPECT_EQ(m_camera->cam_tar, initialTar);
}

/**
 * @test Dolly_WithoutMMB_NoMovement
 *
 * Ctrl held but MMB NOT held — no dolly should occur. Camera unchanged.
 */
TEST_F(CameraControllerTest, Dolly_WithoutMMB_NoMovement)
{
	m_camera->SetCamPos(glm::vec3(0.0f, 0.0f, 5.0f));
	m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
	const glm::vec3 initialPos = m_camera->GetPosition();
	const glm::vec3 initialTar = m_camera->cam_tar;

	InputState input;
	input.ctrlHeld = true;
	input.middleMouseHeld = false; // No MMB — should not trigger dolly
	input.mouseDeltaY = 10.0f;

	m_controller->Update(*m_camera, input);

	EXPECT_EQ(m_camera->GetPosition(), initialPos);
	EXPECT_EQ(m_camera->cam_tar, initialTar);
}

// ===========================================================================
// Pan tests (Shift + MMB)
// ===========================================================================

/**
 * @test Pan_ShiftMMB_Right_MovesCameraAndTarget
 *
 * Camera at (5,0,0), target at origin. Shift+MMB drag right
 * (positive mouseDeltaX) should pan both camera and target along
 * the camera's right vector. The offset between them is preserved.
 */
TEST_F(CameraControllerTest, Pan_ShiftMMB_Right_MovesCameraAndTarget)
{
	m_camera->SetCamPos(glm::vec3(5.0f, 0.0f, 0.0f));
	m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
	const glm::vec3 initialPos = m_camera->GetPosition();
	const glm::vec3 initialTar = m_camera->cam_tar;

	InputState input;
	input.shiftHeld = true;
	input.middleMouseHeld = true;
	input.mouseDeltaX = 10.0f;

	m_controller->Update(*m_camera, input);

	// Both camera position and target changed (pan shifts both)
	EXPECT_NE(m_camera->GetPosition(), initialPos);
	EXPECT_NE(m_camera->cam_tar, initialTar);

	// Position-target offset preserved (pure translation, no rotation)
	const glm::vec3 offsetBefore = initialPos - initialTar;
	const glm::vec3 offsetAfter  = m_camera->GetPosition() - m_camera->cam_tar;
	EXPECT_NEAR(offsetAfter.x, offsetBefore.x, 1e-4f);
	EXPECT_NEAR(offsetAfter.y, offsetBefore.y, 1e-4f);
	EXPECT_NEAR(offsetAfter.z, offsetBefore.z, 1e-4f);
}

/**
 * @test Pan_ShiftMMB_Up_MovesUp
 *
 * Camera at (5,0,0), target at origin. Shift+MMB drag up
 * (positive mouseDeltaY) should pan vertically, changing Y of
 * both camera and target.
 */
TEST_F(CameraControllerTest, Pan_ShiftMMB_Up_MovesUp)
{
	m_camera->SetCamPos(glm::vec3(5.0f, 0.0f, 0.0f));
	m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
	const float initialY = m_camera->GetPosition().y;
	const float initialTarY = m_camera->cam_tar.y;

	InputState input;
	input.shiftHeld = true;
	input.middleMouseHeld = true;
	input.mouseDeltaY = 10.0f;

	m_controller->Update(*m_camera, input);

	// Vertical pan should change Y of both camera and target
	EXPECT_NE(m_camera->GetPosition().y, initialY);
	EXPECT_NE(m_camera->cam_tar.y, initialTarY);
}

// ===========================================================================
// Edge case tests
// ===========================================================================

/**
 * @test Edge_CameraAtTarget_NoCrash
 *
 * Camera position equals target position (degenerate direction vector).
 * Update() should handle this gracefully — no crash, no position change.
 */
TEST_F(CameraControllerTest, Edge_CameraAtTarget_NoCrash)
{
	m_camera->SetCamPos(glm::vec3(0.0f, 0.0f, 0.0f));
	m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
	const glm::vec3 initialPos = m_camera->GetPosition();
	const glm::vec3 initialTar = m_camera->cam_tar;

	InputState input;
	input.middleMouseHeld = true;
	input.mouseDeltaX = 10.0f;
	input.mouseDeltaY = 10.0f;
	input.scrollDelta = 5.0f;

	EXPECT_NO_FATAL_FAILURE(
		m_controller->Update(*m_camera, input);
	);

	// No position change (degenerate direction — all ops early-out)
	EXPECT_EQ(m_camera->GetPosition(), initialPos);
	EXPECT_EQ(m_camera->cam_tar, initialTar);
}

/**
 * @test Edge_NoInput_NoChange
 *
 * All InputState fields zero/false. Update() should not modify the camera.
 */
TEST_F(CameraControllerTest, Edge_NoInput_NoChange)
{
	m_camera->SetCamPos(glm::vec3(3.0f, 4.0f, 5.0f));
	m_camera->SetTarPos(glm::vec3(1.0f, 2.0f, 3.0f));
	const glm::vec3 initialPos = m_camera->GetPosition();
	const glm::vec3 initialTar = m_camera->cam_tar;

	InputState input; // All fields zero/false

	m_controller->Update(*m_camera, input);

	EXPECT_EQ(m_camera->GetPosition(), initialPos);
	EXPECT_EQ(m_camera->cam_tar, initialTar);
}

/**
 * @test Edge_ModifierConflict_CtrlWins
 *
 * Both Ctrl and Shift held with MMB. Ctrl should take priority,
 * resulting in Dolly behavior (not Pan). Target should NOT move.
 */
TEST_F(CameraControllerTest, Edge_ModifierConflict_CtrlWins)
{
	m_camera->SetCamPos(glm::vec3(0.0f, 0.0f, 5.0f));
	m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
	const glm::vec3 initialTar = m_camera->cam_tar;

	InputState input;
	input.ctrlHeld = true;
	input.shiftHeld = true;  // Both modifiers held
	input.middleMouseHeld = true;
	input.mouseDeltaY = 10.0f;

	m_controller->Update(*m_camera, input);

	// Ctrl wins → Dolly behavior: target should NOT move
	EXPECT_EQ(m_camera->cam_tar, initialTar);
	// Camera should have moved (dolly along forward)
	EXPECT_NE(m_camera->GetPosition().z, 5.0f);
}
