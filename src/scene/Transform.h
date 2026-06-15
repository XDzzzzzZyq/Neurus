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
 * - Right-handed coordinate system (OpenGL convention)
 * - Euler rotation order: pitch (X), yaw (Y), roll (Z)
 * - Rotation angles stored in degrees, converted to radians at compute time
 *
 * Model Matrix Composition:
 * - Model = T(position) * Rx(pitch) * Ry(yaw) * Rz(roll) * S(scale)
 * - Applied to vertex v: T * (Rx * (Ry * (Rz * (S * v))))
 * - Order: scale, roll (Z), yaw (Y), pitch (X), translate
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
	 * @brief Returns a typed pointer to this Transform3D.
	 * @return Non-owning pointer to this Transform3D instance.
	 */
	Transform3D* GetTransformPtr() override { return this; }

	// -----------------------------------------------------------------------
	// Setters — mark cached matrix dirty
	// -----------------------------------------------------------------------

	/**
	 * @brief Sets the world position.
	 * @param pos New position in world space.
	 * @note Marks the cached model matrix as dirty.
	 */
	void SetPosition(const glm::vec3& pos)
	{
		m_position = pos;
		m_dirty = true;
	}

	/**
	 * @brief Sets the rotation as Euler angles.
	 * @param degrees Rotation in degrees (pitch=X, yaw=Y, roll=Z).
	 * @note Marks the cached model matrix as dirty.
	 */
	void SetRotation(const glm::vec3& degrees)
	{
		m_rotation = degrees;
		m_dirty = true;
	}

	/**
	 * @brief Sets the local scale.
	 * @param scale Per-axis scale factors.
	 * @note Marks the cached model matrix as dirty.
	 */
	void SetScale(const glm::vec3& scale)
	{
		m_scale = scale;
		m_dirty = true;
	}

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
	 * @return Const reference to the rotation vector (pitch, yaw, roll).
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
	 * Rotation is applied in XYZ order (pitch, yaw, roll).
	 * Result is cached until a component changes (dirty flag).
	 *
	 * @return 4x4 model matrix.
	 */
	glm::mat4 GetModelMatrix() const
	{
		if (!m_dirty)
		{
			return m_cachedMatrix;
		}

		glm::mat4 mat{1.0f};
		const glm::vec3 rad = glm::radians(m_rotation);

		// TRS: Translate * Rotate * Scale
		mat = glm::translate(mat, m_position);
		mat = glm::rotate(mat, rad.x, glm::vec3(1.0f, 0.0f, 0.0f)); // Pitch (X)
		mat = glm::rotate(mat, rad.y, glm::vec3(0.0f, 1.0f, 0.0f)); // Yaw   (Y)
		mat = glm::rotate(mat, rad.z, glm::vec3(0.0f, 0.0f, 1.0f)); // Roll  (Z)
		mat = glm::scale(mat, m_scale);

		m_cachedMatrix = mat;
		m_dirty = false;
		return m_cachedMatrix;
	}

	/**
	 * @brief Computes the normal matrix for transforming surface normals.
	 *
	 * The normal matrix is the inverse transpose of the upper-left 3x3
	 * portion of the model matrix. This ensures correct normal orientation
	 * under non-uniform scaling.
	 *
	 * @return 3x3 normal matrix.
	 */
	glm::mat3 GetNormalMatrix() const
	{
		return glm::transpose(glm::inverse(glm::mat3(GetModelMatrix())));
	}

	/**
	 * @brief Forces the cached model matrix to be recomputed on next access.
	 */
	void Invalidate() { m_dirty = true; }

private:
	glm::vec3 m_position{0.0f};    ///< World position.
	glm::vec3 m_rotation{0.0f};    ///< Euler rotation in degrees (pitch, yaw, roll).
	glm::vec3 m_scale{1.0f};       ///< Per-axis scale.

	mutable bool m_dirty{true};        ///< True if cached matrix needs recomputation.
	mutable glm::mat4 m_cachedMatrix{1.0f}; ///< Cached model matrix.
};

} // namespace neurus
