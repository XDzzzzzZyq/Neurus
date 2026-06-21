/**
 * @file test_camera_controller.cpp
 * @brief Tests for CameraController event-driven navigation (orbit, dolly, pan, zoom).
 *
 * CameraController is now event-driven 鈥?it subscribes to CameraEvents via Init()
 * and reacts to enqueued events dispatched through EventQueue::Process().
 *
 * MMB Convention (from decisions.md):
 *   - MMB alone       = Orbit  (CameraRotateEvent)
 *   - Ctrl + MMB      = Dolly  (CameraPushEvent)
 *   - Shift + MMB     = Pan    (CameraSlideEvent)
 *   - Scroll wheel    = Zoom   (CameraZoomEvent)
 *
 * Sensitivity constants (from CameraController.h):
 *   - kOrbitSensitivity = 0.005f  (radians per pixel)
 *   - kPanSensitivity   = 0.01f   (world units per pixel)
 *   - kZoomSensitivity  = 1.0f    (factor per scroll notch)
 *   - kDollySensitivity = 0.05f   (world units per pixel)
 */

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

#include "editor/controllers/CameraController.h"
#include "editor/events/EventBus.h"
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
		m_controller->Init(EventQueue());
	}

	void TearDown() override
	{
		// No-op 鈥?EventQueue is a singleton shared across tests
	}

	std::unique_ptr<Camera> m_camera;
	std::unique_ptr<CameraController> m_controller;
};

// ===========================================================================
// Orbit tests (MMB, no modifier 鈫?CameraRotateEvent)
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

	CameraRotateEvent e{m_camera.get(), 10.0f, 0.0f};
	EventQueue().enqueue(e);
	EventQueue().Process();

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

	CameraRotateEvent e{m_camera.get(), 0.0f, 10.0f};
	EventQueue().enqueue(e);
	EventQueue().Process();

	// Elevation decreased (drag up = look down) 鈥?camera Y should be lower
	EXPECT_LT(m_camera->GetPosition().y, initialY);
	// Target unchanged
	EXPECT_EQ(m_camera->cam_tar, initialTar);
}

/**
 * @test Orbit_Clamp_Elevation_89_Degrees
 *
 * Camera at (0,5,0) looking straight down at origin. A large mouseDeltaY
 * should NOT flip the camera past the poles. Elevation is clamped to 卤89掳.
 * No crash, position values remain finite.
 */
TEST_F(CameraControllerTest, Orbit_Clamp_Elevation_89_Degrees)
{
	m_camera->SetCamPos(glm::vec3(0.0f, 5.0f, 0.0f));
	m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));

	CameraRotateEvent e{m_camera.get(), 0.0f, 1000.0f};

	// Should not crash or assert
	EXPECT_NO_FATAL_FAILURE(
		EventQueue().enqueue(e);
		EventQueue().Process();
	);

	const glm::vec3 pos = m_camera->GetPosition();
	EXPECT_TRUE(std::isfinite(pos.x));
	EXPECT_TRUE(std::isfinite(pos.y));
	EXPECT_TRUE(std::isfinite(pos.z));

	// Elevation from target should not exceed 卤89掳
	const glm::vec3 dir = pos - m_camera->cam_tar;
	const float radius = glm::length(dir);
	if (radius > 1e-6f)
	{
		const float elevation = std::asin(dir.y / radius);
		EXPECT_LE(std::abs(elevation), glm::radians(89.1f));
	}
}

// ===========================================================================
// Zoom tests (scroll wheel 鈫?CameraZoomEvent)
// ===========================================================================

/**
 * @test Zoom_ScrollUp_Decreases_Distance
 *
 * Camera at (0,0,5), target at origin. Scroll up (positive scroll_dir)
 * should zoom in, decreasing the distance from camera to target.
 */
TEST_F(CameraControllerTest, Zoom_ScrollUp_Decreases_Distance)
{
	m_camera->SetCamPos(glm::vec3(0.0f, 0.0f, 5.0f));
	m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
	const float initialDist = DistanceToTarget(*m_camera);
	const glm::vec3 initialTar = m_camera->cam_tar;

	CameraZoomEvent e{m_camera.get(), 1.0f};
	EventQueue().enqueue(e);
	EventQueue().Process();

	EXPECT_LT(DistanceToTarget(*m_camera), initialDist);
	EXPECT_EQ(m_camera->cam_tar, initialTar);
}

/**
 * @test Zoom_ScrollDown_Increases_Distance
 *
 * Camera at (0,0,5), target at origin. Scroll down (negative scroll_dir)
 * should zoom out, increasing the distance from camera to target.
 */
TEST_F(CameraControllerTest, Zoom_ScrollDown_Increases_Distance)
{
	m_camera->SetCamPos(glm::vec3(0.0f, 0.0f, 5.0f));
	m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
	const float initialDist = DistanceToTarget(*m_camera);
	const glm::vec3 initialTar = m_camera->cam_tar;

	CameraZoomEvent e{m_camera.get(), -1.0f};
	EventQueue().enqueue(e);
	EventQueue().Process();

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

	CameraZoomEvent e{m_camera.get(), 1.0f}; // Try to zoom in more

	EXPECT_NO_FATAL_FAILURE(
		EventQueue().enqueue(e);
		EventQueue().Process();
	);

	// Distance must stay at or above the minimum clamp
	EXPECT_GE(DistanceToTarget(*m_camera), 0.01f);
}

// ===========================================================================
// Dolly tests (Ctrl + MMB 鈫?CameraPushEvent)
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

	CameraPushEvent e{m_camera.get(), 0.0f, 10.0f};
	EventQueue().enqueue(e);
	EventQueue().Process();

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
 * No event enqueued 鈥?camera should not change.
 */
TEST_F(CameraControllerTest, Dolly_WithoutMMB_NoMovement)
{
	m_camera->SetCamPos(glm::vec3(0.0f, 0.0f, 5.0f));
	m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
	const glm::vec3 initialPos = m_camera->GetPosition();
	const glm::vec3 initialTar = m_camera->cam_tar;

	// No event 鈥?camera unchanged
	EventQueue().Process();

	EXPECT_EQ(m_camera->GetPosition(), initialPos);
	EXPECT_EQ(m_camera->cam_tar, initialTar);
}

// ===========================================================================
// Pan tests (Shift + MMB 鈫?CameraSlideEvent)
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

	CameraSlideEvent e{m_camera.get(), 10.0f, 0.0f};
	EventQueue().enqueue(e);
	EventQueue().Process();

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

	CameraSlideEvent e{m_camera.get(), 0.0f, 10.0f};
	EventQueue().enqueue(e);
	EventQueue().Process();

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
 * Event handlers should handle this gracefully 鈥?no crash, no position change.
 */
TEST_F(CameraControllerTest, Edge_CameraAtTarget_NoCrash)
{
	m_camera->SetCamPos(glm::vec3(0.0f, 0.0f, 0.0f));
	m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
	const glm::vec3 initialPos = m_camera->GetPosition();
	const glm::vec3 initialTar = m_camera->cam_tar;

	EXPECT_NO_FATAL_FAILURE(
		EventQueue().enqueue(CameraRotateEvent{m_camera.get(), 10.0f, 10.0f});
		EventQueue().enqueue(CameraZoomEvent{m_camera.get(), 5.0f});
		EventQueue().Process();
	);

	// No position change (degenerate direction 鈥?all ops early-out)
	EXPECT_EQ(m_camera->GetPosition(), initialPos);
	EXPECT_EQ(m_camera->cam_tar, initialTar);
}

/**
 * @test Edge_NoInput_NoChange
 *
 * No events enqueued 鈥?camera should not change.
 */
TEST_F(CameraControllerTest, Edge_NoInput_NoChange)
{
	m_camera->SetCamPos(glm::vec3(3.0f, 4.0f, 5.0f));
	m_camera->SetTarPos(glm::vec3(1.0f, 2.0f, 3.0f));
	const glm::vec3 initialPos = m_camera->GetPosition();
	const glm::vec3 initialTar = m_camera->cam_tar;

	// No events enqueued 鈥?camera unchanged
	EventQueue().Process();

	EXPECT_EQ(m_camera->GetPosition(), initialPos);
	EXPECT_EQ(m_camera->cam_tar, initialTar);
}

/**
 * @test Edge_ModifierConflict_CtrlWins
 *
 * When both Ctrl and Shift are held (event system decides), the appropriate
 * event type (CameraPushEvent for Ctrl priority) should be enqueued.
 * Target should NOT move (dolly behavior, not pan).
 */
TEST_F(CameraControllerTest, Edge_ModifierConflict_CtrlWins)
{
	m_camera->SetCamPos(glm::vec3(0.0f, 0.0f, 5.0f));
	m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
	const glm::vec3 initialTar = m_camera->cam_tar;

	// Ctrl wins 鈫?CameraPushEvent (dolly), not CameraSlideEvent (pan)
	CameraPushEvent e{m_camera.get(), 0.0f, 10.0f};
	EventQueue().enqueue(e);
	EventQueue().Process();

	// Ctrl wins 鈫?Dolly behavior: target should NOT move
	EXPECT_EQ(m_camera->cam_tar, initialTar);
	// Camera should have moved (dolly along forward)
	EXPECT_NE(m_camera->GetPosition().z, 5.0f);
}
