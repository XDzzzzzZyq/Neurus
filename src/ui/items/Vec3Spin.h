/**
 * @file Vec3Spin.h
 * @brief Composite widget: three QDoubleSpinBox in a horizontal row.
 *
 * Vec3Spin encapsulates three QDoubleSpinBox instances in a single QWidget.
 * No labels — the parent provides row context.
 *
 * Architecture:
 * - QWidget subclass with internal QHBoxLayout
 * - Owns the three QDoubleSpinBox child widgets
 * - setValue() with dirty-check: skips spinbox update if values unchanged
 * - No Vulkan or Renderer dependencies — pure Qt UI layer
 * - Lives in src/ui/items/ alongside ScalarSlider and OutlinerRow
 */

#pragma once

#include <QWidget>

class QDoubleSpinBox;

namespace neurus
{

class Vec3Spin : public QWidget
{
	Q_OBJECT

public:
	/**
	 * @brief Constructs a Vec3Spin with three spinboxes.
	 *
	 * @param min      Minimum value for all three spinboxes.
	 * @param max      Maximum value for all three spinboxes.
	 * @param step     Single-step increment.
	 * @param decimals Number of decimal places.
	 * @param suffix   Optional suffix (e.g. "\u00B0" for degrees).
	 * @param parent   Parent widget.
	 */
	explicit Vec3Spin(double min, double max, double step, int decimals,
	                  const QString& suffix = QString(), QWidget* parent = nullptr);
	~Vec3Spin() override = default;

	Vec3Spin(const Vec3Spin&) = delete;
	Vec3Spin& operator=(const Vec3Spin&) = delete;

	/**
	 * @brief Sets all three spinbox values without emitting signals.
	 *
	 * Dirty-checks against cached values — no-op if (x,y,z) unchanged.
	 * Uses QSignalBlocker on all three spinboxes to suppress valueChanged
	 * during programmatic updates.
	 */
	void setValue(double x, double y, double z);

signals:
	/** @brief Emitted when any spinbox changes, carrying the current (x,y,z). */
	void valueChanged(double x, double y, double z);

private:
	QDoubleSpinBox* m_spinX = nullptr;
	QDoubleSpinBox* m_spinY = nullptr;
	QDoubleSpinBox* m_spinZ = nullptr;

	// --- Cached values for dirty-check ---
	double m_valX = 0;
	double m_valY = 0;
	double m_valZ = 0;
};

} // namespace neurus
