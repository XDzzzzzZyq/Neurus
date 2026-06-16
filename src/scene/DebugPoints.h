/**
 * @file DebugPoints.h
 * @brief Debug point primitive for viewport visualization.
 *
 * DebugPoints provides CPU-side storage for debug point primitives used
 * in viewport rendering. Each DebugPoints instance stores a collection of
 * point positions, along with rendering properties (point type, color,
 * scale, opacity, projection mode).
 *
 * Architecture:
 * - Inherits ObjectID for scene graph identity and type discrimination
 * - Inherits Transform3D for world-space placement
 * - Point storage via std::vector<glm::vec3> — no GPU resources
 * - PointType enum for choosing point sprite shape
 * - PushDebugPoint/PushDebugPoints for adding point data
 *
 * @note No GPU resources or shaders — pure CPU data container.
 */

#pragma once

#include <vector>

#include "UID.h"
#include "Transform.h"

namespace neurus
{

/**
 * @brief Debug point primitive for viewport visualization.
 *
 * Stores point positions with rendering attributes including point
 * type (SQUARE, RHOMBUS, CIR, CUBE), color, scale, opacity, and
 * projection mode. All data is CPU-side; GPU upload is handled by
 * the renderer layer.
 *
 * Object type: GO_DP
 *
 * Usage:
 * @code
 *   DebugPoints dp;
 *   dp.PushDebugPoint({0,0,0});            // single point
 *   dp.PushDebugPoints({...});              // multiple points
 *   dp.SetPointType(DebugPoints::CIR);     // circle sprites
 *   dp.SetColor({1,0,0,1});               // red color
 *   dp.SetScale(3.0f);
 * @endcode
 */
class DebugPoints : public ObjectID, public Transform3D
{
public:
	/**
	 * @brief Point sprite type enum.
	 *
	 * Determines the visual shape of each rendered point.
	 */
	enum class PointType
	{
		SQUARE,   ///< Axis-aligned square sprite.
		RHOMBUS,  ///< Diamond / rotated square sprite.
		CIR,      ///< Circular sprite.
		CUBE      ///< 3D cube sprite (volumetric).
	};

	/**
	 * @brief Constructs a DebugPoints with default properties.
	 *
	 * Defaults:
	 * - PointType: SQUARE
	 * - Color: white (1,1,1,1)
	 * - Scale: 1.0
	 * - Opacity: 1.0
	 * - ProjectionMode: 0
	 * - o_type: GO_DP
	 */
	DebugPoints();

	/**
	 * @brief Destroys the DebugPoints.
	 */
	~DebugPoints() override = default;

	// Non-copyable (UID semantics)
	DebugPoints(const DebugPoints&) = delete;
	DebugPoints& operator=(const DebugPoints&) = delete;

	// Movable
	DebugPoints(DebugPoints&&) = default;
	DebugPoints& operator=(DebugPoints&&) = default;

	// -----------------------------------------------------------------------
	// Point management
	// -----------------------------------------------------------------------

	/**
	 * @brief Adds a single debug point at the given position.
	 * @param point World-space position of the point.
	 */
	void PushDebugPoint(const glm::vec3& point);

	/**
	 * @brief Appends multiple debug points.
	 * @param points Vector of world-space positions.
	 */
	void PushDebugPoints(const std::vector<glm::vec3>& points);

	/**
	 * @brief Removes all stored points.
	 */
	void ClearPoints();

	/**
	 * @brief Returns the number of stored points.
	 * @return Point count.
	 */
	int GetPointCount() const { return static_cast<int>(m_points.size()); }

	/**
	 * @brief Returns a const reference to the point storage.
	 * @return Const reference to vector of point positions.
	 */
	const std::vector<glm::vec3>& GetPoints() const { return m_points; }

	// -----------------------------------------------------------------------
	// Properties
	// -----------------------------------------------------------------------

	/** @brief Sets the point sprite type. */
	void SetPointType(PointType type) { m_pointType = type; }
	/** @brief Returns the point sprite type. */
	PointType GetPointType() const { return m_pointType; }

	/** @brief Sets the point color. */
	void SetColor(const glm::vec4& color) { m_color = color; }
	/** @brief Returns the point color. */
	const glm::vec4& GetColor() const { return m_color; }

	/** @brief Sets the point scale factor. */
	void SetScale(float scale) { m_scale = scale; }
	/** @brief Returns the point scale factor. */
	float GetScale() const { return m_scale; }

	/** @brief Sets the opacity (0.0 = transparent, 1.0 = opaque). */
	void SetOpacity(float opacity) { m_opacity = opacity; }
	/** @brief Returns the opacity value. */
	float GetOpacity() const { return m_opacity; }

	/** @brief Sets the projection mode (0 = screen, 1 = world, etc.). */
	void SetProjectionMode(int mode) { m_projectionMode = mode; }
	/** @brief Returns the projection mode. */
	int GetProjectionMode() const { return m_projectionMode; }

private:
	PointType m_pointType{PointType::SQUARE};    ///< Point sprite shape.
	glm::vec4 m_color{1.0f, 1.0f, 1.0f, 1.0f};  ///< Point RGBA color.
	float m_scale{1.0f};                          ///< Point size scale.
	float m_opacity{1.0f};                        ///< Opacity (0-1).
	int m_projectionMode{0};                      ///< Projection mode identifier.

	std::vector<glm::vec3> m_points;              ///< Point positions.
};

} // namespace neurus
