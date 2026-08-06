/**
 * @file test_shader_editor_panel.cpp
 * @brief Offscreen integration test: real ShaderEditorPanel + ShaderController
 *        + OperationManager through the user's undo/redo scenarios.
 *
 * Unlike the controller-only tests, these instantiate the ACTUAL
 * ShaderEditorPanel and drive it through the same event path the app uses
 * (panel Qt signal -> EventQueue -> ShaderController -> op record -> Undo ->
 * version bump -> panel Refresh repopulates the tree). The "+" buttons are
 * clicked programmatically via the tree's index widgets, and field edits go
 * through ShaderStructModel::setData exactly as the delegate delivers them.
 *
 * Scenarios covered (matching the reported bug):
 *   - Add fields to two different sections, undo both, panel reflects both.
 *   - Add member fields to two different struct definitions, undo both.
 *   - Rename a field via the tree model, undo, panel reflects the old name.
 */

#include <gtest/gtest.h>

#include <QAbstractItemModel>
#include <QPushButton>
#include <QTreeView>

#include "editor/controllers/ShaderController.h"
#include "editor/events/EventBus.h"
#include "editor/events/ShaderEvents.h"
#include "editor/operations/OperationManager.h"
#include "render/shaders/RenderShader.h"
#include "render/shaders/Shader.h"
#include "render/shaders/ShaderUnit.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"
#include "ui/UIContext.h"
#include "ui/items/ShaderStructModel.h"
#include "ui/panels/ShaderEditorPanel.h"

using namespace neurus;

namespace {

/** @brief Seeds a realistic vertex IR (one attribute, one uniform, two structs). */
void SeedRealisticIR(ShaderStruct& parsed)
{
	parsed.AB_list.push_back(
		S_IO{ 0, "aPosition", ParaType::VEC3, "vec3", Interp::Smooth });

	parsed.pass_list.push_back(
		S_IO{ 0, "fragColor", ParaType::VEC4, "vec4", Interp::Smooth });

	parsed.uniform_list.push_back(
		S_Uniform{ "uTime", ParaType::FLOAT, 1, 0, "", "float", "" });

	parsed.struct_def_list.push_back(
		S_StructDef{ 0, "LightData", {
			S_IO{ 0, "position", ParaType::VEC3, "vec3", Interp::Smooth },
			S_IO{ 1, "color",    ParaType::VEC3, "vec3", Interp::Smooth },
		}, "lights" });

	parsed.struct_def_list.push_back(
		S_StructDef{ 1, "Material", {
			S_IO{ 0, "albedo",    ParaType::VEC3, "vec3", Interp::Smooth },
			S_IO{ 1, "roughness", ParaType::FLOAT, "float", Interp::Smooth },
		}, "mat" });
}

/** @brief Locates a top-level section row by its display title. */
QModelIndex SectionRow(QAbstractItemModel* model, const QString& title)
{
	for (int row = 0; row < model->rowCount(QModelIndex()); ++row)
	{
		QModelIndex idx = model->index(row, 0, QModelIndex());
		if (idx.data(Qt::DisplayRole).toString() == title)
			return idx;
	}
	return QModelIndex();
}

/** @brief Clicks the "+" button installed on a section / struct-def row. */
void ClickAddButton(QTreeView* tree, const QModelIndex& idx)
{
	QWidget* widget = tree->indexWidget(idx);
	ASSERT_NE(widget, nullptr) << "row has no + button";
	QPushButton* btn = widget->findChild<QPushButton*>();
	ASSERT_NE(btn, nullptr) << "row widget has no button";
	btn->click();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class ShaderEditorPanelTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_controller.Init(m_bus, m_operations);

		m_mesh = std::make_shared<Mesh>();
		auto shader = std::make_shared<RenderShader>("TestShader", "v.vert", "f.frag");
		shader->GetStage(ShaderType::VERTEX).code = "VERT_V0";
		shader->GetStage(ShaderType::FRAGMENT).code = "FRAG_V0";
		SeedRealisticIR(shader->GetStage(ShaderType::VERTEX).parsed);
		m_mesh->o_shader = shader;
		m_scene.UseMesh(m_mesh);
		m_scene.selections.Select(m_mesh.get(), /*increment=*/false);

		m_ctx.editor.scene = &m_scene;

		// Wire the panel exactly like Application::ConnectUIEvent: Qt signal
		// -> EventQueue (deferred to the next Process()).
		QObject::connect(&m_panel, &ShaderEditorPanel::fieldAdded,
		                 [this](const ShaderFieldAdded& e) { m_bus.enqueue(e); });
		QObject::connect(&m_panel, &ShaderEditorPanel::structEdited,
		                 [this](const ShaderStructEdited& e) { m_bus.enqueue(e); });
	}

	/** @brief Drains queued events (one "frame" of Editor::Edit()). */
	void Process() { m_bus.Process(); }

	/** @brief One frame of UIManager::Refresh() — repopulates the tree. */
	void RefreshPanel() { m_panel.Refresh(m_ctx); }

	/** @brief The live vertex ShaderUnit for assertions. */
	ShaderUnit& VertexUnit() { return m_mesh->o_shader->GetStage(ShaderType::VERTEX); }

	EventQueue m_bus;
	Scene m_scene;
	OperationManager m_operations{ m_bus, [this]() -> Scene* { return &m_scene; } };
	ShaderController m_controller;
	std::shared_ptr<Mesh> m_mesh;
	UIContext m_ctx;
	ShaderEditorPanel m_panel;
};

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------

TEST_F(ShaderEditorPanelTest, FieldAdd_TwoDifferentSections_UndoRefreshesTree)
{
	RefreshPanel();

	auto* tree = m_panel.findChild<QTreeView*>();
	ASSERT_NE(tree, nullptr);
	auto* model = static_cast<ShaderStructModel*>(tree->model());
	ASSERT_NE(model, nullptr);

	// Add one field to Attributes, then one to Uniforms (different sections).
	QModelIndex attrs = SectionRow(model, "Attributes");
	ASSERT_TRUE(attrs.isValid());
	ClickAddButton(tree, attrs);
	Process();
	RefreshPanel();

	QModelIndex unifs = SectionRow(model, "Uniforms");
	ASSERT_TRUE(unifs.isValid());
	ClickAddButton(tree, unifs);
	Process();
	RefreshPanel();

	ASSERT_EQ(VertexUnit().parsed.AB_list.size(), 2u);
	ASSERT_EQ(VertexUnit().parsed.uniform_list.size(), 2u);

	// Undo the uniform add — tree must show the attribute add still applied.
	m_operations.Undo();
	RefreshPanel();
	EXPECT_EQ(model->rowCount(SectionRow(model, "Uniforms")), 1);
	EXPECT_EQ(model->rowCount(SectionRow(model, "Attributes")), 2);

	// Undo the attribute add — tree back to the seeded state.
	m_operations.Undo();
	RefreshPanel();
	EXPECT_EQ(model->rowCount(SectionRow(model, "Attributes")), 1);
	EXPECT_FALSE(m_operations.CanUndo());
}

TEST_F(ShaderEditorPanelTest, AddButtons_NeverTakeKeyboardFocus)
{
	// The "+" buttons are click-only: they must never grab keyboard focus.
	// A focused "+" button is deleted on the next model rebuild, and the
	// dead-focus state on macOS swallows window shortcuts (Ctrl+Z) until the
	// user clicks elsewhere — the reported "undo not working after add" bug.
	RefreshPanel();

	auto* tree = m_panel.findChild<QTreeView*>();
	ASSERT_NE(tree, nullptr);
	auto* model = static_cast<ShaderStructModel*>(tree->model());
	ASSERT_NE(model, nullptr);

	// Every section row's "+" button must be NoFocus.
	for (int row = 0; row < model->rowCount(QModelIndex()); ++row)
	{
		QModelIndex sectionIdx = model->index(row, 0, QModelIndex());
		QWidget* widget = tree->indexWidget(sectionIdx);
		ASSERT_NE(widget, nullptr);
		QPushButton* btn = widget->findChild<QPushButton*>();
		ASSERT_NE(btn, nullptr);
		EXPECT_EQ(btn->focusPolicy(), Qt::NoFocus)
			<< "section '" << sectionIdx.data(Qt::DisplayRole).toString().toStdString()
			<< "' + button must not take focus";
	}
}

TEST_F(ShaderEditorPanelTest, FieldAdd_AttributesAndPassOutputs_UndoRefreshesTree)
{
	// The exact reported combo: one field into Attributes, one into Pass
	// Outputs — both S_IO containers.
	RefreshPanel();

	auto* tree = m_panel.findChild<QTreeView*>();
	ASSERT_NE(tree, nullptr);
	auto* model = static_cast<ShaderStructModel*>(tree->model());
	ASSERT_NE(model, nullptr);

	QModelIndex attrs = SectionRow(model, "Attributes");
	ASSERT_TRUE(attrs.isValid());
	ClickAddButton(tree, attrs);
	Process();
	RefreshPanel();

	QModelIndex pass = SectionRow(model, "Pass Outputs");
	ASSERT_TRUE(pass.isValid());
	ClickAddButton(tree, pass);
	Process();
	RefreshPanel();

	ASSERT_EQ(VertexUnit().parsed.AB_list.size(), 2u);
	ASSERT_EQ(VertexUnit().parsed.pass_list.size(), 2u);

	// Undo the pass-output add first (LIFO).
	m_operations.Undo();
	RefreshPanel();
	EXPECT_EQ(VertexUnit().parsed.pass_list.size(), 1u);
	EXPECT_EQ(model->rowCount(SectionRow(model, "Pass Outputs")), 1);
	EXPECT_EQ(model->rowCount(SectionRow(model, "Attributes")), 2);

	// Then the attribute add.
	m_operations.Undo();
	RefreshPanel();
	EXPECT_EQ(VertexUnit().parsed.AB_list.size(), 1u);
	EXPECT_EQ(model->rowCount(SectionRow(model, "Attributes")), 1);
	EXPECT_FALSE(m_operations.CanUndo());
}

TEST_F(ShaderEditorPanelTest, FieldAdd_60FpsFramePattern_UndoRefreshesTree)
{
	// Faithful app frame pattern: the panel refreshes EVERY frame (early-
	// returning when the version cache matches), clicks land between frames,
	// and Process() precedes Refresh() within a frame. Two adds to different
	// sections, then two undos — the tree must track the IR at every step.
	RefreshPanel();

	auto* tree = m_panel.findChild<QTreeView*>();
	ASSERT_NE(tree, nullptr);
	auto* model = static_cast<ShaderStructModel*>(tree->model());
	ASSERT_NE(model, nullptr);

	// Click "+" on Attributes (deferred: applied next frame).
	ClickAddButton(tree, SectionRow(model, "Attributes"));
	RefreshPanel(); // frame N: nothing processed yet — repopulate is a no-op visually
	Process();
	RefreshPanel(); // frame N+1: add applied + repopulated

	// Click "+" on Pass Outputs.
	ClickAddButton(tree, SectionRow(model, "Pass Outputs"));
	RefreshPanel();
	Process();
	RefreshPanel();

	ASSERT_EQ(VertexUnit().parsed.AB_list.size(), 2u);
	ASSERT_EQ(VertexUnit().parsed.pass_list.size(), 2u);

	// Two undos, one per frame, exactly as Ctrl+Z delivers them.
	m_operations.Undo();
	RefreshPanel();
	EXPECT_EQ(model->rowCount(SectionRow(model, "Pass Outputs")), 1);
	EXPECT_EQ(model->rowCount(SectionRow(model, "Attributes")), 2);

	m_operations.Undo();
	RefreshPanel();
	EXPECT_EQ(model->rowCount(SectionRow(model, "Attributes")), 1);
	EXPECT_FALSE(m_operations.CanUndo());

	// Redo restores both in order.
	m_operations.Redo();
	RefreshPanel();
	m_operations.Redo();
	RefreshPanel();
	EXPECT_EQ(VertexUnit().parsed.AB_list.size(), 2u);
	EXPECT_EQ(VertexUnit().parsed.pass_list.size(), 2u);
}

TEST_F(ShaderEditorPanelTest, FieldAdd_TwoStructDefs_UndoRefreshesTree)
{
	RefreshPanel();

	auto* tree = m_panel.findChild<QTreeView*>();
	ASSERT_NE(tree, nullptr);
	auto* model = static_cast<ShaderStructModel*>(tree->model());
	ASSERT_NE(model, nullptr);

	// Add a member field to struct 0, then one to struct 1.
	QModelIndex sd = SectionRow(model, "Struct Definitions");
	ASSERT_TRUE(sd.isValid());
	ClickAddButton(tree, model->index(0, 0, sd));
	Process();
	RefreshPanel();

	sd = SectionRow(model, "Struct Definitions");
	ASSERT_TRUE(sd.isValid());
	ClickAddButton(tree, model->index(1, 0, sd));
	Process();
	RefreshPanel();

	ASSERT_EQ(VertexUnit().parsed.struct_def_list[0].fields.size(), 3u);
	ASSERT_EQ(VertexUnit().parsed.struct_def_list[1].fields.size(), 3u);

	// Undo the struct-1 member add, then the struct-0 one.
	m_operations.Undo();
	RefreshPanel();
	EXPECT_EQ(VertexUnit().parsed.struct_def_list[1].fields.size(), 2u);
	sd = SectionRow(model, "Struct Definitions");
	EXPECT_EQ(model->rowCount(model->index(1, 0, sd)), 2);

	m_operations.Undo();
	RefreshPanel();
	EXPECT_EQ(VertexUnit().parsed.struct_def_list[0].fields.size(), 2u);
	sd = SectionRow(model, "Struct Definitions");
	EXPECT_EQ(model->rowCount(model->index(0, 0, sd)), 2);
	EXPECT_FALSE(m_operations.CanUndo());
}

TEST_F(ShaderEditorPanelTest, StructEdit_RenameViaTree_UndoRefreshesTree)
{
	RefreshPanel();

	auto* tree = m_panel.findChild<QTreeView*>();
	ASSERT_NE(tree, nullptr);
	auto* model = static_cast<ShaderStructModel*>(tree->model());
	ASSERT_NE(model, nullptr);

	// Rename the first attribute via the same setData path the delegate uses.
	QModelIndex attrs = SectionRow(model, "Attributes");
	ASSERT_TRUE(attrs.isValid());
	ASSERT_TRUE(model->setData(model->index(0, 1, attrs), "renamed", Qt::EditRole));
	Process();
	RefreshPanel();

	EXPECT_EQ(VertexUnit().parsed.AB_list[0].name, "renamed");
	attrs = SectionRow(model, "Attributes");
	EXPECT_EQ(model->index(0, 1, attrs).data(Qt::DisplayRole).toString().toStdString(),
	          "renamed");

	// Undo — tree must show the original name again.
	m_operations.Undo();
	RefreshPanel();
	EXPECT_EQ(VertexUnit().parsed.AB_list[0].name, "aPosition");
	attrs = SectionRow(model, "Attributes");
	EXPECT_EQ(model->index(0, 1, attrs).data(Qt::DisplayRole).toString().toStdString(),
	          "aPosition");
	EXPECT_FALSE(m_operations.CanUndo());
}
