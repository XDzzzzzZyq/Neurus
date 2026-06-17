/**
 * @file DefaultScene.h
 * @brief Default scene factory function.
 *
 * Provides CreateDefaultScene() which constructs a pre-configured Scene
 * with one camera, one sphere mesh, and one point light. Follows the
 * OpenGL SceneConfigs factory pattern (SceneManager::Main).
 *
 * This function will be replaced by Project::Open("res/default.neurus.json")
 * in Phase 4.6 (serialization).
 */

#pragma once

#include <memory>
#include <string>

#include "Scene.h"

namespace neurus {

/**
 * @brief Creates the default scene: 1 camera, 1 sphere mesh, 1 point light.
 *
 * Factory function that constructs and populates a Scene with:
 *   - Camera: pos(0,2,5), target(0,0,0), FOV 60°, near 0.1, far 100
 *   - Mesh:   Loaded from the given OBJ path, default PBR material
 *   - Light:  Point light, pos(3,3,3), white, power 10, radius 0.05
 *
 * @param objPath Absolute or relative path to the sphere.obj file.
 * @return Shared pointer to the fully populated Scene instance.
 *
 * @note Returns shared_ptr (not value) because Scene inherits from UID,
 *       which is non-copyable and non-movable.
 */
std::shared_ptr<Scene> CreateDefaultScene(const std::string& objPath);

} // namespace neurus
