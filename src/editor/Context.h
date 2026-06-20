/**
 * @file Context.h
 * @brief Context system providing explicit data flow across Renderer, Editor, and UI layers.
 *
 * The Context system enables decoupled communication by aggregating read-only views of
 * application state. It enforces architectural boundaries: Editor owns and mutates Context,
 * while Renderer and UI consume it immutably.
 *
 * Architecture:
 * - SceneContext: Owns active Scene* pointer, provides const accessors
 * - EditorContext: Owns SelectionManager, editor state, dirty tracking (QObject with signals)
 * - RenderContext: Non-owning RenderConfigs* pointer (stub - T45 creates RenderConfigs)
 * - Context: Composite aggregating all three; subscribes to EventQueue events
 */

#pragma once

#include <QObject>
#include <vector>

#include "SelectionManager.h"

namespace neurus {

// --- Forward declarations ---
class Scene;
class Camera;
class ObjectID;
class RenderConfigs;
class EventQueue;

// ===========================================================================
// SceneContext - read-only scene graph access
// ===========================================================================

/**
 * @brief Provides read-only access to the active scene and its objects.
 *
 * SceneContext is the primary interface for querying scene state. It returns
 * const pointers to enforce immutability from consumers (Renderer, UI).
 *
 * @note Thread-safety: Not thread-safe. Must be accessed from main thread only.
 */
class SceneContext
{
public:
	/**
	 * @brief Sets the active scene.
	 * @param scene Pointer to the scene to activate (non-owning).
	 */
	void UseScene(Scene* scene) { m_activeScene = scene; }

	/**
	 * @brief Returns the active scene.
	 * @return Const pointer to the active Scene, or nullptr if none set.
	 */
	const Scene* GetActiveScene() const { return m_activeScene; }

	/**
	 * @brief Returns the currently active camera.
	 * @return Const pointer to active Camera, or nullptr if none active.
	 */
	const Camera* GetActiveCamera() const;

	/**
	 * @brief Retrieves an object by its unique ID.
	 * @param id Unique object identifier.
	 * @return Const pointer to ObjectID, or nullptr if not found.
	 */
	const ObjectID* GetObjectID(int id) const;

	/**
	 * @brief Returns all renderable objects in the scene.
	 * @return Vector of const pointers to scene objects.
	 * @note Used by Renderer to iterate objects for draw calls.
	 */
	std::vector<const ObjectID*> GetObjectIDs() const;

private:
	Scene* m_activeScene = nullptr; ///< Non-owning pointer to active scene
};

// ===========================================================================
// EditorContext - editor state, selection, dirty tracking
// ===========================================================================

/**
 * @brief Stores editor-specific state (selections, dirty flag, etc.).
 *
 * EditorContext holds state that belongs to the Editor layer but not to the Scene.
 * It emits Qt signals for UI updates and exposes selections for highlight/display.
 *
 * Signals:
 *   - sceneChanged():     Emitted when the scene modification status changes.
 *   - selectionChanged(): Emitted when the editor selection changes.
 *
 * @note Ownership: Created and mutated by Editor layer.
 * @note QObject: Inherits QObject for Qt signal/slot mechanism.
 */
class EditorContext : public QObject
{
	Q_OBJECT

public:
	explicit EditorContext(QObject* parent = nullptr);
	~EditorContext() override = default;

	EditorContext(const EditorContext&) = delete;
	EditorContext& operator=(const EditorContext&) = delete;

	// --- Scene access (delegates to SceneContext) ---

	/**
	 * @brief Stores a pointer to the active scene.
	 * @param scene Pointer to the scene (delegates to SceneContext).
	 */
	void SetScene(Scene* scene);

	/**
	 * @brief Notifies that scene modification status changed.
	 * @param status Bitfield of Scene::SceneModifStatus flags.
	 * @note Enqueues SceneStatusChanged event on EventBus.
	 */
	void NotifySceneChanged(int status);

	/**
	 * @brief Returns the currently active scene.
	 * @return Const pointer to the active Scene, or nullptr if none set.
	 */
	const Scene* activeScene() const;

	// --- Selection state ---

	SelectionManager<ObjectID> selections; ///< Currently selected objects

	// --- Dirty tracking ---

	/** @brief Marks editor state as modified. */
	void MarkDirty() { m_dirty = true; }

	/** @brief Returns whether editor state has unsaved modifications. */
	bool IsDirty() const { return m_dirty; }

	/** @brief Clears the dirty flag (after save). */
	void ClearDirty() { m_dirty = false; }

signals:
	/** @brief Emitted when the scene state changes. UI should refresh. */
	void sceneChanged();

	/** @brief Emitted when the editor selection changes. */
	void selectionChanged();

private:
	friend class Context;
	SceneContext* m_sceneCtx = nullptr; ///< Back-pointer to owning Context's SceneContext
	bool m_dirty = false;               ///< Unsaved editor modifications flag
};

// ===========================================================================
// RenderContext - render configuration access
// ===========================================================================

/**
 * @brief Links to rendering configuration without owning it.
 *
 * RenderContext provides access to RenderConfigs via non-owning pointer.
 * This allows Renderer to read settings without coupling to config ownership.
 *
 * @note Ownership: RenderConfigs is owned externally (Editor or Application).
 * @note Stub: RenderConfigs will be implemented in Phase 5 T45.
 */
class RenderContext
{
public:
	/**
	 * @brief Sets the render configuration to use.
	 * @param config Non-owning pointer to RenderConfigs.
	 */
	void UseConfig(RenderConfigs* config) { m_config = config; }

	/**
	 * @brief Returns pointer to render configuration.
	 * @return Non-owning pointer to RenderConfigs, or nullptr if not set.
	 */
	RenderConfigs* GetConfig() const { return m_config; }

private:
	RenderConfigs* m_config = nullptr; ///< Non-owning pointer to render settings
};

// ===========================================================================
// Context - composite aggregating all sub-contexts
// ===========================================================================

/**
 * @brief Unified context container aggregating all context types.
 *
 * Context is the single source of truth for queryable application state.
 * It is passed across layers to enable decoupled data access:
 * - Editor mutates Context
 * - Renderer receives const Context& (read-only)
 * - UI reads Context for display
 *
 * @note Architecture: Context enforces one-way data flow and prevents hidden coupling.
 */
class Context
{
public:
	SceneContext scene{};   ///< Scene graph read-only view
	EditorContext editor;   ///< Editor state (selections, signals, dirty flag)
	RenderContext render{}; ///< Render settings accessor

public:
	/**
	 * @brief Constructs the context and subscribes to relevant events.
	 * @param pool EventQueue for subscribing to context-related events.
	 */
	explicit Context(EventQueue& pool);

	Context(const Context&) = delete;
	Context& operator=(const Context&) = delete;
};

} // namespace neurus
