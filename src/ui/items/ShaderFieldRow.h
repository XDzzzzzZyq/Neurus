#pragma once

#include <QWidget>

#include <string>

class QComboBox;
class QLineEdit;
class QSpinBox;

namespace neurus
{

/**
 * @brief Reusable widget for a single shader field (type combo + name field + optional location).
 *
 * Supports lazy dirty-check via setField() caching to avoid redundant widget updates.
 */
class ShaderFieldRow : public QWidget
{
	Q_OBJECT

public:
	explicit ShaderFieldRow(QWidget* parent = nullptr);
	~ShaderFieldRow() override = default;

	ShaderFieldRow(const ShaderFieldRow&) = delete;
	ShaderFieldRow& operator=(const ShaderFieldRow&) = delete;

	/** @brief Control visibility of the location spinbox. Default: hidden. */
	void setShowLocation(bool show);

	/** @brief Set field data with location, type, and name. Dirty-checked. */
	void setField(int location, const std::string& type, const std::string& name);

	/** @brief Set field data with type and name only (no location). Dirty-checked. */
	void setField(const std::string& type, const std::string& name);

	/** @brief Make all widgets read-only. */
	void setReadOnly(bool readOnly);

	/** @brief Reset all cached values to sentinel. Call when field identity changes. */
	void resetCaches();

signals:
	/** @brief Emitted when any field changes (user edit). */
	void fieldChanged(int location, const QString& type, const QString& name);

private:
	/** @brief Populate the type combo with GLSL types from ShaderStruct::type_table. */
	void populateTypeCombo();

	QComboBox* m_typeCombo    = nullptr;
	QLineEdit* m_nameField    = nullptr;
	QSpinBox*  m_locationSpin = nullptr;

	// Cached values for dirty-check
	int         m_cachedLocation = -1;
	std::string m_cachedType;
	std::string m_cachedName;
	bool        m_showLocation   = false;
};

} // namespace neurus
