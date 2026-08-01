/**
 * @file ShaderEditorPanel.h
 * @brief Shader Editor dock panel -- Struct mode + Code mode.
 *
 * Provides a tree-based Struct Editor for editing per-mesh GLSL shaders.
 * A QStackedWidget switches between Code Editor and Struct Editor views.
 */
#pragma once

#include "UIPanel.h"
#include "editor/events/ShaderEvents.h"

#include <string>

class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTreeView;

namespace neurus { class CodeEditor; class ObjectID; class ShaderFieldDelegate; class ShaderStructModel; }

namespace neurus
{

class ShaderEditorPanel : public UIPanel
{
	Q_OBJECT

public:
	static constexpr PanelType kType = PanelType::ShaderEditor;

	explicit ShaderEditorPanel(QWidget* parent = nullptr);
	~ShaderEditorPanel() override = default;

	ShaderEditorPanel(const ShaderEditorPanel&) = delete;
	ShaderEditorPanel& operator=(const ShaderEditorPanel&) = delete;

	void Refresh(const UIContext& ctx) override;

signals:
	void createShaderRequested(const ShaderCreateRequested& e);
	void compileRequested(const ShaderCompileRequested& e);
	void codeEdited(const ShaderCodeEdited& e);
	void structEdited(const ShaderStructEdited& e);
	void fieldAdded(const ShaderFieldAdded& e);

private:
	/** @brief Populates the struct tree model and code editor from a ShaderUnit. */
	void populateSections(const void* shaderUnitPtr, bool objectChanged);

	/** @brief Resolves the target section/subField for the Add button from the current selection. */
	void handleAddEntry();

	/** @brief Shows the "Create Shader" button for objects without a shader. */
	void setShowCreateButton(bool show);

	/** @brief Shows the "No object selected" message. */
	void setShowEmptyState(bool show);

	// --- Top toolbar ---
	QComboBox*    m_modeCombo    = nullptr;  // "Code" | "Structure"
	QComboBox*    m_stageCombo   = nullptr;  // VERTEX | FRAGMENT
	QPushButton*  m_compileBtn   = nullptr;
	QPushButton*  m_saveBtn      = nullptr;

	// --- Content stack ---
	QStackedWidget* m_contentStack = nullptr;

	// --- Code Editor (Page 0) ---
	CodeEditor* m_codeEditor = nullptr;

	// --- Struct Editor (Page 1) ---
	QTreeView* m_treeView = nullptr;
	ShaderStructModel* m_model = nullptr;
	ShaderFieldDelegate* m_delegate = nullptr;
	QPushButton* m_addBtn = nullptr;
	QPushButton* m_removeBtn = nullptr;

	// --- Empty state / create button ---
	QLabel*      m_emptyLabel = nullptr;
	QPushButton* m_createBtn  = nullptr;

	// --- State ---
	const ObjectID* m_activeObject    = nullptr;
	int  m_cachedStageType      = 0;   // 0=VERTEX, 1=FRAGMENT
	int  m_cachedShaderVersion  = -1;  // ShaderUnit::m_version; -1 = no shader
	bool m_showingCreateButton  = false;
	bool m_showingEmptyState    = false;
};

} // namespace neurus
