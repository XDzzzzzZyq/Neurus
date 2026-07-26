/**
 * @file ShaderEditorPanel.h
 * @brief Shader Editor dock panel -- Struct mode + Code mode (placeholder).
 *
 * Provides a tree-based Struct Editor for editing per-mesh GLSL shaders.
 * A QStackedWidget switches between Code Editor (placeholder) and Struct
 * Editor views based on the mode combo selection.
 */
#pragma once

#include "UIPanel.h"

#include <string>
#include <vector>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;

namespace neurus
{

class ShaderStructSection;

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
	/** @brief Emitted when user clicks Compile for the active object's shader. */
	void compileRequested(int objectId);

private:
	/** @brief Sets the active scene object. Resets caches on ID change. */
	void setActiveObject(int objectId);

	/** @brief Rebuilds the Struct Editor tree from the active shader's ShaderStruct. */
	void rebuildStructTree();

	/** @brief Shows the "Create Shader" button for objects without a shader. */
	void setShowCreateButton(bool show);

	/** @brief Shows the "No object selected" message. */
	void setShowEmptyState(bool show);

	/** @brief Clears all sections in the struct editor. */
	void clearStructSections();

	// --- Top toolbar ---
	QComboBox*    m_modeCombo    = nullptr;  // "Code" | "Structure"
	QComboBox*    m_stageCombo   = nullptr;  // VERTEX | FRAGMENT
	QPushButton*  m_compileBtn   = nullptr;
	QPushButton*  m_saveBtn      = nullptr;

	// --- Content stack ---
	QStackedWidget* m_contentStack = nullptr;

	// --- Code Editor placeholder (Page 0) ---
	QPlainTextEdit* m_codePlaceholder = nullptr;

	// --- Struct Editor (Page 1) ---
	QScrollArea*     m_structScroll  = nullptr;
	QWidget*         m_structContent = nullptr;
	QVBoxLayout*     m_structLayout  = nullptr;

	// --- Struct sections (hardcoded for now) ---
	ShaderStructSection* m_abSection      = nullptr;  // Attributes
	ShaderStructSection* m_passSection    = nullptr;  // Outputs (pass)
	ShaderStructSection* m_inputSection   = nullptr;  // Inputs
	ShaderStructSection* m_outputSection  = nullptr;  // Outputs
	ShaderStructSection* m_uniformSection = nullptr;  // Uniforms
	ShaderStructSection* m_structSection  = nullptr;  // Struct Definitions
	ShaderStructSection* m_funcSection    = nullptr;  // Functions

	// --- Empty state / create button ---
	QLabel*       m_emptyLabel    = nullptr;
	QPushButton*  m_createBtn     = nullptr;

	// --- State ---
	int  m_activeObjectId    = -1;
	int  m_cachedShaderUID   = -1;
	int  m_cachedStageCount  = 0;
	bool m_showingCreateButton = false;
	bool m_showingEmptyState   = false;
};

} // namespace neurus
