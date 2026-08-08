#include <gtest/gtest.h>

#include <memory>

#include "editor/controllers/SceneController.h"
#include "editor/events/SceneEvents.h"
#include "editor/events/EditorEvents.h"
#include "editor/operations/OperationManager.h"
#include "editor/Input.h"
#include "scene/Camera.h"
#include "scene/Environment.h"
#include "scene/Light.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"
#include "scene/ObjectID.h"

using namespace neurus;

class SceneControllerTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_controller.Init(m_eventBus, m_operations);

		m_camera = std::make_shared<Camera>();
		m_mesh   = std::make_shared<Mesh>();
		m_light  = std::make_shared<Light>(POINTLIGHT, 10.0f, glm::vec3(1.0f));
		m_env    = std::make_shared<Environment>();
		m_scene.UseCamera(m_camera);
		m_scene.UseMesh(m_mesh);
		m_scene.UseLight(m_light);
		m_scene.UseEnvironment(m_env);
	}

	void Process() { m_eventBus.Process(); }

	EventQueue m_eventBus;
	Scene m_scene;
	OperationManager m_operations{ m_eventBus, [this]() -> Scene* { return &m_scene; } };
	SceneController m_controller;
	std::shared_ptr<Camera> m_camera;
	std::shared_ptr<Mesh> m_mesh;
	std::shared_ptr<Light> m_light;
	std::shared_ptr<Environment> m_env;
};

// --- Selection -------------------------------------------------------------

TEST_F(SceneControllerTest, ObjectSelected_SelectsObject)
{
	m_eventBus.enqueue(ObjectSelected{&m_scene, m_mesh.get(), 0});
	Process();
	EXPECT_TRUE(m_scene.selections.IsSelected(m_mesh.get()));
	EXPECT_EQ(m_scene.selections.GetActiveObject(), m_mesh.get());
}

TEST_F(SceneControllerTest, ObjectSelected_NullObject_ClearsSelection)
{
	m_scene.selections.Select(m_mesh.get(), false);
	m_eventBus.enqueue(ObjectSelected{&m_scene, nullptr, 0});
	Process();
	EXPECT_EQ(m_scene.selections.GetSelectionCount(), 0);
}

TEST_F(SceneControllerTest, ObjectSelected_Shift_AddsToSelection)
{
	m_eventBus.enqueue(ObjectSelected{&m_scene, m_mesh.get(), 0});
	m_eventBus.enqueue(ObjectSelected{&m_scene, m_light.get(),
	                                  static_cast<int>(Input::Mod_Shift)});
	Process();
	EXPECT_EQ(m_scene.selections.GetSelectionCount(), 2);
}

TEST_F(SceneControllerTest, ObjectDeselected_RemovesFromSelection)
{
	m_scene.selections.Select(m_mesh.get(), false);
	m_eventBus.enqueue(ObjectDeselected{&m_scene, m_mesh.get()});
	Process();
	EXPECT_FALSE(m_scene.selections.IsSelected(m_mesh.get()));
}

// --- Visibility ------------------------------------------------------------

TEST_F(SceneControllerTest, VisibilityChanged_SetsFlags)
{
	m_eventBus.enqueue(VisibilityChanged{m_mesh.get(), false, true});
	Process();
	EXPECT_FALSE(m_mesh->is_viewport);
	EXPECT_TRUE(m_mesh->is_rendered);
}

TEST_F(SceneControllerTest, VisibilityChanged_OnLight_EnqueuesLightingRebuild)
{
	bool rebuilt = false;
	m_eventBus.subscribe<LightingRebuild>([&](const LightingRebuild&) { rebuilt = true; });
	m_eventBus.enqueue(VisibilityChanged{m_light.get(), false, true});
	Process();
	EXPECT_TRUE(rebuilt);
}

// --- Transform -------------------------------------------------------------

TEST_F(SceneControllerTest, PositionChanged_UpdatesTransform)
{
	m_eventBus.enqueue(PositionChanged{m_mesh.get(), 1.0f, 2.0f, 3.0f});
	Process();
	const glm::vec3& pos = m_mesh->GetPosition();
	EXPECT_FLOAT_EQ(pos.x, 1.0f);
	EXPECT_FLOAT_EQ(pos.y, 2.0f);
	EXPECT_FLOAT_EQ(pos.z, 3.0f);
}

TEST_F(SceneControllerTest, PositionChanged_OnLight_EnqueuesLightingRebuild)
{
	bool rebuilt = false;
	m_eventBus.subscribe<LightingRebuild>([&](const LightingRebuild&) { rebuilt = true; });
	m_eventBus.enqueue(PositionChanged{m_light.get(), 1.0f, 0.0f, 0.0f});
	Process();
	EXPECT_TRUE(rebuilt);
}

TEST_F(SceneControllerTest, ScaleChanged_UpdatesScale)
{
	m_eventBus.enqueue(ScaleChanged{m_mesh.get(), 2.0f, 2.0f, 2.0f});
	Process();
	const glm::vec3& scl = m_mesh->GetScale();
	EXPECT_FLOAT_EQ(scl.x, 2.0f);
	EXPECT_FLOAT_EQ(scl.y, 2.0f);
	EXPECT_FLOAT_EQ(scl.z, 2.0f);
}

// --- Camera ----------------------------------------------------------------

TEST_F(SceneControllerTest, CameraTargetChanged_Applies)
{
	m_eventBus.enqueue(CameraTargetChanged{m_camera.get(), 1.0f, 2.0f, 3.0f});
	Process();
	EXPECT_EQ(m_camera->cam_tar, glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST_F(SceneControllerTest, CameraFovChanged_Applies)
{
	m_eventBus.enqueue(CameraFovChanged{m_camera.get(), 45.0f});
	Process();
	EXPECT_FLOAT_EQ(m_camera->cam_pers, 45.0f);
}

// --- Mesh ------------------------------------------------------------------

TEST_F(SceneControllerTest, MeshShadowChanged_Applies)
{
	m_eventBus.enqueue(MeshShadowChanged{m_mesh.get(), false});
	Process();
	EXPECT_FALSE(m_mesh->using_shadow);
}

TEST_F(SceneControllerTest, MeshMaterialChanged_Applies)
{
	m_eventBus.enqueue(MeshMaterialChanged{m_mesh.get(), false});
	Process();
	EXPECT_FALSE(m_mesh->using_material);
}

// --- Light -----------------------------------------------------------------

TEST_F(SceneControllerTest, LightPowerChanged_AppliesAndEnqueuesGpuEvent)
{
	const ObjectID* gpuObject = nullptr;
	m_eventBus.subscribe<LightGpuChanged>([&](const LightGpuChanged& e) { gpuObject = e.object; });
	m_eventBus.enqueue(LightPowerChanged{m_light.get(), 42.0f});
	Process();
	EXPECT_FLOAT_EQ(m_light->light_power, 42.0f);
	EXPECT_EQ(gpuObject, m_light.get());
}

TEST_F(SceneControllerTest, LightRadiusChanged_Applies)
{
	m_eventBus.enqueue(LightRadiusChanged{m_light.get(), 0.5f});
	Process();
	EXPECT_FLOAT_EQ(m_light->light_radius, 0.5f);
}

TEST_F(SceneControllerTest, LightShadowChanged_EnqueuesLightingRebuild)
{
	bool rebuilt = false;
	m_eventBus.subscribe<LightingRebuild>([&](const LightingRebuild&) { rebuilt = true; });
	m_eventBus.enqueue(LightShadowChanged{m_light.get(), false});
	Process();
	EXPECT_FALSE(m_light->use_shadow);
	EXPECT_TRUE(rebuilt);
}

TEST_F(SceneControllerTest, LightCutoffChanged_Applies)
{
	m_eventBus.enqueue(LightCutoffChanged{m_light.get(), 0.7f});
	Process();
	EXPECT_FLOAT_EQ(m_light->spot_cutoff, 0.7f);
}

// --- Environment -----------------------------------------------------------

TEST_F(SceneControllerTest, EnvironmentIntensityChanged_Applies)
{
	m_eventBus.enqueue(EnvironmentIntensityChanged{m_env.get(), 2.5f});
	Process();
	EXPECT_FLOAT_EQ(m_env->GetIntensity(), 2.5f);
}

TEST_F(SceneControllerTest, EnvironmentRotationChanged_Applies)
{
	m_eventBus.enqueue(EnvironmentRotationChanged{m_env.get(), 90.0f});
	Process();
	EXPECT_FLOAT_EQ(m_env->GetRotation(), 90.0f);
}

// --- Dirty / reset semantics ----------------------------------------------

TEST_F(SceneControllerTest, PropertyChange_EnqueuesSceneModified)
{
	int modified = 0;
	m_eventBus.subscribe<SceneModified>([&](const SceneModified&) { modified++; });
	m_eventBus.enqueue(CameraFovChanged{m_camera.get(), 45.0f});
	Process();
	EXPECT_EQ(modified, 1);
}

TEST_F(SceneControllerTest, Selection_DoesNotEnqueueSceneModified)
{
	int modified = 0;
	m_eventBus.subscribe<SceneModified>([&](const SceneModified&) { modified++; });
	m_eventBus.enqueue(ObjectSelected{&m_scene, m_mesh.get(), 0});
	Process();
	EXPECT_EQ(modified, 0);
}
