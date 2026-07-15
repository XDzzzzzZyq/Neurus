#pragma once

#include "render/RenderConfig.h"

namespace neurus {

/** @brief Emitted when the render configuration is changed from the UI panel. */
struct RenderConfigChangedEvent { RenderConfig config; };

} // namespace neurus
