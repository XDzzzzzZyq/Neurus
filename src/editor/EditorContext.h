#pragma once

/**
 * @file EditorContext.h
 * @brief Backward-compatibility header - includes the full Context system.
 *
 * EditorContext was originally a standalone QObject with scene/selection state.
 * It has been refactored into a three-part Context system:
 *   - SceneContext: active scene pointer + const accessors
 *   - EditorContext: QObject with signals, SelectionManager, dirty tracking
 *   - RenderContext: non-owning RenderConfigs* (stub)
 *
 * All three are aggregated in the composite Context class.
 */

#include "editor/Context.h"
