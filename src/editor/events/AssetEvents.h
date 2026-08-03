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

/** @brief Emitted when a new spot light should be added to the scene (Edit → Add → Spot Light). */
struct SpotLightAddEvent {};

/** @brief Emitted when the user requests undo (Edit → Undo / Ctrl+Z). */
struct UndoRequested {};

/** @brief Emitted when the user requests redo (Edit → Redo / Ctrl+Shift+Z). */
struct RedoRequested {};

} // namespace neurus
