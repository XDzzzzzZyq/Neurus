/**
 * @file test_resource_manager.cpp
 * @brief Tests for the core ResourceManager (UID object pool + factory).
 *
 * Covers:
 * - Load<T> constructs + registers + returns a typed shared_ptr
 * - Duplicate registration throws (safety net)
 * - Get<T> type-checked lookup (wrong type -> nullptr)
 * - Contains / Remove / Clear / Size
 * - Polymorphic save/load round-trip (Camera + Mesh + Light + MeshData via
 *   cereal JSON archives), including UID preservation and post-load counter
 *   bump (new objects never collide with restored IDs).
 *
 * Pure CPU -- no GPU required.
 */

#include <gtest/gtest.h>

#include <cereal/archives/json.hpp>
#include <sstream>

#include "core/ResourceManager.h"
#include "scene/Camera.h"
#include "scene/Light.h"
#include "scene/Mesh.h"
#include "asset/data/MeshData.h"
#include "render/shaders/RenderShader.h"
#include "render/shaders/Shader.h"
#include "render/shaders/ShaderUnit.h"

// Force-link the polymorphic registration TUs (static libs) so the pool's
// UID-derived types are registered for the JSON round-trip below.
#include "scene/registrations/TypeRegistration.h"
#include "asset/registrations/DataRegistration.h"
#include "render/registrations/ShaderRegistration.h"

using namespace neurus;

// -----------------------------------------------------------------------
// Load / Register semantics
// -----------------------------------------------------------------------

/**
 * @test Load<T> constructs, registers, and returns a typed shared_ptr.
 */
TEST(ResourceManagerTest, LoadConstructsAndRegisters)
{
	ResourceManager pool;
	EXPECT_EQ(pool.Size(), 0u);

	auto cam = pool.Load<Camera>();
	ASSERT_NE(cam, nullptr);
	EXPECT_EQ(pool.Size(), 1u);
	EXPECT_TRUE(pool.Contains(cam->GetObjectID()));
	EXPECT_EQ(pool.Get<UID>(cam->GetObjectID()), cam);
}

/**
 * @test Load<T> with a data-resource type (UID-derived) registers it.
 *
 * Load<MeshData>(path) constructs the resource; with an empty path the
 * resource stays an identity shell (no content) but IS registered.
 */
TEST(ResourceManagerTest, LoadDataResource)
{
	ResourceManager pool;
	auto meshData = pool.Load<MeshData>("");
	ASSERT_NE(meshData, nullptr);
	EXPECT_EQ(pool.Size(), 1u);
	EXPECT_EQ(meshData->GetVertexCount(), 0u); // empty shell, no content
}

/**
 * @test Register throws on a duplicate UID (safety net).
 */
TEST(ResourceManagerTest, DuplicateRegisterThrows)
{
	ResourceManager pool;
	auto cam = pool.Load<Camera>();
	EXPECT_THROW(pool.Register(cam), std::runtime_error);
}

/**
 * @test Get<T> with the wrong type returns nullptr; base types resolve.
 */
TEST(ResourceManagerTest, GetTypeChecked)
{
	ResourceManager pool;
	auto cam = pool.Load<Camera>();

	EXPECT_NE(pool.Get<Camera>(cam->GetObjectID()), nullptr);
	EXPECT_NE(pool.Get<ObjectID>(cam->GetObjectID()), nullptr);
	EXPECT_NE(pool.Get<UID>(cam->GetObjectID()), nullptr);
	EXPECT_EQ(pool.Get<Mesh>(cam->GetObjectID()), nullptr); // wrong type
	EXPECT_EQ(pool.Get<Camera>(123456), nullptr);           // missing ID
}

/**
 * @test Contains / Remove / Clear / Size.
 */
TEST(ResourceManagerTest, RemoveClearSize)
{
	ResourceManager pool;
	auto cam = pool.Load<Camera>();
	auto light = pool.Load<Light>(POINTLIGHT, 5.0f, glm::vec3(1.0f));
	EXPECT_EQ(pool.Size(), 2u);

	EXPECT_TRUE(pool.Remove(cam->GetObjectID()));
	EXPECT_EQ(pool.Size(), 1u);
	EXPECT_FALSE(pool.Contains(cam->GetObjectID()));
	EXPECT_FALSE(pool.Remove(cam->GetObjectID())); // already gone

	pool.Clear();
	EXPECT_EQ(pool.Size(), 0u);
	EXPECT_FALSE(pool.Contains(light->GetObjectID()));
}

/**
 * @test ForEach<T> visits only pooled objects of the requested type.
 */
TEST(ResourceManagerTest, ForEachVisitsTypedObjects)
{
	ResourceManager pool;
	auto cam = pool.Load<Camera>();
	auto mesh = pool.Load<Mesh>(pool.Load<MeshData>());
	auto light = pool.Load<Light>(POINTLIGHT, 5.0f, glm::vec3(1.0f));

	int meshes = 0, cameras = 0;
	pool.ForEach<Mesh>([&](const std::shared_ptr<Mesh>& m) { ++meshes; EXPECT_EQ(m, mesh); });
	pool.ForEach<Camera>([&](const std::shared_ptr<Camera>& c) { ++cameras; EXPECT_EQ(c, cam); });

	EXPECT_EQ(meshes, 1);
	EXPECT_EQ(cameras, 1);
}

// -----------------------------------------------------------------------
// Polymorphic save/load round-trip
// -----------------------------------------------------------------------

/**
 * @test A pool containing Camera + MeshData + Mesh + Light round-trips
 *       through cereal JSON archives: type, IDs, and per-type fields survive.
 */
TEST(ResourceManagerTest, PoolSaveLoadRoundtrip)
{
	ResourceManager pool;

	auto cam = pool.Load<Camera>();
	cam->o_name = "Cam1";
	cam->cam_pers = 42.0f;

	auto meshData = pool.Load<MeshData>("obj/sphere.obj");
	auto mesh = pool.Load<Mesh>(meshData);
	mesh->o_name = "Mesh1";
	mesh->using_shadow = false;

	auto light = pool.Load<Light>(POINTLIGHT, 7.0f, glm::vec3(1.0f, 0.5f, 0.2f));
	light->o_name = "Light1";

	EXPECT_EQ(pool.Size(), 4u);

	// --- Save ---
	std::stringstream ss;
	{
		cereal::JSONOutputArchive ar(ss);
		ar(pool);
	}

	// --- Load ---
	ResourceManager loaded;
	{
		std::stringstream is(ss.str());
		cereal::JSONInputArchive ar(is);
		ar(loaded);
	}

	EXPECT_EQ(loaded.Size(), 4u);

	// Type + identity preserved, and new IDs are allocated past restored ones.
	auto cam2 = loaded.Get<Camera>(cam->GetObjectID());
	ASSERT_NE(cam2, nullptr);
	EXPECT_EQ(cam2->o_name, "Cam1");
	EXPECT_FLOAT_EQ(cam2->cam_pers, 42.0f);

	auto mesh2 = loaded.Get<Mesh>(mesh->GetObjectID());
	ASSERT_NE(mesh2, nullptr);
	EXPECT_EQ(mesh2->o_name, "Mesh1");
	EXPECT_FALSE(mesh2->using_shadow);
	// The data-resource ID reference survives the round-trip. The o_mesh
	// pointer is wired by Scene::ResolveDataReferences (Scene-level), not by
	// the pool's own deserialization, so it stays null in a raw pool test.
	EXPECT_EQ(mesh2->o_meshDataId, meshData->GetObjectID());

	auto light2 = loaded.Get<Light>(light->GetObjectID());
	ASSERT_NE(light2, nullptr);
	EXPECT_EQ(light2->o_name, "Light1");
	EXPECT_EQ(light2->light_type, POINTLIGHT);
	EXPECT_FLOAT_EQ(light2->light_power, 7.0f);
	EXPECT_EQ(light2->light_color, glm::vec3(1.0f, 0.5f, 0.2f));

	// Wrong-type lookups stay null after the round-trip.
	EXPECT_EQ(loaded.Get<Mesh>(cam->GetObjectID()), nullptr);

	// UID::serialize bumps s_count past restored IDs: fresh allocations never
	// collide with the loaded ones.
	auto fresh = loaded.Load<Camera>();
	EXPECT_GT(fresh->GetObjectID(), cam->GetObjectID());
	EXPECT_NE(fresh->GetObjectID(), cam->GetObjectID());
	EXPECT_NE(fresh->GetObjectID(), mesh->GetObjectID());
}

/**
 * @test A pooled RenderShader (the per-mesh "created shader") round-trips
 *       through the pool. Serialization stores only the source paths, but
 *       RenderShader::serialize(load) re-parses content from disk AND
 *       recompiles to SPIR-V (CompileToSpv), so the loaded shader is ready for
 *       GeometryPass's per-mesh pipeline without any post-load recompile step.
 *
 *       Regression: "[GeometryPass] Per-mesh pipeline creation failed" after
 *       create shader → save project → reopen app (SPIR-V was lost on load).
 */
TEST(ResourceManagerTest, PooledShaderRecompilesAfterLoad)
{
	ResourceManager pool;

	// Mirrors Editor::OnCreateShader: pooled RenderShader + parse + compile.
	auto shader = pool.Load<RenderShader>(
		"MeshShader_10", "res/shaders/render/gbuffer.vert", "res/shaders/render/gbuffer.frag");
	ASSERT_NE(shader, nullptr);
	ASSERT_TRUE(shader->ParseAndGenerate());
	ASSERT_TRUE(shader->CompileToSpv());
	ASSERT_FALSE(shader->GetStage(ShaderType::VERTEX).spv.empty());
	ASSERT_FALSE(shader->GetStage(ShaderType::FRAGMENT).spv.empty());

	// --- Save (the "save and quit" step) ---
	std::stringstream ss;
	{
		cereal::JSONOutputArchive ar(ss);
		ar(pool);
	}

	// --- Load (the "reopen the app" step) ---
	ResourceManager loaded;
	{
		std::stringstream is(ss.str());
		cereal::JSONInputArchive ar(is);
		ar(loaded);
	}

	// The load lifecycle restores derived state: serialize(load) re-parses the
	// stages and recompiles to SPIR-V, so GeometryPass can build the pipeline
	// on the first frame (no per-frame "pipeline creation failed" fallback).
	auto shader2 = loaded.Get<RenderShader>(shader->GetObjectID());
	ASSERT_NE(shader2, nullptr);
	EXPECT_FALSE(shader2->GetStage(ShaderType::VERTEX).spv.empty());
	EXPECT_FALSE(shader2->GetStage(ShaderType::FRAGMENT).spv.empty());
	EXPECT_GT(shader2->GetVersion(), 0);
}
