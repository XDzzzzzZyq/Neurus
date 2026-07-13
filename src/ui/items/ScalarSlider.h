/**
 * @file ScalarSlider.h
 * @brief Composite widget pairing a QSlider with a QDoubleSpinBox.
 *
 * ScalarSlider encapsulates a bidirectional slider↔spinbox pair into a single
 * QWidget. Moving either control synchronises the other and emits a single
 * valueChanged() signal — no matter which control was manipulated.
 *
 * Architecture:
 * - QWidget subclass with internal QHBoxLayout
 * - Owns the QSlider and QDoubleSpinBox as child widgets
 * - No Vulkan or Renderer dependencies — pure Qt UI layer
 * - Lives in src/ui/items/ alongside other reusable UI primitives
 */

#pragma once

#include <QWidget>

class QSlider;
class QDoubleSpinBox;

namespace neurus
{

class ScalarSlider : public QWidget
{
	Q_OBJECT

public:
	/**
	 * @brief Constructs a ScalarSlider with the given range and initial value.
	 *
	 * The slider uses @p sliderSteps discrete steps; the spinbox uses
	 * @p step as its increment. The initial value is set on both controls.
	 *
	 * @param min         Minimum value (spinbox lower bound).
	 * @param max         Maximum value (spinbox upper bound).
	 * @param step        Spinbox single-step increment.
	 * @param sliderSteps Number of discrete slider positions (resolution).
	 * @param initial     Initial value for both controls.
	 * @param parent      Parent widget.
	 */
	explicit ScalarSlider(double min, double max, double step, int sliderSteps,
	                      double initial, QWidget* parent = nullptr);
	~ScalarSlider() override = default;

	ScalarSlider(const ScalarSlider&) = delete;
	ScalarSlider& operator=(const ScalarSlider&) = delete;

	/** @brief Returns the current value from the spinbox. */
	double value() const;

	/** @brief Returns the internal QSlider (read-only access for layout tuning). */
	QSlider* slider() const { return m_slider; }

	/** @brief Returns the internal QDoubleSpinBox (read-only access for layout tuning). */
	QDoubleSpinBox* spinBox() const { return m_spin; }

public slots:
	/**
	 * @brief Sets the value on both controls without emitting signals.
	 *
	 * Uses QSignalBlocker on both the slider and spinbox so that
	 * programmatic updates do not trigger valueChanged().
	 *
	 * @param v New value.
	 */
	void setValue(double v);

signals:
	/** @brief Emitted whenever the slider or spinbox value changes. */
	void valueChanged();

private:
	QSlider*        m_slider      = nullptr;
	QDoubleSpinBox* m_spin        = nullptr;
	double          m_min         = 0.0;
	double          m_max         = 1.0;
	int             m_sliderSteps = 100;
};

} // namespace neurus
