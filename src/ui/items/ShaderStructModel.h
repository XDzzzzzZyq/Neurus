#pragma once

#include "editor/events/ShaderEvents.h"
#include "render/shaders/ShaderStruct.h"

#include <QAbstractItemModel>
#include <memory>
#include <vector>

namespace neurus {

/**
 * @brief Tree model for ShaderStruct IR.
 *
 * 3-level tree: section headers -> field/struct nodes -> struct members.
 * The model is read-only; edits emit signals so the editor controller can
 * mutate the underlying ShaderStruct and refresh.
 */
class ShaderStructModel : public QAbstractItemModel
{
	Q_OBJECT

public:
	explicit ShaderStructModel(QObject* parent = nullptr);
	~ShaderStructModel() override = default;

	ShaderStructModel(const ShaderStructModel&) = delete;
	ShaderStructModel& operator=(const ShaderStructModel&) = delete;

	enum CustomRole
	{
		RoleNodeType = Qt::UserRole,
		RoleSection,
		RoleFieldIndex,
		RoleSubFieldIndex
	};

	enum NodeType
	{
		NodeSection = 0,
		NodeField,
		NodeStructDef,
		NodeStructMember
	};

	// QAbstractItemModel interface
	QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
	QModelIndex parent(const QModelIndex& child) const override;
	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	int columnCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
	Qt::ItemFlags flags(const QModelIndex& index) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

	/** @brief Rebuilds the entire tree from a ShaderStruct. */
	void setShaderStruct(const ShaderStruct* shaderStruct);

signals:
	void fieldEdited(ShaderSection section, int fieldIndex, int subFieldIndex,
	                 const QString& field, const QString& value);

private:
	struct Node
	{
		NodeType type = NodeSection;
		int sectionIndex = -1;
		int fieldIndex = -1;
		int memberIndex = -1;
		Node* parent = nullptr;
		std::vector<std::unique_ptr<Node>> children;
	};

	void buildTree();
	void addSection(const QString& title, ShaderSection section, int count);
	QString nodeString(const Node* node, int column) const;
	const S_Uniform& getUniform(int sectionIndex, int fieldIndex) const;
	QString sectionTitle(int sectionIndex) const;

	const ShaderStruct* m_struct = nullptr;
	std::unique_ptr<Node> m_root;
};

} // namespace neurus
