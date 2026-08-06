#include "ui/panels/LogPanel.h"

#include <fstream>

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QScrollBar>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/Log.h"

namespace neurus
{

// --- QSS loading (static one-shot, mirrors ShaderEditorPanel pattern) ---
namespace
{

QString LoadLogPanelStyle()
{
	static const QString s_style = []() {
		QFile file(QStringLiteral(":/qml/logpanel.qss"));
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
			return QString();
		QTextStream stream(&file);
		const QString css = stream.readAll();
		file.close();
		return css;
	}();
	return s_style;
}

} // namespace

LogPanel::LogPanel(QWidget* parent)
	: UIPanel(PanelType::Log, QStringLiteral("Log"), parent)
{
	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(4, 4, 4, 4);
	root->setSpacing(4);

	// Counts header (dirty-checked in Refresh()).
	m_counts = new QLabel(QStringLiteral("INFO 0 · ERROR 0"), this);
	m_counts->setObjectName(QStringLiteral("logCounts"));
	root->addWidget(m_counts);

	BuildToolbar();
	root->addLayout(m_toolbar);

	m_proxy.setSourceModel(&m_model);
	m_view.setModel(&m_proxy);
	m_view.setItemDelegate(&m_delegate);
	m_view.setObjectName(QStringLiteral("logView"));
	m_view.setUniformItemSizes(true);
	m_view.setSelectionMode(QAbstractItemView::NoSelection);
	m_view.setFocusPolicy(Qt::NoFocus);
	m_view.setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	root->addWidget(&m_view, 1);

	setStyleSheet(LoadLogPanelStyle());
}

void LogPanel::BuildToolbar()
{
	m_toolbar = new QHBoxLayout();
	m_toolbar->setContentsMargins(0, 0, 0, 0);
	m_toolbar->setSpacing(4);

	m_filter = new QComboBox(this);
	m_filter->addItems({QStringLiteral("All"), QStringLiteral("Info"),
	                    QStringLiteral("Errors")});
	m_filter->setCurrentIndex(0);
	m_filter->setObjectName(QStringLiteral("logToolBtn"));
	m_toolbar->addWidget(m_filter);

	m_search = new QLineEdit(this);
	m_search->setPlaceholderText(QStringLiteral("Search..."));
	m_search->setClearButtonEnabled(true);
	m_search->setObjectName(QStringLiteral("logSearch"));
	m_toolbar->addWidget(m_search, 1);

	m_autoScrollBtn = new QToolButton(this);
	m_autoScrollBtn->setText(QStringLiteral("Auto-scroll"));
	m_autoScrollBtn->setCheckable(true);
	m_autoScrollBtn->setChecked(true);
	m_autoScrollBtn->setObjectName(QStringLiteral("logToolBtn"));
	m_toolbar->addWidget(m_autoScrollBtn);

	m_pauseBtn = new QToolButton(this);
	m_pauseBtn->setText(QStringLiteral("Pause"));
	m_pauseBtn->setCheckable(true);
	m_pauseBtn->setChecked(false);
	m_pauseBtn->setObjectName(QStringLiteral("logToolBtn"));
	m_toolbar->addWidget(m_pauseBtn);

	m_clearBtn = new QToolButton(this);
	m_clearBtn->setText(QStringLiteral("Clear"));
	m_clearBtn->setObjectName(QStringLiteral("logToolBtn"));
	m_toolbar->addWidget(m_clearBtn);

	m_exportBtn = new QToolButton(this);
	m_exportBtn->setText(QStringLiteral("Export..."));
	m_exportBtn->setObjectName(QStringLiteral("logToolBtn"));
	m_toolbar->addWidget(m_exportBtn);

	connect(m_filter, &QComboBox::currentIndexChanged,
	        this, &LogPanel::OnFilterChanged);
	connect(m_search, &QLineEdit::textChanged,
	        this, &LogPanel::OnSearchChanged);
	connect(m_autoScrollBtn, &QToolButton::toggled,
	        this, &LogPanel::OnAutoScrollToggled);
	connect(m_pauseBtn, &QToolButton::toggled,
	        this, &LogPanel::OnPauseToggled);
	connect(m_clearBtn, &QToolButton::clicked,
	        this, &LogPanel::OnClearClicked);
	connect(m_exportBtn, &QToolButton::clicked,
	        this, &LogPanel::OnExportClicked);
}

void LogPanel::Refresh(const UIContext& ctx)
{
	const auto* buffer = static_cast<const LogBuffer*>(ctx.log);
	if (!buffer)
		return;

	if (!m_paused)
		m_model.Refresh(buffer);

	const std::size_t info = buffer->InfoCount();
	const std::size_t errors = buffer->ErrorCount();

	// Error notifier: fire only when the count grew (throttled to one
	// signal per Refresh window; repeated errors accumulate in delta).
	if (errors > m_lastErrorCount)
	{
		const std::size_t delta = errors - m_lastErrorCount;
		QString firstMsg;
		const std::size_t size = buffer->Size();
		const std::size_t limit = (delta < size) ? delta : size;
		for (std::size_t k = 0; k < limit; ++k)
		{
			const LogEntry e = buffer->At(size - 1 - k);
			if (e.level == LogLevel::Error)
			{
				firstMsg = QString::fromStdString(e.message);
				break;
			}
		}
		emit errorNotified(static_cast<int>(delta), firstMsg);
	}

	// Counts header (dirty-checked).
	if (info != m_lastInfoCount || errors != m_lastErrorCount)
	{
		m_lastInfoCount = info;
		m_lastErrorCount = errors;
		m_counts->setText(QStringLiteral("INFO %1 · ERROR %2")
		                      .arg(info).arg(errors));
	}

	MaybeScrollToBottom();
}

void LogPanel::MaybeScrollToBottom()
{
	if (!m_autoScrollBtn->isChecked())
		return;
	auto* sb = m_view.verticalScrollBar();
	sb->setValue(sb->maximum());
}

void LogPanel::OnFilterChanged(int index)
{
	switch (index)
	{
	case 1: m_proxy.setLevelFilter(LogFilterProxy::LevelFilter::InfoOnly); break;
	case 2: m_proxy.setLevelFilter(LogFilterProxy::LevelFilter::ErrorsOnly); break;
	default: m_proxy.setLevelFilter(LogFilterProxy::LevelFilter::All); break;
	}
}

void LogPanel::OnSearchChanged(const QString& text)
{
	m_proxy.setSearchText(text);
}

void LogPanel::OnAutoScrollToggled(bool checked)
{
	Q_UNUSED(checked);
	MaybeScrollToBottom();
}

void LogPanel::OnPauseToggled(bool checked)
{
	m_paused = checked;
	if (!m_paused)
		m_model.Refresh(&LogBuffer::instance());
}

void LogPanel::OnClearClicked()
{
	LogBuffer::instance().Clear();
	m_lastErrorCount = 0;
	m_lastInfoCount = 0;
	m_model.Refresh(&LogBuffer::instance());
	m_counts->setText(QStringLiteral("INFO 0 · ERROR 0"));
}

void LogPanel::OnExportClicked()
{
	const QString path = QFileDialog::getSaveFileName(
	    this, QStringLiteral("Save Log"), QStringLiteral("neurus.log"),
	    QStringLiteral("Log Files (*.log)"));
	if (path.isEmpty())
		return;

	std::ofstream file(path.toStdString(), std::ios::binary);
	if (!file.is_open())
	{
		QMessageBox::warning(this, QStringLiteral("Save Log"),
		                     QStringLiteral("Failed to open file for writing."));
		return;
	}

	const LogBuffer& buf = LogBuffer::instance();
	const std::size_t size = buf.Size();
	for (std::size_t i = 0; i < size; ++i)
	{
		const LogEntry e = buf.At(i);
		file << FormatLogTimestamp(e.timestamp).toStdString()
		     << " " << (e.level == LogLevel::Error ? "ERROR" : "INFO")
		     << " [" << e.func << ":" << e.line << "] "
		     << e.message << "\n";
	}
	file.close();
	if (!file.good())
	{
		QMessageBox::warning(this, QStringLiteral("Save Log"),
		                     QStringLiteral("Error writing log file."));
		return;
	}
}

} // namespace neurus
