/**
 * @file Outliner.h
 * @brief Outliner dock panel displaying scene object hierarchy in a QTreeView.
 *
 * The Outliner reads from a const Scene reference and builds a flat tree
 * grouped by object type (Cameras, Meshes, Lights). Clicking an object emits
 * ObjectSelected via EventBus for Editor↔Renderer selection propagation.
 *
 * Architecture:
 * - QTreeView + QStandardItemModel (simpler than QAbstractItemModel for MVP)
 * - Reads scene.cam_list, scene.mesh_list, scene.light_list
 * - Click → EventBus().enqueue(ObjectSelected{id})
 * - No DnD, no inline editing, no hierarchy (flat lists per category)
 *
 * @note UI Layer - communicates via EventBus (typed events, no Qt dependency).
 */

#pragma once

#include "UIPanel.h"

class QTreeView;
class QStandardItemModel;

namespace neurus
{

class Scene;

class Outliner : public UIPanel
{
	Q_OBJECT

public:
	static constexpr PanelType kType = PanelType::Outliner;

	explicit Outliner(QWidget* parent = nullptr);
	~Outliner() override = default;

	/**
	 * @brief Refreshes the panel from a UIContext snapshot.
	 *
	 * Currently a no-op — the Outliner rebuilds via Refresh(const Scene&)
	 * on project load/change events rather than per-frame.
	 *
	 * @param ctx Read-only UI context (unused).
	 */
	void Refresh(const UIContext& ctx) override;

	/**
	 * @brief Rebuilds the tree view from the given scene's object pools.
	 *
	 * Clears existing items and repopulates with current cameras, meshes,
	 * and lights from the scene. Categories with no objects are hidden.
	 *
	 * @param scene Scene to read object pools from (const, read-only).
	 */
	void Refresh(const Scene& scene);

private slots:
	/**
	 * @brief Handles tree view item click.
	 *
	 * Extracts the object ID from Qt::UserRole+1 and emits the
	 * objectSelected signal, which Application wires to EventBus.
	 *
	 * @param index Model index of the clicked item.
	 */
	void OnItemClicked(const QModelIndex& index);

signals:
	/** @brief Emitted when a user clicks on a scene object in the outliner. */
	void objectSelected(int objectId);

private:
	QTreeView*         m_treeView = nullptr;
	QStandardItemModel* m_model    = nullptr;

	static constexpr int ObjectIdRole   = Qt::UserRole + 1;
	static constexpr int ObjectTypeRole = Qt::UserRole + 2;
};

} // namespace neurus
