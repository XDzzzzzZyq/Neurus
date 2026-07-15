#pragma once

#include <string>

namespace neurus {

/** @brief Emitted when a mesh file is selected for import (Edit → Add → Mesh...). */
struct MeshImportEvent { std::string path; };

/** @brief Emitted when a new camera should be added to the scene (Edit → Add → Camera). */
struct CameraAddEvent {};

/** @brief Emitted when a new point light should be added to the scene (Edit → Add → Light). */
struct LightAddEvent {};

/** @brief Emitted when a new sun light should be added to the scene (Edit → Add → Sun Light). */
struct SunLightAddEvent {};

} // namespace neurus
