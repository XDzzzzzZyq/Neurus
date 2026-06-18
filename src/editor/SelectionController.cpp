/**
 * @file SelectionController.cpp
 * @brief Implementation of SelectionController - click-select via raycast.
 */

#include "SelectionController.h"

#include <cmath>

#include <glm/gtc/matrix_inverse.hpp>

#include "events/EditorEvents.h"
#include "events/EventBus.h"
#include "scene/Camera.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"

namespace neurus
{

// ---------------------------------------------------------------------------
// Core selection API
// ---------------------------------------------------------------------------

void SelectionController::Select(int objectId)
{
	if (objectId < 0) return;

	// If already selected and active, nothing to do
	if (m_activeId == objectId && m_selection.contains(objectId))
	{
		return;
	}

	// If already in selection, just promote to active
	if (m_selection.contains(objectId))
	{
		m_activeId = objectId;
		return;
	}

	// Single-select mode: clear previous selection, emit deselect events
	std::set<int> previous;
	previous.swap(m_selection);

	for (int id : previous)
	{
		EventBus().enqueue(ObjectDeselected{id});
	}

	m_selection.insert(objectId);
	m_activeId = objectId;

	EventBus().enqueue(ObjectSelected{objectId});
}

void SelectionController::Deselect(int objectId)
{
	if (objectId < 0) return;

	auto it = m_selection.find(objectId);
	if (it == m_selection.end()) return;

	m_selection.erase(it);

	if (m_activeId == objectId)
	{
		// Active becomes the last remaining, or -1 if empty
		m_activeId = m_selection.empty() ? -1 : *m_selection.rbegin();
	}

	// Emit event
	EventBus().enqueue(ObjectDeselected{objectId});
}

void SelectionController::ClearSelection()
{
	std::set<int> toRemove;
	toRemove.swap(m_selection);

	m_activeId = -1;

	// Emit deselect events for every previously selected object
	for (int id : toRemove)
	{
		EventBus().enqueue(ObjectDeselected{id});
	}
}

const std::set<int>& SelectionController::GetSelection() const
{
	return m_selection;
}

bool SelectionController::IsSelected(int objectId) const
{
	return m_selection.contains(objectId);
}

int SelectionController::GetActiveObject() const
{
	return m_activeId;
}

// ---------------------------------------------------------------------------
// Raycast
// ---------------------------------------------------------------------------

SelectionController::Ray SelectionController::ScreenToRay(
	Camera& camera,
	float screenX, float screenY,
	int screenWidth, int screenHeight)
{
	// Convert screen coordinates to NDC [-1, 1]
	// screenY=0 is top, NDC y=1 is top
	const float ndcX = (2.0f * screenX) / static_cast<float>(screenWidth) - 1.0f;
	const float ndcY = 1.0f - (2.0f * screenY) / static_cast<float>(screenHeight);

	const glm::mat4 view = camera.GetViewMatrix();
	const glm::mat4 proj = camera.GetProjectionMatrix();
	const glm::mat4 invVP = glm::inverse(proj * view);

	// Unproject near plane and far plane
	const glm::vec4 nearPoint = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
	const glm::vec4 farPoint  = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);

	const glm::vec3 nearWorld = glm::vec3(nearPoint) / nearPoint.w;
	const glm::vec3 farWorld  = glm::vec3(farPoint) / farPoint.w;

	Ray ray;
	ray.origin = camera.GetPosition();
	ray.direction = glm::normalize(farWorld - nearWorld);
	return ray;
}

bool SelectionController::RayIntersectsSphere(
	const Ray& ray,
	const glm::vec3& center,
	float radius,
	float& tOut)
{
	const glm::vec3 L = center - ray.origin;
	const float tca = glm::dot(L, ray.direction);

	// Sphere center is behind the ray origin
	if (tca < 0.0f) return false;

	const float d2 = glm::dot(L, L) - tca * tca;
	const float r2 = radius * radius;

	// Ray misses the sphere
	if (d2 > r2) return false;

	const float thc = std::sqrt(r2 - d2);
	tOut = tca - thc; // nearest intersection
	return true;
}

int SelectionController::RaycastSelect(
	const Scene& scene,
	Camera& camera,
	float screenX, float screenY,
	int screenWidth, int screenHeight) const
{
	const Ray ray = ScreenToRay(camera, screenX, screenY, screenWidth, screenHeight);

	int closestId = -1;
	float closestT = std::numeric_limits<float>::max();

	// Default bounding-sphere radius per object (can be refined with per-object data later)
	constexpr float kDefaultRadius = 1.0f;

	for (const auto& [id, mesh] : scene.mesh_list)
	{
		if (!mesh) continue;

		const glm::vec3 pos = mesh->GetPosition();

		float t = 0.0f;
		if (RayIntersectsSphere(ray, pos, kDefaultRadius, t))
		{
			if (t < closestT)
			{
				closestT = t;
				closestId = id;
			}
		}
	}

	return closestId;
}

// ---------------------------------------------------------------------------
// Box-select (stub)
// ---------------------------------------------------------------------------

std::vector<int> SelectionController::BoxSelect(
	const Scene& /*scene*/,
	Camera& /*camera*/,
	float /*x0*/, float /*y0*/, float /*x1*/, float /*y1*/,
	int /*screenWidth*/, int /*screenHeight*/) const
{
	// Stub - full implementation deferred to Phase 6 (viewport integration)
	return {};
}

} // namespace neurus
