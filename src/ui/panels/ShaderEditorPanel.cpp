#include "ShaderEditorPanel.h"

#include "items/ShaderFieldRow.h"
#include "items/ShaderStructSection.h"
#include "UIContext.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"

#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace neurus
{

// =========================================================================
// Constructor
// =========================================================================

ShaderEditorPanel::ShaderEditorPanel(QWidget* parent)
	: UIPanel(kType, "Shader Editor", parent)
{
	auto* rootLayout = new QVBoxLayout(this);
	rootLayout->setContentsMargins(4, 4, 4, 4);
	rootLayout->setSpacing(4);

	// --- Top toolbar ---
	auto* toolbarLayout = new QHBoxLayout();

	m_modeCombo = new QComboBox(this);
	m_modeCombo->addItem("Code");
	m_modeCombo->addItem("Structure");
	m_modeCombo->setCurrentIndex(1);  // Default to Structure mode
	toolbarLayout->addWidget(new QLabel("Mode:", this));
	toolbarLayout->addWidget(m_modeCombo);

	m_stageCombo = new QComboBox(this);
	m_stageCombo->addItem("VERTEX");
	m_stageCombo->addItem("FRAGMENT");
	toolbarLayout->addWidget(new QLabel("Stage:", this));
	toolbarLayout->addWidget(m_stageCombo);

	toolbarLayout->addStretch();

	m_compileBtn = new QPushButton("Compile", this);
	toolbarLayout->addWidget(m_compileBtn);

	m_saveBtn = new QPushButton("Save", this);
	toolbarLayout->addWidget(m_saveBtn);

	rootLayout->addLayout(toolbarLayout);

	// --- Content stack ---
	m_contentStack = new QStackedWidget(this);

	// Page 0: Code Editor placeholder
	auto* codePage = new QWidget();
	auto* codeLayout = new QVBoxLayout(codePage);
	codeLayout->setContentsMargins(0, 0, 0, 0);
	m_codePlaceholder = new QPlainTextEdit(this);
	m_codePlaceholder->setReadOnly(true);
	m_codePlaceholder->setPlaceholderText("Code Editor -- coming soon");
	codeLayout->addWidget(m_codePlaceholder);
	m_contentStack->addWidget(codePage);

	// Page 1: Struct Editor
	auto* structPage = new QWidget();
	auto* structPageLayout = new QVBoxLayout(structPage);
	structPageLayout->setContentsMargins(0, 0, 0, 0);

	m_structScroll = new QScrollArea(this);
	m_structScroll->setWidgetResizable(true);
	m_structContent = new QWidget();
	m_structLayout = new QVBoxLayout(m_structContent);
	m_structLayout->setContentsMargins(0, 0, 0, 0);
	m_structLayout->setSpacing(4);

	// --- Empty state label ---
	m_emptyLabel = new QLabel("No object selected", this);
	m_emptyLabel->setAlignment(Qt::AlignCenter);
	m_emptyLabel->setStyleSheet("color: gray;");
	m_structLayout->addWidget(m_emptyLabel);

	// --- Create Shader button ---
	m_createBtn = new QPushButton("Create Shader", this);
	m_createBtn->setVisible(false);
	m_structLayout->addWidget(m_createBtn);

	// Create all sections (hidden until populated)
	m_abSection      = new ShaderStructSection(this);
	m_passSection    = new ShaderStructSection(this);
	m_inputSection   = new ShaderStructSection(this);
	m_outputSection  = new ShaderStructSection(this);
	m_uniformSection = new ShaderStructSection(this);
	m_structSection  = new ShaderStructSection(this);
	m_funcSection    = new ShaderStructSection(this);

	m_abSection->setTitle("Attributes (layout(location))");
	m_passSection->setTitle("Pass Outputs (layout(location))");
	m_inputSection->setTitle("Inputs");
	m_outputSection->setTitle("Outputs");
	m_uniformSection->setTitle("Uniforms");
	m_structSection->setTitle("Struct Definitions");
	m_funcSection->setTitle("Functions");

	m_abSection->setAddButtonVisible(true);
	m_passSection->setAddButtonVisible(true);
	m_inputSection->setAddButtonVisible(true);
	m_outputSection->setAddButtonVisible(true);
	m_uniformSection->setAddButtonVisible(true);
	m_structSection->setAddButtonVisible(true);
	m_funcSection->setAddButtonVisible(true);

	m_structLayout->addWidget(m_abSection);
	m_structLayout->addWidget(m_passSection);
	m_structLayout->addWidget(m_inputSection);
	m_structLayout->addWidget(m_outputSection);
	m_structLayout->addWidget(m_uniformSection);
	m_structLayout->addWidget(m_structSection);
	m_structLayout->addWidget(m_funcSection);

	m_structLayout->addStretch();

	m_structScroll->setWidget(m_structContent);
	structPageLayout->addWidget(m_structScroll);
	m_contentStack->addWidget(structPage);

	// Default to Struct Editor
	m_contentStack->setCurrentIndex(1);

	rootLayout->addWidget(m_contentStack, 1);

	// --- Mode combo -> switch pages ---
	QObject::connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	                 m_contentStack, &QStackedWidget::setCurrentIndex);

	// --- Compile button -> signal ---
	QObject::connect(m_compileBtn, &QPushButton::clicked, [this]()
	{
		if (m_activeObjectId > 0)
			emit compileRequested(m_activeObjectId);
	});

	// Start with empty state
	setShowEmptyState(true);
}

// =========================================================================
// Refresh
// =========================================================================

void ShaderEditorPanel::Refresh(const UIContext& ctx)
{
	const auto* scene = static_cast<const Scene*>(ctx.scene);
	if (!scene)
	{
		setActiveObject(0);
		return;
	}

	// Get the active selected object
	const auto* activeObj = scene->selections.GetActiveObject();
	if (!activeObj)
	{
		setActiveObject(0);
		return;
	}

	setActiveObject(activeObj->GetObjectID());
}

// =========================================================================
// setActiveObject
// =========================================================================

void ShaderEditorPanel::setActiveObject(int objectId)
{
	if (m_activeObjectId == objectId) return;
	m_activeObjectId = objectId;

	if (objectId <= 0)
	{
		setShowEmptyState(true);
		setShowCreateButton(false);
		clearStructSections();
		return;
	}

	setShowEmptyState(false);

	// In this hardcoded prototype, always show the struct tree
	// with example data. Real wiring will check mesh->o_shader.
	setShowCreateButton(false);
	clearStructSections();
	rebuildStructTree();
}

// =========================================================================
// State visibility helpers
// =========================================================================

void ShaderEditorPanel::setShowEmptyState(bool show)
{
	if (m_showingEmptyState == show) return;
	m_showingEmptyState = show;
	m_emptyLabel->setVisible(show);
}

void ShaderEditorPanel::setShowCreateButton(bool show)
{
	if (m_showingCreateButton == show) return;
	m_showingCreateButton = show;
	m_createBtn->setVisible(show);
}

// =========================================================================
// Struct tree
// =========================================================================

void ShaderEditorPanel::clearStructSections()
{
	m_abSection->clearRows();
	m_passSection->clearRows();
	m_inputSection->clearRows();
	m_outputSection->clearRows();
	m_uniformSection->clearRows();
	m_structSection->clearRows();
	m_funcSection->clearRows();
}

void ShaderEditorPanel::rebuildStructTree()
{
	// Hardcoded prototype data -- mimics a typical gbuffer.vert shader
	// Attributes (AB_list)
	{
		auto* row = m_abSection->addRow();
		row->setShowLocation(true);
		row->setShowInterpolation(false);
		row->setField(0, "vec3", "aPos");

		row = m_abSection->addRow();
		row->setShowLocation(true);
		row->setShowInterpolation(false);
		row->setField(1, "vec3", "aNormal");

		row = m_abSection->addRow();
		row->setShowLocation(true);
		row->setShowInterpolation(false);
		row->setField(2, "vec2", "aTexCoord");
	}

	// Pass Outputs
	{
		auto* row = m_passSection->addRow();
		row->setShowLocation(true);
		row->setShowInterpolation(false);
		row->setField(0, "vec4", "fragAlbedo");

		row = m_passSection->addRow();
		row->setShowLocation(true);
		row->setShowInterpolation(false);
		row->setField(1, "vec4", "fragNormal");

		row = m_passSection->addRow();
		row->setShowLocation(true);
		row->setShowInterpolation(false);
		row->setField(2, "vec4", "fragPosition");
	}

	// Inputs
	{
		auto* row = m_inputSection->addRow();
		row->setField("vec3", "vNormal");

		row = m_inputSection->addRow();
		row->setField("vec2", "vTexCoord");

		row = m_inputSection->addRow();
		row->setField("vec3", "vWorldPos");
	}

	// Outputs
	{
		auto* row = m_outputSection->addRow();
		row->setField("vec4", "fragColor");
	}

	// Uniforms
	{
		auto* row = m_uniformSection->addRow();
		row->setField("CameraUBO { mat4 viewProj }", "");

		row = m_uniformSection->addRow();
		row->setField("sampler2D", "diffuseTex");
	}

	// Struct Definitions
	{
		auto* row = m_structSection->addRow();
		row->setField("Light", "{ vec3 pos, vec3 color }");
	}

	// Functions
	{
		auto* row = m_funcSection->addRow();
		row->setField("calculateLighting()", "");
	}

	m_cachedShaderUID = -1;  // Hardcoded for now
}

} // namespace neurus
