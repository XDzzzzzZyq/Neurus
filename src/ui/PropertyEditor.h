#pragma once

#include <QWidget>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QScrollArea>

// Forward declarations
namespace neurus {
class Scene;
class ObjectID;
class Transform3D;
class Camera;
class Light;
}

namespace neurus {

/**
 * @brief Property Editor panel displaying properties of the selected scene object.
 *
 * Subscribes to ObjectSelected/ObjectDeselected events via EventQueue and
 * populates a QFormLayout with read-only property fields based on object type.
 * Supports Transform (all objects), Camera, and Light properties for MVP.
 *
 * @note Properties are read-only for MVP. Editing will be added via Controllers.
 * @note Owned by NeurusMainWindow as a right dock widget.
 */
class PropertyEditor : public QWidget
{
	Q_OBJECT

public:
	/**
	 * @brief Constructs the Property Editor.
	 * @param scene Non-owning pointer to the active Scene (nullable - set later via SetScene).
	 * @param parent Parent widget.
	 */
	explicit PropertyEditor(Scene* scene, QWidget* parent = nullptr);

	~PropertyEditor() override = default;

	PropertyEditor(const PropertyEditor&) = delete;
	PropertyEditor& operator=(const PropertyEditor&) = delete;

	/**
	 * @brief Sets the active scene for object lookups.
	 * @param scene Non-owning pointer to the Scene.
	 */
	void SetScene(Scene* scene);

	/**
	 * @brief Loads and displays properties for the given object.
	 * @param objectId Unique ID of the object to inspect.
	 */
	void LoadObject(int objectId);

	/**
	 * @brief Clears all displayed properties.
	 */
	void Clear();

private:
	// --- Layout helpers ---

	/** @brief Destroys the current form container and creates a fresh one. */
	void ClearFormLayout();

	/** @brief Adds a section header label to the form. */
	void AddSectionHeader(const QString& title);

	/** @brief Creates a read-only QDoubleSpinBox for display. */
	QDoubleSpinBox* CreateReadOnlySpinBox(double value);

	// --- Population helpers ---

	void PopulateHeader(const ObjectID* obj);
	void PopulateTransform(const Transform3D* transform);
	void PopulateCamera(const Camera* cam);
	void PopulateLight(const Light* light);

	// --- Data ---

	Scene* m_scene = nullptr;

	// --- Layout ---

	QVBoxLayout*  m_mainLayout = nullptr;
	QScrollArea*  m_scrollArea = nullptr;
	QWidget*      m_formContainer = nullptr;
	QFormLayout*  m_formLayout = nullptr;
};

} // namespace neurus
