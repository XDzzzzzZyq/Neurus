/**
 * @file SelectionController.h
 * @brief Click-select via raycast — pure logic controller for object selection.
 *
 * SelectionController tracks a set of selected object IDs and provides
 * raycast-based click-select through scene mesh objects. It emits
 * ObjectSelected / ObjectDeselected events via EventBus on selection changes.
 *
 * Architecture:
 * - Pure logic class — no Qt, no Vulkan, no viewport dependency
 * - Owned by EditorContext
 * - Consumes Scene (read-only mesh iteraction) and Camera (for unproject)
 * - Emits typed events via EventBus singleton for Editor↔Renderer communication
 *
 * Selection Modes:
 * - Click-select: Raycast from screen coords through scene meshes (sphere test)
 * - Box-select: Stub — full implementation in Phase 6 with viewport
 *
 * @note Editor Layer — owned by EditorContext, no viewport/rendering dependency.
 * @note Thread-safety: Not thread-safe. Must be used from main thread only.
 */

#pragma once

#include <set>
#include <vector>

#include <glm/glm.hpp>

namespace neurus
{

class Scene;
class Camera;

/**
 * @brief Pure-logic selection controller with raycast-based click-select.
 *
 * Manages a set of selected object IDs and provides raycasting from screen
 * coordinates through scene mesh objects. Emits events via EventBus when
 * selection changes so that UI and Renderer can respond.
 *
 * Usage:
 * @code
 *   SelectionController sel;
 *   int hitId = sel.RaycastSelect(scene, camera, mouseX, mouseY, w, h);
 *   if (hitId >= 0) sel.Select(hitId);
 * @endcode
 *
 * @note Box-select is a stub — returns empty vector until Phase 6.
 */
class SelectionController
{
public:
	SelectionController() = default;
	~SelectionController() = default;

	// --- Core selection API ---

	/**
	 * @brief Selects a single object by ID.
	 *
	 * Replaces current selection with the given object. If objectId is already
	 * selected, it becomes the active object (moved to front). Emits
	 * ObjectSelected via EventBus.
	 *
	 * @param objectId Unique ID of scene object to select (-1 ignored).
	 */
	void Select(int objectId);

	/**
	 * @brief Deselects an object by ID.
	 *
	 * Removes objectId from the selection set. Emits ObjectDeselected via
	 * EventBus if the object was previously selected.
	 *
	 * @param objectId Unique ID of scene object to deselect (-1 ignored).
	 */
	void Deselect(int objectId);

	/**
	 * @brief Clears all selections.
	 *
	 * Emits ObjectDeselected for each previously selected object via EventBus.
	 */
	void ClearSelection();

	/**
	 * @brief Returns the current selection set.
	 * @return Const reference to set of selected object IDs.
	 */
	const std::set<int>& GetSelection() const;

	/**
	 * @brief Checks if an object is currently selected.
	 * @param objectId Object ID to check.
	 * @return true if objectId is in selection set.
	 */
	bool IsSelected(int objectId) const;

	/**
	 * @brief Returns the active (last selected) object ID.
	 * @return Active object ID, or -1 if nothing selected.
	 */
	int GetActiveObject() const;

	// --- Raycast API ---

	/**
	 * @brief Performs a click-select raycast through scene mesh objects.
	 *
	 * Converts screen coordinates to a world-space ray using the camera's
	 * view/projection matrices, then tests each mesh in the scene for
	 * bounding-sphere intersection. Returns the closest hit's object ID.
	 *
	 * @param scene       Scene containing mesh objects to test against.
	 * @param camera      Active camera for unprojection.
	 * @param screenX     Mouse X in screen pixels (0 = left).
	 * @param screenY     Mouse Y in screen pixels (0 = top).
	 * @param screenWidth  Viewport width in pixels.
	 * @param screenHeight Viewport height in pixels.
	 * @return Object ID of the closest hit, or -1 if nothing hit.
	 *
	 * @note Uses a bounding-sphere test with a default 1.0f radius per object.
	 *       Per-object bounding data may be added in a later phase.
	 */
	int RaycastSelect(
		const Scene& scene,
		Camera& camera,
		float screenX, float screenY,
		int screenWidth, int screenHeight) const;

	/**
	 * @brief Box-select: selects all objects within a screen-space rectangle.
	 *
	 * Stub — full implementation deferred to Phase 6 (viewport integration).
	 *
	 * @param scene       Scene containing mesh objects to test against.
	 * @param camera      Active camera for frustum construction.
	 * @param x0          Left edge of selection box in screen pixels.
	 * @param y0          Top edge of selection box in screen pixels.
	 * @param x1          Right edge of selection box in screen pixels.
	 * @param y1          Bottom edge of selection box in screen pixels.
	 * @param screenWidth  Viewport width in pixels.
	 * @param screenHeight Viewport height in pixels.
	 * @return Empty vector (stub).
	 */
	std::vector<int> BoxSelect(
		const Scene& scene,
		Camera& camera,
		float x0, float y0, float x1, float y1,
		int screenWidth, int screenHeight) const;

private:
	/// Selected object IDs, ordered by insertion.
	std::set<int> m_selection;

	/// Active (last selected) object ID, -1 if no selection.
	int m_activeId = -1;

	// --- Internal helpers ---

	/**
	 * @brief Represents a world-space ray for intersection testing.
	 */
	struct Ray
	{
		glm::vec3 origin;
		glm::vec3 direction;
	};

	/**
	 * @brief Converts screen coordinates to a world-space ray.
	 * @param camera       Active camera for unprojection.
	 * @param screenX      X in screen pixels.
	 * @param screenY      Y in screen pixels.
	 * @param screenWidth  Viewport width in pixels.
	 * @param screenHeight Viewport height in pixels.
	 * @return World-space ray from camera position through unprojected point.
	 */
	static Ray ScreenToRay(
		Camera& camera,
		float screenX, float screenY,
		int screenWidth, int screenHeight);

	/**
	 * @brief Sphere-ray intersection test.
	 * @param ray    World-space ray.
	 * @param center Sphere center in world space.
	 * @param radius Sphere radius.
	 * @param tOut   Output: distance along ray to nearest intersection, if hit.
	 * @return true if ray intersects the sphere.
	 */
	static bool RayIntersectsSphere(
		const Ray& ray,
		const glm::vec3& center,
		float radius,
		float& tOut);
};

} // namespace neurus
