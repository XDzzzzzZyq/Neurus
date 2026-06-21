/**
 * @file OutlinerPanel.h
 * @brief Outliner dock panel displaying scene object hierarchy in a QTreeView.
 *
 * The OutlinerPanel reads from a const Scene reference and builds a flat tree
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

#include <QWidget>

class QTreeView;
class QStandardItemModel;

namespace neurus
{

class Scene;

class OutlinerPanel : public QWidget
{
	Q_OBJECT

public:
	explicit OutlinerPanel(QWidget* parent = nullptr);
	~OutlinerPanel() override = default;

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
	 * Extracts the object ID from Qt::UserRole+1 and enqueues an
	 * ObjectSelected event via EventBus.
	 *
	 * @param index Model index of the clicked item.
	 */
	void OnItemClicked(const QModelIndex& index);

private:
	QTreeView*         m_treeView = nullptr;
	QStandardItemModel* m_model    = nullptr;

	static constexpr int ObjectIdRole   = Qt::UserRole + 1;
	static constexpr int ObjectTypeRole = Qt::UserRole + 2;
};

} // namespace neurus
