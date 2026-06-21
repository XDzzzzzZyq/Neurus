/**
 * @file OutlinerPanel.cpp
 * @brief OutlinerPanel implementation - scene object tree view with EventBus selection.
 */

#include "OutlinerPanel.h"

#include "editor/events/EditorEvents.h"
#include "editor/events/EventBus.h"
#include "scene/Light.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"

#include <QHeaderView>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

namespace neurus
{

// =========================================================================
// Constructor
// =========================================================================

OutlinerPanel::OutlinerPanel(QWidget* parent)
	: QWidget(parent)
{
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	m_model = new QStandardItemModel(this);
	m_model->setHorizontalHeaderLabels({"Outliner"});

	m_treeView = new QTreeView(this);
	m_treeView->setModel(m_model);
	m_treeView->setHeaderHidden(true);
	m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
	m_treeView->setDragDropMode(QAbstractItemView::NoDragDrop);
	m_treeView->setRootIsDecorated(true);
	m_treeView->setAnimated(true);

	layout->addWidget(m_treeView);

	connect(m_treeView, &QTreeView::clicked,
		this, &OutlinerPanel::OnItemClicked);
}

// =========================================================================
// Refresh - rebuild tree from Scene
// =========================================================================

void OutlinerPanel::Refresh(const Scene& scene)
{
	m_model->clear();

	// Helper: add a non-selectable category header
	auto addCategory = [this](const QString& name) -> QStandardItem*
	{
		auto* cat = new QStandardItem(name);
		cat->setFlags(cat->flags() & ~Qt::ItemIsSelectable);
		cat->setEditable(false);
		m_model->invisibleRootItem()->appendRow(cat);
		return cat;
	};

	// Helper: add a selectable leaf item under a category
	auto addLeaf = [](QStandardItem* parent, const QString& label, int id)
	{
		auto* item = new QStandardItem(label);
		item->setData(id, Qt::UserRole + 1);
		item->setEditable(false);
		parent->appendRow(item);
	};

	// --- Cameras ---
	if (!scene.cam_list.empty())
	{
		auto* camCat = addCategory("Cameras");
		for (const auto& [id, cam] : scene.cam_list)
		{
			QString label = cam->o_name.empty()
				? QString("Camera #%1").arg(id)
				: QString::fromStdString(cam->o_name);
			addLeaf(camCat, label, id);
		}
	}

	// --- Meshes ---
	if (!scene.mesh_list.empty())
	{
		auto* meshCat = addCategory("Meshes");
		for (const auto& [id, mesh] : scene.mesh_list)
		{
			QString label = mesh->o_name.empty()
				? QString("Mesh #%1").arg(id)
				: QString::fromStdString(mesh->o_name);
			addLeaf(meshCat, label, id);
		}
	}

	// --- Lights ---
	if (!scene.light_list.empty())
	{
		auto* lightCat = addCategory("Lights");
		for (const auto& [id, light] : scene.light_list)
		{
			QString label = light->o_name.empty()
				? QString("Light #%1").arg(id)
				: QString::fromStdString(light->o_name);
			addLeaf(lightCat, label, id);
		}
	}

	m_treeView->expandAll();
}

// =========================================================================
// OnItemClicked - emit ObjectSelected via EventBus
// =========================================================================

void OutlinerPanel::OnItemClicked(const QModelIndex& index)
{
	if (!index.isValid())
		return;

	int objectId = index.data(Qt::UserRole + 1).toInt();
	if (objectId > 0)
	{
		EventQueue().enqueue(ObjectSelected{objectId});
	}
}

} // namespace neurus
