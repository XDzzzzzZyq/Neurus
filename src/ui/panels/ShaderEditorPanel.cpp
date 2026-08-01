#include "ShaderEditorPanel.h"

#include "elements/CodeEditor.h"
#include "items/ShaderStructSection.h"
#include "UIContext.h"
#include "scene/Scene.h"
#include "render/shaders/ShaderUnit.h"
#include "render/shaders/ShaderStruct.h"

#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
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
	m_modeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	row1->addWidget(m_modeCombo, 1);
	row1->addSpacing(8);
	row1->addWidget(new QLabel("Stage:", this));
	m_stageCombo = new QComboBox(this);
	m_stageCombo->addItem("VERTEX");
	m_stageCombo->addItem("FRAGMENT");
	m_stageCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	row1->addWidget(m_stageCombo, 1);
	toolbarContainer->addLayout(row1);

	// Row 2: Compile + Save
	auto* row2 = new QHBoxLayout();
	m_compileBtn = new QPushButton("Compile", this);
	m_compileBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	row2->addWidget(m_compileBtn, 1);
	m_saveBtn = new QPushButton("Save", this);
	m_saveBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	row2->addWidget(m_saveBtn, 1);
	toolbarContainer->addLayout(row2);

	rootLayout->addLayout(toolbarContainer);

	// --- Empty state label (shown above content stack in both modes) ---
	m_emptyLabel = new QLabel("No object selected", this);
	m_emptyLabel->setAlignment(Qt::AlignCenter);
	m_emptyLabel->setStyleSheet("color: gray;");
	m_emptyLabel->setVisible(false);
	m_emptyLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	rootLayout->addWidget(m_emptyLabel);

	// --- Create Shader button (shown above content stack in both modes) ---
	m_createBtn = new QPushButton("Create Shader", this);
	m_createBtn->setVisible(false);
	m_createBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	rootLayout->addWidget(m_createBtn);

	// --- Content stack ---
	m_contentStack = new QStackedWidget(this);

	// Page 0: Code Editor
	auto* codePage = new QWidget();
	auto* codeLayout = new QVBoxLayout(codePage);
	codeLayout->setContentsMargins(0, 0, 0, 0);
	m_codeEditor = new CodeEditor(this);
	m_codeEditor->setLanguage(CodeEditor::Language::GLSL);
	codeLayout->addWidget(m_codeEditor);
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

	// Create all sections (hidden until populated)
	m_abSection      = new ShaderStructSection(this);
	m_passSection    = new ShaderStructSection(this);
	m_inputSection   = new ShaderStructSection(this);
	m_outputSection  = new ShaderStructSection(this);
	m_uniformSection = new ShaderStructSection(this);
	m_structSection  = new ShaderStructSection(this);
	m_funcSection    = new ShaderStructSection(this);
	m_pushConstSection = new ShaderStructSection(this);

	m_abSection->setTitle("Attributes (layout(location))");
	m_passSection->setTitle("Pass Outputs (layout(location))");
	m_inputSection->setTitle("Inputs");
	m_outputSection->setTitle("Outputs");
	m_uniformSection->setTitle("Uniforms");
	m_structSection->setTitle("Struct Definitions");
	m_funcSection->setTitle("Functions");
	m_pushConstSection->setTitle("Push Constants");

	m_abSection->setAddButtonVisible(true);
	m_passSection->setAddButtonVisible(true);
	m_inputSection->setAddButtonVisible(true);
	m_outputSection->setAddButtonVisible(true);
	m_uniformSection->setAddButtonVisible(true);
	m_structSection->setAddButtonVisible(true);
	m_funcSection->setAddButtonVisible(true);
	m_pushConstSection->setAddButtonVisible(true);

	m_structLayout->addWidget(m_abSection);
	m_structLayout->addWidget(m_passSection);
	m_structLayout->addWidget(m_inputSection);
	m_structLayout->addWidget(m_outputSection);
	m_structLayout->addWidget(m_uniformSection);
	m_structLayout->addWidget(m_structSection);
	m_structLayout->addWidget(m_funcSection);
	m_structLayout->addWidget(m_pushConstSection);

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

	// --- Compile button -> emit with unitType (0=Code, 1=Struct) ---
	QObject::connect(m_compileBtn, &QPushButton::clicked, [this]()
	{
		if (m_activeObject)
		{
			int unitType = (m_contentStack->currentIndex() == 0) ? 0 : 1;
			emit compileRequested({m_activeObject, m_cachedStageType, unitType});
		}
	});

	// --- CodeEditor text changes -> codeEdited event ---
	QObject::connect(m_codeEditor, &CodeEditor::codeChanged, [this](const std::string& code)
	{
		if (m_activeObject)
			emit codeEdited({m_activeObject, m_cachedStageType, code});
	});

	// --- Create Shader button -> event signal ---
	QObject::connect(m_createBtn, &QPushButton::clicked, [this]()
	{
		if (m_activeObject)
			emit createShaderRequested({m_activeObject});
	});

	// --- Stage combo change -> invalidate cache ---
	QObject::connect(m_stageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
	                 [this](int index) {
		m_cachedStageType = index;
		m_activeObject = nullptr;  // Re-read on next Refresh
	});

	// --- Section field edits -> structEdited event ---
	auto connectFieldEdited = [this](ShaderStructSection* section, ShaderSection sectionId)
	{
		QObject::connect(section, &ShaderStructSection::fieldEdited,
		                 [this, sectionId](int fieldIndex, const QString& field, const QString& value) {
			if (m_activeObject)
			{
				emit structEdited({m_activeObject, m_cachedStageType, sectionId,
				                   fieldIndex, -1, field.toStdString(), value.toStdString()});
				m_cachedShaderVersion = -1;  // invalidate cache -> re-read on next Refresh
			}
		});
	};
	connectFieldEdited(m_abSection,      ShaderSection::Attributes);
	connectFieldEdited(m_passSection,    ShaderSection::PassOutputs);
	connectFieldEdited(m_inputSection,   ShaderSection::Inputs);
	connectFieldEdited(m_outputSection,  ShaderSection::Outputs);
	connectFieldEdited(m_uniformSection, ShaderSection::Uniforms);
	connectFieldEdited(m_structSection,  ShaderSection::StructDefs);
	connectFieldEdited(m_funcSection,    ShaderSection::Functions);
	connectFieldEdited(m_pushConstSection, ShaderSection::PushConstants);

	// --- Section add buttons -> fieldAdded event ---
	auto connectAddButton = [this](ShaderStructSection* section, ShaderSection sectionId)
	{
		QObject::connect(section, &ShaderStructSection::addButtonClicked,
		                 [this, sectionId]() {
			if (m_activeObject)
			{
				emit fieldAdded({m_activeObject, m_cachedStageType, sectionId, -1});
				m_cachedShaderVersion = -1;  // invalidate cache -> re-read on next Refresh
			}
		});
	};
	connectAddButton(m_abSection,      ShaderSection::Attributes);
	connectAddButton(m_passSection,    ShaderSection::PassOutputs);
	connectAddButton(m_inputSection,   ShaderSection::Inputs);
	connectAddButton(m_outputSection,  ShaderSection::Outputs);
	connectAddButton(m_uniformSection, ShaderSection::Uniforms);
	connectAddButton(m_structSection,  ShaderSection::StructDefs);
	connectAddButton(m_funcSection,    ShaderSection::Functions);
	connectAddButton(m_pushConstSection, ShaderSection::PushConstants);

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
		setShowEmptyState(true);
		setShowCreateButton(false);
		m_activeObject = nullptr;
		return;
	}

	const auto* activeObj = scene->selections.GetActiveObject();
	if (!activeObj)
	{
		setShowEmptyState(true);
		setShowCreateButton(false);
		m_activeObject = nullptr;
		return;
	}

	// Only handle GO_MESH objects
	if (activeObj->o_type != ObjectID::GOType::GO_MESH)
	{
		setShowEmptyState(true);
		setShowCreateButton(false);
		m_activeObject = nullptr;
		return;
	}

	// Get shader unit and its version (-1 if no shader)
	int unitVersion = -1;
	void* unitPtr = activeObj->GetShaderUnit(m_cachedStageType);
	if (unitPtr)
		unitVersion = static_cast<const ShaderUnit*>(unitPtr)->GetVersion();

	// Dirty-check: same object AND same version = nothing changed
	if (activeObj == m_activeObject && unitVersion == m_cachedShaderVersion)
		return;

	m_activeObject = activeObj;
	m_cachedShaderVersion = unitVersion;

	if (!unitPtr)
	{
		// No shader assigned — show create button, hide everything else
		m_emptyLabel->setVisible(false);
		m_showingEmptyState = false;
		setShowCreateButton(true);
		if (m_codeEditor) m_codeEditor->setVisible(false);
		m_abSection->setVisible(false);
		m_passSection->setVisible(false);
		m_inputSection->setVisible(false);
		m_outputSection->setVisible(false);
		m_uniformSection->setVisible(false);
		m_structSection->setVisible(false);
		m_funcSection->setVisible(false);
		return;
	}

	// Has shader — show sections and code editor, hide create button and empty label
	m_emptyLabel->setVisible(false);
	m_showingEmptyState = false;
	setShowCreateButton(false);
	if (m_codeEditor) m_codeEditor->setVisible(true);
	populateSections(unitPtr);
}

// =========================================================================
// populateSections
// =========================================================================

void ShaderEditorPanel::populateSections(const void* shaderUnitPtr)
{
	auto* unit = static_cast<const ShaderUnit*>(shaderUnitPtr);
	const auto& parsed = unit->parsed;

	// Populate code editor with generated source
	m_codeEditor->setCode(unit->code);

	using F = ShaderStructSection::FieldData;

	// Attributes (AB_list)
	if (!parsed.AB_list.empty())
	{
		std::vector<F> fields;
		fields.reserve(parsed.AB_list.size());
		for (const auto& io : parsed.AB_list)
			fields.push_back({ShaderStruct::ParseType(io.type), io.name, io.location});
		m_abSection->setFields(fields);
		m_abSection->setVisible(true);
	}
	else
	{
		m_abSection->setFields({});
		m_abSection->setVisible(false);
	}

	// Pass Outputs (pass_list)
	if (!parsed.pass_list.empty())
	{
		std::vector<F> fields;
		fields.reserve(parsed.pass_list.size());
		for (const auto& io : parsed.pass_list)
			fields.push_back({ShaderStruct::ParseType(io.type), io.name, io.location});
		m_passSection->setFields(fields);
		m_passSection->setVisible(true);
	}
	else
	{
		m_passSection->setFields({});
		m_passSection->setVisible(false);
	}

	// Inputs (input_list)
	if (!parsed.input_list.empty())
	{
		std::vector<F> fields;
		fields.reserve(parsed.input_list.size());
		for (const auto& u : parsed.input_list)
		{
			std::string typeName = u.actualType.empty()
				? ShaderStruct::ParseType(u.type)
				: u.actualType;
			fields.push_back({typeName, u.name});
		}
		m_inputSection->setFields(fields);
		m_inputSection->setVisible(true);
	}
	else
	{
		m_inputSection->setFields({});
		m_inputSection->setVisible(false);
	}

	// Outputs (output_list)
	if (!parsed.output_list.empty())
	{
		std::vector<F> fields;
		fields.reserve(parsed.output_list.size());
		for (const auto& u : parsed.output_list)
		{
			std::string typeName = u.actualType.empty()
				? ShaderStruct::ParseType(u.type)
				: u.actualType;
			fields.push_back({typeName, u.name});
		}
		m_outputSection->setFields(fields);
		m_outputSection->setVisible(true);
	}
	else
	{
		m_outputSection->setFields({});
		m_outputSection->setVisible(false);
	}

	// Uniforms (uniform_list)
	if (!parsed.uniform_list.empty())
	{
		std::vector<F> fields;
		fields.reserve(parsed.uniform_list.size());
		for (const auto& u : parsed.uniform_list)
		{
			std::string typeName = u.actualType.empty()
				? ShaderStruct::ParseType(u.type)
				: u.actualType;
			fields.push_back({typeName, u.name});
		}
		m_uniformSection->setFields(fields);
		m_uniformSection->setVisible(true);
	}
	else
	{
		m_uniformSection->setFields({});
		m_uniformSection->setVisible(false);
	}

	// Struct Definitions (struct_def_list) — show struct name + indented member fields
	if (!parsed.struct_def_list.empty())
	{
		std::vector<F> fields;
		fields.reserve(parsed.struct_def_list.size());
		for (const auto& sd : parsed.struct_def_list)
		{
			// Struct definition row (name + varName)
			fields.push_back({sd.name, sd.varName});
			// Indented member fields
			for (const auto& mf : sd.fields)
			{
				std::string typeName = mf.typeName.empty()
					? ShaderStruct::ParseType(mf.type)
					: mf.typeName;
				fields.push_back({"  " + typeName, mf.name});
			}
		}
		m_structSection->setFields(fields);
		m_structSection->setVisible(true);
	}
	else
	{
		m_structSection->setFields({});
		m_structSection->setVisible(false);
	}

	// Functions (func_list)
	if (!parsed.func_list.empty())
	{
		std::vector<F> fields;
		fields.reserve(parsed.func_list.size());
		for (const auto& fn : parsed.func_list)
			fields.push_back({ShaderStruct::ParseType(fn.returnType), fn.name});
		m_funcSection->setFields(fields);
		m_funcSection->setVisible(true);
	}
	else
	{
		m_funcSection->setFields({});
		m_funcSection->setVisible(false);
	}

	// Push Constants (push_constants)
	if (!parsed.push_constants.empty())
	{
		std::vector<F> fields;
		fields.reserve(parsed.push_constants.size());
		for (const auto& pc : parsed.push_constants)
			fields.push_back({pc.typeName, pc.name});
		m_pushConstSection->setFields(fields);
		m_pushConstSection->setVisible(true);
	}
	else
	{
		m_pushConstSection->setFields({});
		m_pushConstSection->setVisible(false);
	}
}

// =========================================================================
// State visibility helpers
// =========================================================================

void ShaderEditorPanel::setShowEmptyState(bool show)
{
	if (m_showingEmptyState == show) return;
	m_showingEmptyState = show;
	m_emptyLabel->setVisible(show);

	// Hide all sections and code editor when showing empty state
	if (m_codeEditor) m_codeEditor->setVisible(!show);
	m_abSection->setVisible(!show);
	m_passSection->setVisible(!show);
	m_inputSection->setVisible(!show);
	m_outputSection->setVisible(!show);
	m_uniformSection->setVisible(!show);
	m_structSection->setVisible(!show);
	m_funcSection->setVisible(!show);
	m_pushConstSection->setVisible(!show);
}

void ShaderEditorPanel::setShowCreateButton(bool show)
{
	if (m_showingCreateButton == show) return;
	m_showingCreateButton = show;
	m_createBtn->setVisible(show);
}

} // namespace neurus
