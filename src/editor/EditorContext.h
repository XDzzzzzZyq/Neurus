#pragma once

#include <QObject>

namespace neurus {

/**
 * @brief Container for editor and scene state.
 *
 * Provides immutable scene data to the Renderer layer and mutable
 * editor state (selections, active tool, etc.) to the UI layer.
 *
 * For the Triangle MVP, this is a stub — no scene graph exists yet.
 */
class EditorContext : public QObject
{
	Q_OBJECT

public:
	explicit EditorContext(QObject* parent = nullptr);
	~EditorContext() override = default;

	// Prevent copies
	EditorContext(const EditorContext&) = delete;
	EditorContext& operator=(const EditorContext&) = delete;

	// --- Scene state (future) ---
	// const Scene& activeScene() const;

	// --- Editor state (future) ---
	// SelectionManager& selection();

signals:
	/** @brief Emitted when the scene state changes. UI should refresh. */
	void sceneChanged();

	/** @brief Emitted when the editor selection changes. */
	void selectionChanged();
};

} // namespace neurus
