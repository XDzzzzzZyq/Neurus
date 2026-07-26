#include "ShaderEditorPanel.h"

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

	// --- Top toolbar (two rows) ---
	auto* toolbarContainer = new QVBoxLayout();
	toolbarContainer->setContentsMargins(0, 0, 0, 0);
	toolbarContainer->setSpacing(2);

	// Row 1: Mode + Stage
	auto* row1 = new QHBoxLayout();
	row1->addWidget(new QLabel("Mode:", this));
	m_modeCombo = new QComboBox(this);
	m_modeCombo->addItem("Code");
	m_modeCombo->addItem("Structure");
	m_modeCombo->setCurrentIndex(1);
	row1->addWidget(m_modeCombo);
	row1->addSpacing(8);
	row1->addWidget(new QLabel("Stage:", this));
	m_stageCombo = new QComboBox(this);
	m_stageCombo->addItem("VERTEX");
	m_stageCombo->addItem("FRAGMENT");
	row1->addWidget(m_stageCombo);
	row1->addStretch();
	toolbarContainer->addLayout(row1);

	// Row 2: Compile + Save
	auto* row2 = new QHBoxLayout();
	m_compileBtn = new QPushButton("Compile", this);
	row2->addWidget(m_compileBtn);
	m_saveBtn = new QPushButton("Save", this);
	row2->addWidget(m_saveBtn);
	row2->addStretch();
	toolbarContainer->addLayout(row2);

	rootLayout->addLayout(toolbarContainer);

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
		// Don't clear sections -- just hide them (setShowEmptyState already does this)
		return;
	}

	setShowEmptyState(false);

	// In this hardcoded prototype, always show the struct tree
	// with example data. Real wiring will check mesh->o_shader.
	setShowCreateButton(false);
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

	// Hide all sections when showing empty state
	m_abSection->setVisible(!show);
	m_passSection->setVisible(!show);
	m_inputSection->setVisible(!show);
	m_outputSection->setVisible(!show);
	m_uniformSection->setVisible(!show);
	m_structSection->setVisible(!show);
	m_funcSection->setVisible(!show);
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

void ShaderEditorPanel::rebuildStructTree()
{
	using F = ShaderStructSection::FieldData;

	m_abSection->setFields({
		F{"vec3", "aPos", 0},
		F{"vec3", "aNormal", 1},
		F{"vec2", "aTexCoord", 2}
	});

	m_passSection->setFields({
		F{"vec4", "fragAlbedo", 0},
		F{"vec4", "fragNormal", 1},
		F{"vec4", "fragPosition", 2}
	});

	m_inputSection->setFields({
		F{"vec3", "vNormal"},
		F{"vec2", "vTexCoord"},
		F{"vec3", "vWorldPos"}
	});

	m_outputSection->setFields({
		F{"vec4", "fragColor"}
	});

	m_uniformSection->setFields({
		F{"CameraUBO", "camera"},
		F{"sampler2D", "diffuseTex"}
	});

	m_structSection->setFields({
		F{"Light", ""}
	});

	m_funcSection->setFields({
		F{"void", "calculateLighting"}
	});

	m_cachedShaderUID = -1;
}

} // namespace neurus
