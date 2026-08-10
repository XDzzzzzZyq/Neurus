/**
 * @file ControllerContext.h
 * @brief Bundle of the three controller-facing interfaces + editor singleton access.
 *
 * Controllers depend ONLY on this context: they subscribe/enqueue/emitNow
 * events through IEventQueue, resolve pooled objects by integer id through
 * IResourceLookup, and record undoable operations through IOperationSink —
 * never on the concrete EventQueue, ResourceManager, or OperationManager.
 *
 * The context additionally carries providers for the two Editor-owned
 * singletons that are NOT pooled UID objects:
 *   - scene:  the current Scene (re-queried per use, because New/Load swaps it)
 *   - config: the live RenderConfig to mutate (stable Editor member)
 *
 * Ownership/lifetime rules:
 * - The context is constructed by the Editor once and outlives the
 *   controllers' event subscriptions (declared before the controller list).
 * - Controllers MUST NOT store the context or any of its members; handler
 *   lambdas may capture it by reference (it outlives the EventQueue, whose
 *   stored lambdas reference it).
 */

#pragma once

#include <functional>

#include "core/IResourceLookup.h"
#include "editor/events/IEventQueue.h"
#include "editor/operations/IOperationSink.h"

namespace neurus {

class Scene;
class RenderConfig;

/**
 * @brief The three controller-facing interfaces plus editor singleton access.
 */
struct ControllerContext
{
	/** @brief Typed event dispatch: subscribe / enqueue / emitNow. */
	IEventQueue& events;

	/** @brief Read-only fetch/lookup of pooled UID objects by integer id. */
	IResourceLookup& resources;

	/** @brief Sink for recording undoable operations at mutation time. */
	IOperationSink& ops;

	/** @brief Returns the current Scene (re-queried per use; may swap on New/Load). */
	std::function<Scene*()> scene;

	/** @brief Returns the live Editor-owned RenderConfig to mutate. */
	std::function<RenderConfig*()> config;
};

} // namespace neurus
