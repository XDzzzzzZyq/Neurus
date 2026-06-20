/**
 * @file test_camera_controller.cpp
 * @brief Unit tests for CameraController orbit, pan, and zoom.
 *
 * Tests verify that mouse input is correctly translated into camera
 * transform updates. All tests are pure CPU — no GPU required.
 */

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

#include "editor/CameraController.h"
#include "editor/events/EventBus.h"
#include "scene/Camera.h"
#include "scene/Scene.h"

using namespace neurus;

// ---------------------------------------------------------------------------
// Test Fixture
// ---------------------------------------------------------------------------

class CameraControllerTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_scene = std::make_unique<Scene>();
		// Camera starts at (0, 2, 5), looking at origin
		m_camera = std::make_unique<Camera>();
		m_camera->SetCamPos(glm::vec3(0.0f, 2.0f, 5.0f));
		m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));

		m_controller = std::make_unique<CameraController>();
	}

	void TearDown() override
	{
		// Process any queued EventQueue events
		EventQueue().Process();
	}

	std::unique_ptr<Scene> m_scene;
	std::unique_ptr<Camera> m_camera;
	std::unique_ptr<CameraController> m_controller;
};

// ---------------------------------------------------------------------------
// Orbiting: RMB drag rotates camera around target
// ---------------------------------------------------------------------------

TEST_F(CameraControllerTest, Orbit_RotatesAroundTarget)
{
	const glm::vec3 initialPos = m_camera->GetPosition();
	const glm::vec3 initialTar = m_camera->cam_tar;

	InputState input;
	input.rightMouseHeld = true;
	input.mouseDeltaX = 100.0f; // horizontal drag → yaw
	input.mouseDeltaY = 0.0f;   // no vertical

	m_controller->Update(*m_camera, *m_scene, input);

	// Position should change (camera orbits)
	EXPECT_NE(m_camera->GetPosition(), initialPos);
	// Target should remain fixed during orbit
	EXPECT_EQ(m_camera->cam_tar, initialTar);
}

TEST_F(CameraControllerTest, Orbit_VerticalRotation)
{
	const float initialDistance = glm::length(m_camera->GetPosition() - m_camera->cam_tar);

	InputState input;
	input.rightMouseHeld = true;
	input.mouseDeltaX = 0.0f;
	input.mouseDeltaY = 50.0f; // vertical drag → pitch

	m_controller->Update(*m_camera, *m_scene, input);

	// Distance from target should be preserved in orbit
	const float newDistance = glm::length(m_camera->GetPosition() - m_camera->cam_tar);
	EXPECT_NEAR(newDistance, initialDistance, 1e-4f);
}

TEST_F(CameraControllerTest, Orbit_PreservesTargetDistance)
{
	const float distBefore = glm::length(m_camera->GetPosition() - m_camera->cam_tar);

	InputState input;
	input.rightMouseHeld = true;
	input.mouseDeltaX = 200.0f;
	input.mouseDeltaY = -30.0f;

	m_controller->Update(*m_camera, *m_scene, input);

	const float distAfter = glm::length(m_camera->GetPosition() - m_camera->cam_tar);
	EXPECT_NEAR(distBefore, distAfter, 1e-4f);
}

// ---------------------------------------------------------------------------
// Panning: MMB drag translates camera and target
// ---------------------------------------------------------------------------

TEST_F(CameraControllerTest, Pan_MovesPositionAndTarget)
{
	const glm::vec3 initialPos = m_camera->GetPosition();
	const glm::vec3 initialTar = m_camera->cam_tar;

	InputState input;
	input.middleMouseHeld = true;
	input.mouseDeltaX = 50.0f;
	input.mouseDeltaY = 0.0f;

	m_controller->Update(*m_camera, *m_scene, input);

	// Both position and target should move
	EXPECT_NE(m_camera->GetPosition(), initialPos);
	EXPECT_NE(m_camera->cam_tar, initialTar);

	// Position - target offset should be preserved (pure translation)
	const glm::vec3 offsetBefore = initialPos - initialTar;
	const glm::vec3 offsetAfter = m_camera->GetPosition() - m_camera->cam_tar;
	EXPECT_NEAR(offsetAfter.x, offsetBefore.x, 1e-4f);
	EXPECT_NEAR(offsetAfter.y, offsetBefore.y, 1e-4f);
	EXPECT_NEAR(offsetAfter.z, offsetBefore.z, 1e-4f);
}

TEST_F(CameraControllerTest, Pan_ShiftRMB_TriggersPanNotOrbit)
{
	const glm::vec3 initialTar = m_camera->cam_tar;

	InputState input;
	input.rightMouseHeld = true;
	input.shiftHeld = true; // Shift+RMB → pan instead of orbit
	input.mouseDeltaX = 50.0f;
	input.mouseDeltaY = 0.0f;

	m_controller->Update(*m_camera, *m_scene, input);

	// Target should move (pan behavior), not stay fixed (orbit behavior)
	EXPECT_NE(m_camera->cam_tar, initialTar);
}

// ---------------------------------------------------------------------------
// Zooming: scroll wheel adjusts distance to target
// ---------------------------------------------------------------------------

TEST_F(CameraControllerTest, Zoom_ScrollUpBringsCameraCloser)
{
	const float distBefore = glm::length(m_camera->GetPosition() - m_camera->cam_tar);
	const glm::vec3 initialTar = m_camera->cam_tar;

	InputState input;
	input.scrollDelta = 5.0f; // scroll up → zoom in (closer)

	m_controller->Update(*m_camera, *m_scene, input);

	const float distAfter = glm::length(m_camera->GetPosition() - m_camera->cam_tar);
	// Camera should be closer to target
	EXPECT_LT(distAfter, distBefore);
	// Target remains stationary
	EXPECT_EQ(m_camera->cam_tar, initialTar);
}

TEST_F(CameraControllerTest, Zoom_ScrollDownMovesCameraAway)
{
	const float distBefore = glm::length(m_camera->GetPosition() - m_camera->cam_tar);
	const glm::vec3 initialTar = m_camera->cam_tar;

	InputState input;
	input.scrollDelta = -5.0f; // scroll down → zoom out (farther)

	m_controller->Update(*m_camera, *m_scene, input);

	const float distAfter = glm::length(m_camera->GetPosition() - m_camera->cam_tar);
	// Camera should be farther from target
	EXPECT_GT(distAfter, distBefore);
	EXPECT_EQ(m_camera->cam_tar, initialTar);
}

TEST_F(CameraControllerTest, Zoom_PreservesDirection)
{
	const glm::vec3 dirBefore = glm::normalize(m_camera->GetPosition() - m_camera->cam_tar);

	InputState input;
	input.scrollDelta = 10.0f;

	m_controller->Update(*m_camera, *m_scene, input);

	const glm::vec3 dirAfter = glm::normalize(m_camera->GetPosition() - m_camera->cam_tar);
	// Direction from target to camera should be unchanged
	EXPECT_NEAR(dirAfter.x, dirBefore.x, 1e-5f);
	EXPECT_NEAR(dirAfter.y, dirBefore.y, 1e-5f);
	EXPECT_NEAR(dirAfter.z, dirBefore.z, 1e-5f);
}

// ---------------------------------------------------------------------------
// No input → no change
// ---------------------------------------------------------------------------

TEST_F(CameraControllerTest, NoInput_NoChange)
{
	const glm::vec3 initialPos = m_camera->GetPosition();
	const glm::vec3 initialTar = m_camera->cam_tar;

	InputState input; // all zeros/defaults
	m_controller->Update(*m_camera, *m_scene, input);

	EXPECT_EQ(m_camera->GetPosition(), initialPos);
	EXPECT_EQ(m_camera->cam_tar, initialTar);
}

TEST_F(CameraControllerTest, RMBWithoutDrag_NoChange)
{
	const glm::vec3 initialPos = m_camera->GetPosition();

	InputState input;
	input.rightMouseHeld = true; // held but no movement
	input.mouseDeltaX = 0.0f;
	input.mouseDeltaY = 0.0f;

	m_controller->Update(*m_camera, *m_scene, input);

	EXPECT_EQ(m_camera->GetPosition(), initialPos);
}

// ---------------------------------------------------------------------------
// Speed modifiers (Shift=fast, Ctrl=slow)
// ---------------------------------------------------------------------------

TEST_F(CameraControllerTest, ShiftModifier_IncreasesMovement)
{
	// Use zoom (scroll) to test speed — Shift+RMB triggers pan, not fast orbit.
	InputState normalInput;
	normalInput.scrollDelta = 5.0f;

	auto camNormal = std::make_unique<Camera>();
	camNormal->SetCamPos(glm::vec3(0.0f, 2.0f, 5.0f));
	camNormal->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
	auto normalScene = std::make_unique<Scene>();
	CameraController ctrlNormal;
	ctrlNormal.Update(*camNormal, *normalScene, normalInput);
	const float normalDist = glm::length(camNormal->GetPosition() - camNormal->cam_tar);

	InputState fastInput;
	fastInput.scrollDelta = 5.0f;
	fastInput.shiftHeld = true; // fast zoom

	auto camFast = std::make_unique<Camera>();
	camFast->SetCamPos(glm::vec3(0.0f, 2.0f, 5.0f));
	camFast->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
	auto fastScene = std::make_unique<Scene>();
	CameraController ctrlFast;
	ctrlFast.Update(*camFast, *fastScene, fastInput);
	const float fastDist = glm::length(camFast->GetPosition() - camFast->cam_tar);

	const float initialDist = glm::length(glm::vec3(0.0f, 2.0f, 5.0f));

	// Fast zoom should move camera closer (larger reduction in distance)
	const float normalDelta = initialDist - normalDist;
	const float fastDelta = initialDist - fastDist;
	EXPECT_GT(fastDelta, normalDelta);
}

TEST_F(CameraControllerTest, CtrlModifier_DecreasesMovement)
{
	InputState normalInput;
	normalInput.rightMouseHeld = true;
	normalInput.mouseDeltaX = 50.0f;
	normalInput.mouseDeltaY = 0.0f;

	auto camNormal = std::make_unique<Camera>();
	camNormal->SetCamPos(glm::vec3(0.0f, 2.0f, 5.0f));
	camNormal->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
	auto normalScene = std::make_unique<Scene>();
	CameraController ctrlNormal;
	ctrlNormal.Update(*camNormal, *normalScene, normalInput);
	const glm::vec3 normalPos = camNormal->GetPosition();

	InputState slowInput;
	slowInput.rightMouseHeld = true;
	slowInput.mouseDeltaX = 50.0f;
	slowInput.mouseDeltaY = 0.0f;
	slowInput.ctrlHeld = true; // slow mode

	auto camSlow = std::make_unique<Camera>();
	camSlow->SetCamPos(glm::vec3(0.0f, 2.0f, 5.0f));
	camSlow->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f));
	auto slowScene = std::make_unique<Scene>();
	CameraController ctrlSlow;
	ctrlSlow.Update(*camSlow, *slowScene, slowInput);
	const glm::vec3 slowPos = camSlow->GetPosition();

	// Slow mode should produce a smaller delta than normal
	const float normalDelta = glm::length(normalPos - glm::vec3(0.0f, 2.0f, 5.0f));
	const float slowDelta = glm::length(slowPos - glm::vec3(0.0f, 2.0f, 5.0f));
	EXPECT_LT(slowDelta, normalDelta);
}

// ---------------------------------------------------------------------------
// Scene status notification
// ---------------------------------------------------------------------------

TEST_F(CameraControllerTest, Update_SetsCameraChangedStatus)
{
	InputState input;
	input.rightMouseHeld = true;
	input.mouseDeltaX = 50.0f;
	input.mouseDeltaY = 10.0f;

	m_controller->Update(*m_camera, *m_scene, input);

	EXPECT_TRUE(m_scene->CheckStatus(Scene::CameraChanged));
}

TEST_F(CameraControllerTest, NoInput_DoesNotSetCameraChanged)
{
	m_scene->ResetStatus();

	InputState input; // all zeros
	m_controller->Update(*m_camera, *m_scene, input);

	EXPECT_FALSE(m_scene->CheckStatus(Scene::CameraChanged));
}

// ---------------------------------------------------------------------------
// Edge cases: degenerate camera positions
// ---------------------------------------------------------------------------

TEST_F(CameraControllerTest, Orbit_CameraAtTarget_DoesNotCrash)
{
	m_camera->SetCamPos(glm::vec3(0.0f, 0.0f, 0.0f));
	m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f)); // camera at target

	InputState input;
	input.rightMouseHeld = true;
	input.mouseDeltaX = 100.0f;
	input.mouseDeltaY = 100.0f;

	// Should not crash — orbit is a no-op when direction is zero
	EXPECT_NO_FATAL_FAILURE(
		m_controller->Update(*m_camera, *m_scene, input);
	);
}

TEST_F(CameraControllerTest, Zoom_CameraAtTarget_DoesNotCrash)
{
	m_camera->SetCamPos(glm::vec3(0.0f, 0.0f, 0.0f));
	m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f)); // camera at target

	InputState input;
	input.scrollDelta = 5.0f;

	EXPECT_NO_FATAL_FAILURE(
		m_controller->Update(*m_camera, *m_scene, input);
	);
}

TEST_F(CameraControllerTest, Pan_CameraAtTarget_DoesNotCrash)
{
	m_camera->SetCamPos(glm::vec3(0.0f, 0.0f, 0.0f));
	m_camera->SetTarPos(glm::vec3(0.0f, 0.0f, 0.0f)); // camera at target

	InputState input;
	input.middleMouseHeld = true;
	input.mouseDeltaX = 50.0f;
	input.mouseDeltaY = 50.0f;

	EXPECT_NO_FATAL_FAILURE(
		m_controller->Update(*m_camera, *m_scene, input);
	);
}

// ---------------------------------------------------------------------------
// Combined operations: orbit + zoom in same frame
// ---------------------------------------------------------------------------

TEST_F(CameraControllerTest, CombinedOrbitAndZoom)
{
	const glm::vec3 initialTar = m_camera->cam_tar;

	InputState input;
	input.rightMouseHeld = true;
	input.mouseDeltaX = 100.0f;
	input.mouseDeltaY = 0.0f;
	input.scrollDelta = 3.0f; // also zoom

	m_controller->Update(*m_camera, *m_scene, input);

	// Target should stay fixed (orbit + zoom both preserve target)
	EXPECT_EQ(m_camera->cam_tar, initialTar);
	// Position should change
	EXPECT_NE(m_camera->GetPosition(), glm::vec3(0.0f, 2.0f, 5.0f));
}
