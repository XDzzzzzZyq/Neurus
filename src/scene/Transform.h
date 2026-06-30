/**
 * @file Transform.h
 * @brief Transform components for 3D spatial representation.
 *
 * Provides Transform base class and Transform3D implementation for
 * position/rotation/scale (TRS) matrix computation. Supports dirty-flag
 * caching for deferred matrix recomputation.
 *
 * Architecture:
 * - Transform is abstract base with virtual GetTransformPtr() for polymorphic access
 * - Transform3D provides Euler-angle 3D transforms using glm::mat4
 * - Dirty flag caches the model matrix until a component changes
 *
 * Transform Composition:
 * - Model matrix = Translate * RotateX * RotateY * RotateZ * Scale
 * - Normal matrix = inverse-transpose of upper-left 3x3 model matrix
 *
 * @note All scene objects with spatial placement should inherit Transform3D.
 * @note Transforms are not thread-safe. Access from main thread only.
 */

#pragma once

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

#include <cereal/cereal.hpp>

#include "GlmSerialization.h"

namespace neurus {

/**
 * @brief Abstract base class for transform components.
 *
 * Provides the polymorphic interface for spatial transforms.
 * Derived classes implement dimension-specific matrix math.
 */
class Transform
{
public:
	virtual ~Transform() = default;

	/**
	 * @brief Returns a pointer to this transform for polymorphic access.
	 * @return Non-owning pointer to this Transform instance.
	 */
	virtual Transform* GetTransformPtr() { return this; }

	// Non-copyable (RAII resource ownership semantics)
	Transform(const Transform&) = delete;
	Transform& operator=(const Transform&) = delete;

	// Movable
	Transform(Transform&&) = default;
	Transform& operator=(Transform&&) = default;

protected:
	Transform() = default;
};

/**
 * @brief 3D transform component with TRS (translate/rotate/scale) matrix computation.
 *
 * Transform3D manages position, rotation (Euler angles in degrees), and scale
 * for 3D scene objects. The model matrix is computed lazily and cached via a
 * dirty flag, avoiding redundant matrix multiplications.
 *
 * Coordinate System:
 * - Right-handed coordinate system, Z-up
 * - X = right, Y = forward, Z = up
 * - Euler rotation order: pitch (X), yaw (Z), roll (Y)
 * - Rotation angles stored in degrees, converted to radians at compute time
 *
 * Model Matrix Composition:
 * - Model = T(position) * Rz(yaw) * Rx(pitch) * Ry(roll) * S(scale)
 * - Applied to vertex v: T * (Rz * (Rx * (Ry * (S * v))))
 * - Order: scale, roll (Y), pitch (X), yaw (Z), translate
 *
 * Forward Direction:
 * - Local forward = +Y (0, 1, 0). GetDirection() rotates to world-space.
 * - Up = +Z (0, 0, 1). Yaw rotates around Z.
 */
class Transform3D : public Transform
{
public:
	/**
	 * @brief Constructs a Transform3D with identity values.
	 */
	Transform3D() = default;

	/**
	 * @brief Destroys the Transform3D.
	 */
	~Transform3D() override = default;

	/**
	 * @brief Cereal serialization for TRS transform.
	 * @tparam Archive Cereal archive type (input or output).
	 * @param ar Archive to serialize to/from.
	 * @note Only position/rotation/scale are serialized. Cached matrix
	 *       and dirty flag are computed values, not persisted.
	 */
	template<class Archive>
	void serialize(Archive& ar)
	{
		ar(CEREAL_NVP(m_position), CEREAL_NVP(m_rotation), CEREAL_NVP(m_scale));
	}

	/**
	 * @brief Returns a typed pointer to this Transform3D.
	 * @return Non-owning pointer to this Transform3D instance.
	 */
	Transform3D* GetTransformPtr() override { return this; }

	// -----------------------------------------------------------------------
	// Setters - mark cached matrix dirty
	// -----------------------------------------------------------------------

	/**
	 * @brief Sets the world position.
	 * @param pos New position in world space.
	 * @note Marks the cached model matrix as dirty.
	 */
	void SetPosition(const glm::vec3& pos);

	/**
	 * @brief Sets the rotation as Euler angles.
	 * @param degrees Rotation in degrees (pitch=X, yaw=Z, roll=Y).
	 * @note Marks the cached model matrix as dirty.
	 */
	void SetRotation(const glm::vec3& degrees);

	/**
	 * @brief Sets the local scale.
	 * @param scale Per-axis scale factors.
	 * @note Marks the cached model matrix as dirty.
	 */
	void SetScale(const glm::vec3& scale);

	// -----------------------------------------------------------------------
	// Getters
	// -----------------------------------------------------------------------

	/**
	 * @brief Returns the current position.
	 * @return Const reference to the position vector.
	 */
	const glm::vec3& GetPosition() const { return m_position; }

	/**
	 * @brief Returns the current rotation in degrees.
	 * @return Const reference to the rotation vector (pitch=X, yaw=Z, roll=Y).
	 */
	const glm::vec3& GetRotation() const { return m_rotation; }

	/**
	 * @brief Returns the current scale.
	 * @return Const reference to the scale vector.
	 */
	const glm::vec3& GetScale() const { return m_scale; }

	// -----------------------------------------------------------------------
	// Matrix computation
	// -----------------------------------------------------------------------

	/**
	 * @brief Computes and returns the model matrix (TRS).
	 *
	 * Constructs the matrix as: Translate * Rotate * Scale.
	 * Rotation is applied in ZXY order (yaw=Z, pitch=X, roll=Y).
	 * Result is cached until a component changes (dirty flag).
	 *
	 * @return 4x4 model matrix.
	 */
	glm::mat4 GetModelMatrix() const;

	/**
	 * @brief Computes the local-space forward direction vector.
	 *
	 * Rotates the forward vector (0,1,0) by the current Euler rotation
	 * (yaw=Z, pitch=X, roll=Y). Returns a normalized direction vector.
	 * When no rotation is applied, returns (0,1,0) (+Y forward).
	 *
	 * @return Normalized forward direction vector.
	 */
	glm::vec3 GetDirection() const;

	/**
	 * @brief Computes the normal matrix for transforming surface normals.
	 *
	 * The normal matrix is the inverse transpose of the upper-left 3x3
	 * portion of the model matrix. This ensures correct normal orientation
	 * under non-uniform scaling.
	 *
	 * @return 3x3 normal matrix.
	 */
	glm::mat3 GetNormalMatrix() const;

	/**
	 * @brief Forces the cached model matrix to be recomputed on next access.
	 */
	void Invalidate() { m_dirty = true; }

private:
	glm::vec3 m_position{0.0f};    ///< World position.
	glm::vec3 m_rotation{0.0f};    ///< Euler rotation in degrees (pitch=X, yaw=Z, roll=Y).
	glm::vec3 m_scale{1.0f};       ///< Per-axis scale.

	mutable bool m_dirty{true};        ///< True if cached matrix needs recomputation.
	mutable glm::mat4 m_cachedMatrix{1.0f}; ///< Cached model matrix.
};

} // namespace neurus
