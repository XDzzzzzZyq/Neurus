#include "ProfilingPanel.h"

#include "UIContext.h"
#include "render/ProfilingData.h"

#include <QAbstractItemView>
#include <QFont>
#include <QHeaderView>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace neurus
{

// =========================================================================
// Constructor - tree widget with per-pass / frame-total columns
// =========================================================================

ProfilingPanel::ProfilingPanel(QWidget* parent)
	: UIPanel(PanelType::Profiling, QStringLiteral("Profiling"), parent)
{
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);

	m_tree = new QTreeWidget(this);
	m_tree->setColumnCount(5);
	m_tree->setHeaderLabels(QStringList{
		"Pass", "CPU (ms)", "GPU (ms)", "Draws", "Dispatches" });
	m_tree->setRootIsDecorated(true);
	m_tree->setUniformRowHeights(true);
	m_tree->setAlternatingRowColors(true);
	m_tree->setAllColumnsShowFocus(true);
	m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
	layout->addWidget(m_tree);
}

// =========================================================================
// Refresh - copy latest profile, rebuild only on change
// =========================================================================

void ProfilingPanel::Refresh(const UIContext& ctx)
{
	const auto* profile = static_cast<const FrameProfile*>(ctx.frameProfile);
	if (!profile)
	{
		m_tree->clear();
		m_hasProfile = false;
		return;
	}

	const bool changed = !m_hasProfile ||
		m_lastCpuMs != profile->cpuRecordMs ||
		m_lastGpuMs != profile->gpuTotalMs ||
		m_lastPassCount != profile->passCount;
	m_lastCpuMs = profile->cpuRecordMs;
	m_lastGpuMs = profile->gpuTotalMs;
	m_lastPassCount = profile->passCount;
	m_hasProfile = true;

	if (!changed)
		return;

	Rebuild(*profile);
}

// =========================================================================
// Rebuild - Frame totals root row + one child row per pass
// =========================================================================

void ProfilingPanel::Rebuild(const FrameProfile& profile)
{
	m_tree->clear();

	if (profile.passCount == 0)
	{
		auto* placeholder = new QTreeWidgetItem(m_tree);
		placeholder->setText(0, "No profiling data yet");
		return;
	}

	// GPU timestamps need device support AND a fence-signaled readback;
	// until then only CPU data is available.
	const bool gpuShown = profile.gpuTimingAvailable && profile.gpuReady;

	auto* frame = new QTreeWidgetItem(m_tree);
	frame->setText(0, "Frame");
	frame->setText(1, QString::number(profile.cpuRecordMs, 'f', 2));
	frame->setText(2, gpuShown ? QString::number(profile.gpuTotalMs, 'f', 2) : "--");
	frame->setText(3, QString::number(profile.drawCalls));
	frame->setText(4, QString::number(profile.dispatches));

	QFont boldFont = frame->font(0);
	boldFont.setBold(true);
	frame->setFont(0, boldFont);

	for (const auto& pass : profile.passes)
	{
		auto* item = new QTreeWidgetItem(frame);
		item->setText(0, QString::fromStdString(pass.name));
		item->setText(1, QString::number(pass.cpuMs, 'f', 2));
		item->setText(2, gpuShown ? QString::number(pass.gpuMs, 'f', 2) : "--");
		item->setText(3, QString::number(pass.drawCalls));
		item->setText(4, QString::number(pass.dispatches));
	}

	frame->setExpanded(true);

	// Fit content columns; keep numeric columns at a fixed readable width.
	m_tree->resizeColumnToContents(0);
	m_tree->resizeColumnToContents(3);
	m_tree->resizeColumnToContents(4);
}

} // namespace neurus
