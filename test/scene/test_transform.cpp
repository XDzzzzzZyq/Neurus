/**
 * @file test_transform.cpp
 * @brief Unit tests for Transform base and Transform3D classes.
 *
 * TDD: RED (test written first) → GREEN (implementation verified).
 * All tests are pure CPU math — no GPU required.
 */

#include <gtest/gtest.h>

#include "scene/Transform.h"

using namespace neurus;

// -----------------------------------------------------------------------
// Transform base class
// -----------------------------------------------------------------------

/**
 * @brief Test that Transform base provides virtual GetTransformPtr().
 */
TEST(Transform, GetTransformPtr_ReturnsPolymorphicPointer)
{
	Transform3D t3d;
	Transform* base = &t3d;

	// Call through base pointer — must not return nullptr
	Transform* ptr = base->GetTransformPtr();
	EXPECT_NE(ptr, nullptr);
	// Dynamic cast to verify it's actually a Transform3D
	EXPECT_NE(dynamic_cast<Transform3D*>(ptr), nullptr);
}

/**
 * @brief Test that Transform is non-copyable (RAII semantics).
 */
TEST(Transform, NonCopyable)
{
	EXPECT_FALSE(std::is_copy_constructible_v<Transform>);
	EXPECT_FALSE(std::is_copy_assignable_v<Transform>);
	EXPECT_FALSE(std::is_copy_constructible_v<Transform3D>);
	EXPECT_FALSE(std::is_copy_assignable_v<Transform3D>);
}

// -----------------------------------------------------------------------
// Transform3D — Identity
// -----------------------------------------------------------------------

/**
 * @brief Default-constructed Transform3D produces identity matrix.
 */
TEST(Transform3D, IdentityMatrix)
{
	Transform3D t;
	glm::mat4 model = t.GetModelMatrix();
	EXPECT_EQ(model, glm::mat4(1.0f));
}

// -----------------------------------------------------------------------
// Transform3D — Translation
// -----------------------------------------------------------------------

/**
 * @brief Translation-only transform produces correct translation matrix.
 */
TEST(Transform3D, TranslateOnly)
{
	Transform3D t;
	t.SetPosition(glm::vec3(5.0f, 10.0f, -3.0f));

	glm::mat4 model = t.GetModelMatrix();

	// Translation is stored in column 3 (the w column) in column-major
	// M[3][0] = x, M[3][1] = y, M[3][2] = z
	EXPECT_FLOAT_EQ(model[3][0], 5.0f);
	EXPECT_FLOAT_EQ(model[3][1], 10.0f);
	EXPECT_FLOAT_EQ(model[3][2], -3.0f);

	// Rotation/scale part (upper-left 3x3) should remain identity
	EXPECT_EQ(glm::mat3(model), glm::mat3(1.0f));
}

// -----------------------------------------------------------------------
// Transform3D — Rotation (90 degrees around X axis)
// -----------------------------------------------------------------------

/**
 * @brief Rotation of 90 degrees around X produces correct rotation matrix.
 *
 * Rx(90°) =
 * [1,  0,  0, 0]
 * [0,  0, -1, 0]
 * [0,  1,  0, 0]
 * [0,  0,  0, 1]
 */
TEST(Transform3D, Rotate90X)
{
	Transform3D t;
	t.SetRotation(glm::vec3(90.0f, 0.0f, 0.0f));

	glm::mat4 model = t.GetModelMatrix();

	// Column 0 (X axis) — unchanged
	EXPECT_FLOAT_EQ(model[0][0], 1.0f);
	EXPECT_NEAR(model[0][1], 0.0f, 1e-6f);
	EXPECT_NEAR(model[0][2], 0.0f, 1e-6f);

	// Column 1 (Y axis) — rotates to Z: (0, 0, 1)
	EXPECT_NEAR(model[1][0], 0.0f, 1e-6f);
	EXPECT_NEAR(model[1][1], 0.0f, 1e-6f);
	EXPECT_FLOAT_EQ(model[1][2], 1.0f);

	// Column 2 (Z axis) — rotates to -Y: (0, -1, 0)
	EXPECT_NEAR(model[2][0], 0.0f, 1e-6f);
	EXPECT_FLOAT_EQ(model[2][1], -1.0f);
	EXPECT_NEAR(model[2][2], 0.0f, 1e-6f);
}

// -----------------------------------------------------------------------
// Transform3D — Rotation (90 degrees around Y axis)
// -----------------------------------------------------------------------

/**
 * @brief Rotation of 90 degrees around Y produces correct rotation matrix.
 *
 * Ry(90°) =
 * [ 0, 0, 1, 0]
 * [ 0, 1, 0, 0]
 * [-1, 0, 0, 0]
 * [ 0, 0, 0, 1]
 */
TEST(Transform3D, Rotate90Y)
{
	Transform3D t;
	t.SetRotation(glm::vec3(0.0f, 90.0f, 0.0f));

	glm::mat4 model = t.GetModelMatrix();

	// Column 0 (X axis) — rotates to -Z: (0, 0, -1)
	EXPECT_NEAR(model[0][0], 0.0f, 1e-6f);
	EXPECT_NEAR(model[0][1], 0.0f, 1e-6f);
	EXPECT_FLOAT_EQ(model[0][2], -1.0f);

	// Column 1 (Y axis) — unchanged
	EXPECT_NEAR(model[1][0], 0.0f, 1e-6f);
	EXPECT_FLOAT_EQ(model[1][1], 1.0f);
	EXPECT_NEAR(model[1][2], 0.0f, 1e-6f);

	// Column 2 (Z axis) — rotates to X: (1, 0, 0)
	EXPECT_FLOAT_EQ(model[2][0], 1.0f);
	EXPECT_NEAR(model[2][1], 0.0f, 1e-6f);
	EXPECT_NEAR(model[2][2], 0.0f, 1e-6f);
}

// -----------------------------------------------------------------------
// Transform3D — Scale (uniform)
// -----------------------------------------------------------------------

/**
 * @brief Uniform scale produces correctly scaled diagonal matrix.
 */
TEST(Transform3D, ScaleUniform)
{
	Transform3D t;
	t.SetScale(glm::vec3(2.0f));

	glm::mat4 model = t.GetModelMatrix();

	EXPECT_FLOAT_EQ(model[0][0], 2.0f);
	EXPECT_FLOAT_EQ(model[1][1], 2.0f);
	EXPECT_FLOAT_EQ(model[2][2], 2.0f);
	EXPECT_FLOAT_EQ(model[3][3], 1.0f); // w-component always 1
}

// -----------------------------------------------------------------------
// Transform3D — Scale (non-uniform)
// -----------------------------------------------------------------------

/**
 * @brief Non-uniform scale factors are correctly reflected in the matrix.
 */
TEST(Transform3D, ScaleNonUniform)
{
	Transform3D t;
	t.SetScale(glm::vec3(2.0f, 0.5f, 3.0f));

	glm::mat4 model = t.GetModelMatrix();

	EXPECT_FLOAT_EQ(model[0][0], 2.0f);
	EXPECT_FLOAT_EQ(model[1][1], 0.5f);
	EXPECT_FLOAT_EQ(model[2][2], 3.0f);
}

// -----------------------------------------------------------------------
// Transform3D — Normal matrix
// -----------------------------------------------------------------------

/**
 * @brief Normal matrix matches inverse-transpose of upper-left 3x3 model matrix.
 */
TEST(Transform3D, NormalMatrix)
{
	Transform3D t;
	t.SetPosition(glm::vec3(1.0f, 2.0f, 3.0f));
	t.SetRotation(glm::vec3(45.0f, 30.0f, 15.0f));
	t.SetScale(glm::vec3(2.0f, 1.0f, 3.0f));

	glm::mat3 normalMat = t.GetNormalMatrix();
	glm::mat3 expected = glm::transpose(glm::inverse(glm::mat3(t.GetModelMatrix())));

	for (int col = 0; col < 3; ++col)
	{
		for (int row = 0; row < 3; ++row)
		{
			EXPECT_FLOAT_EQ(normalMat[col][row], expected[col][row]);
		}
	}
}

// -----------------------------------------------------------------------
// Transform3D — Dirty flag / cached matrix
// -----------------------------------------------------------------------

/**
 * @brief Cached matrix is reused when no components have changed.
 */
TEST(Transform3D, Dirty_CachesMatrixOnNoChange)
{
	Transform3D t;
	t.SetPosition(glm::vec3(1.0f, 2.0f, 3.0f));

	glm::mat4 first = t.GetModelMatrix();
	glm::mat4 second = t.GetModelMatrix();

	// Without modifying anything, same matrix returned from cache
	EXPECT_EQ(first, second);
}

/**
 * @brief Modifying a transform component recomputes the cached matrix.
 */
TEST(Transform3D, Dirty_RecomputesOnPositionChange)
{
	Transform3D t;
	t.SetPosition(glm::vec3(1.0f, 2.0f, 3.0f));

	glm::mat4 first = t.GetModelMatrix();

	t.SetPosition(glm::vec3(4.0f, 5.0f, 6.0f));
	glm::mat4 third = t.GetModelMatrix();

	// Different position → different matrix
	EXPECT_NE(first, third);

	// Verify new translation
	EXPECT_FLOAT_EQ(third[3][0], 4.0f);
	EXPECT_FLOAT_EQ(third[3][1], 5.0f);
	EXPECT_FLOAT_EQ(third[3][2], 6.0f);
}

/**
 * @brief Invalidate() forces recomputation on next GetModelMatrix() call.
 */
TEST(Transform3D, Dirty_InvalidateForcesRecompute)
{
	Transform3D t;
	t.SetPosition(glm::vec3(7.0f, 8.0f, 9.0f));
	glm::mat4 first = t.GetModelMatrix();

	// Modify without calling a setter
	t.Invalidate();
	glm::mat4 second = t.GetModelMatrix();

	// Without actual data change, the result should be the same
	// (dirty flag causes recomputation, but inputs are unchanged)
	EXPECT_EQ(first, second);

	// Now change and invalidate
	t.SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
	t.Invalidate();
	glm::mat4 third = t.GetModelMatrix();

	EXPECT_NE(first, third);
	EXPECT_EQ(third, glm::mat4(1.0f));
}

// -----------------------------------------------------------------------
// Transform3D — Combined TRS
// -----------------------------------------------------------------------

/**
 * @brief Full TRS composition: translate, rotate, and scale combined.
 */
TEST(Transform3D, FullTRS)
{
	Transform3D t;
	t.SetPosition(glm::vec3(10.0f, 20.0f, 30.0f));
	t.SetRotation(glm::vec3(0.0f, 90.0f, 0.0f)); // Yaw 90°
	t.SetScale(glm::vec3(2.0f));

	glm::mat4 model = t.GetModelMatrix();

	// After Ry(90°) * S(2), X axis becomes Z: [0, 0, -2]
	EXPECT_NEAR(model[0][0], 0.0f, 1e-6f);
	EXPECT_NEAR(model[0][1], 0.0f, 1e-6f);
	EXPECT_FLOAT_EQ(model[0][2], -2.0f);

	// Y axis unchanged: [0, 2, 0]
	EXPECT_NEAR(model[1][0], 0.0f, 1e-6f);
	EXPECT_FLOAT_EQ(model[1][1], 2.0f);
	EXPECT_NEAR(model[1][2], 0.0f, 1e-6f);

	// Z axis becomes X: [2, 0, 0]
	EXPECT_FLOAT_EQ(model[2][0], 2.0f);
	EXPECT_NEAR(model[2][1], 0.0f, 1e-6f);
	EXPECT_NEAR(model[2][2], 0.0f, 1e-6f);

	// Translation: [10, 20, 30]
	EXPECT_FLOAT_EQ(model[3][0], 10.0f);
	EXPECT_FLOAT_EQ(model[3][1], 20.0f);
	EXPECT_FLOAT_EQ(model[3][2], 30.0f);
}
