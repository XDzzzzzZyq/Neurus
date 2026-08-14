/**
 * @file PreferencesDialog.h
 * @brief Preferences dialog — app-level settings with instant, live apply.
 *
 * Opened from File → Preferences… (Ctrl+,). All changes apply immediately —
 * picking a language retranslates the entire UI on the spot, picking a target
 * FPS updates the render loop — so there is no OK/Cancel flow, only Close.
 *
 * Architecture:
 * - Holds a non-owning pointer to the Application-owned Preferences for
 *   READING current values (SyncFromPreferences).
 * - Every mutation is emitted as a signal and routed through UIEvents so the
 *   Application applies the change, persists ~/.neurus/preferences.json, and
 *   (for language) triggers the global retranslation.
 * - Connects to I18n::languageChanged() to retranslate itself while open.
 */

#pragma once

#include <QDialog>

class QComboBox;
class QGroupBox;
class QLabel;
class QPushButton;

namespace neurus {

class Preferences;

class PreferencesDialog : public QDialog
{
	Q_OBJECT

public:
	/**
	 * @brief Constructs the dialog.
	 * @param prefs Application-owned preferences (non-owning, read-only here).
	 * @param parent Parent widget.
	 */
	explicit PreferencesDialog(Preferences* prefs, QWidget* parent = nullptr);

	/** @brief Re-applies every text in the active language. */
	void Retranslate();

signals:
	/** @brief Emitted when the user picks a language (code, e.g. "zh_CN"). */
	void languageChangeRequested(const QString& language);

	/** @brief Emitted when the user picks a target FPS (0 = unlimited). */
	void targetFpsChangeRequested(int fps);

private:
	/** @brief Pushes current Preferences values into the controls (no signals). */
	void SyncFromPreferences();

	void OnLanguageChanged(int index);
	void OnTargetFpsChanged(int index);
	void OnResetDefaults();

	Preferences* m_prefs = nullptr;

	QGroupBox*   m_generalGroup  = nullptr;
	QComboBox*   m_languageCombo = nullptr;
	QComboBox*   m_fpsCombo      = nullptr;
	QLabel*      m_languageLabel = nullptr;
	QLabel*      m_fpsLabel      = nullptr;
	QLabel*      m_pathLabel     = nullptr;
	QPushButton* m_resetBtn      = nullptr;
	QPushButton* m_closeBtn      = nullptr;
};

} // namespace neurus
