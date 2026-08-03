/**
 * @file EditorEvents.h
 * @brief Cross-component editor events (pure data, no Qt, no Vulkan).
 *
 * Only events that involve a DIFFERENT component than the emitter live here:
 * - RenderResetEvent  (Renderer temporal accumulation)
 * - EnvironmentChanged (Renderer IBL regeneration)
 * - SceneModified     (SceneController -> Editor dirty flag)
 * - LightGpuChanged   (SceneController -> Editor: single light SSBO struct)
 * - LightingRebuild   (SceneController -> Editor: full light SSBO dict)
 *
 * Scene-domain events live in SceneEvents.h; asset add/import events live in
 * AssetEvents.h.
 */

#pragma once

namespace neurus {

class ObjectID;

/**
 * @brief Emitted when the scene state changes in a way that invalidates
 *  temporal accumulation. Subscribe to this to reset any per-frame history
 *  (shadow accumulation, SSAO temporal, SSR temporal, etc.).
 */
struct RenderResetEvent
{
};

/**
 * @brief Emitted when the active IBL environment is loaded or changed.
 *
 * The Editor subscribes to regenerate diffuse/specular cubemaps via
 * GenerateIBL(). The event carries the IDs needed to locate the Environment
 * object in the Scene's env_list pool.
 */
struct EnvironmentChanged
{
	int sceneId = -1; ///< ID of the Scene containing the Environment
	int envId   = -1; ///< ID of the Environment object (from UID::GetObjectID())
};

/**
 * @brief Emitted by SceneController after any scene mutation that constitutes
 *  a project change (transform, property, visibility, object add).
 *  Editor subscribes and marks the project dirty. Selection and camera
 *  navigation do NOT emit this.
 */
struct SceneModified
{
};

/**
 * @brief Emitted by SceneController when a single light's GPU SSBO struct must
 *  be updated (power/radius/cutoff/outerCutoff changes).
 *  Editor subscribes, casts object to Light*, and calls
 *  UploadLighting(*light) + RenderCache::UpdateLight(id, struct).
 */
struct LightGpuChanged
{
	const ObjectID* object = nullptr; ///< The changed light (Light*).
};

/**
 * @brief Emitted by SceneController when the full light SSBO dict must be
 *  rebuilt (light transform, visibility, shadow toggle).
 *  Editor subscribes and calls UploadLighting().
 */
struct LightingRebuild
{
};

} // namespace neurus
