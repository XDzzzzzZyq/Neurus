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

	// This is a read-only telemetry view: kill selection, focus, and hover
	// highlighting so the rows don't react to the mouse like an interactive list.
	m_tree->setSelectionMode(QAbstractItemView::NoSelection);
	m_tree->setFocusPolicy(Qt::NoFocus);
	m_tree->setMouseTracking(false);
	m_tree->viewport()->setAttribute(Qt::WA_Hover, false);

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
		// No renderer profile yet: hide every pooled row and reset detection.
		if (m_frameItem)
			m_frameItem->setHidden(true);
		for (auto* row : m_rowPool)
			row->setHidden(true);
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

	Populate(*profile);
}

// =========================================================================
// EnsureRowPool - grow the per-pass child-row pool (recycle, never destroy)
// =========================================================================

void ProfilingPanel::EnsureRowPool(std::size_t needed)
{
	while (m_rowPool.size() < needed)
		m_rowPool.push_back(new QTreeWidgetItem(m_frameItem));
}

// =========================================================================
// Populate - Frame totals root row + one recycled child row per pass
// =========================================================================

void ProfilingPanel::Populate(const FrameProfile& profile)
{
	// Create the persistent Frame totals row once; reused every frame.
	if (!m_frameItem)
	{
		m_frameItem = new QTreeWidgetItem(m_tree);
		QFont boldFont = m_frameItem->font(0);
		boldFont.setBold(true);
		m_frameItem->setFont(0, boldFont);
	}
	m_frameItem->setHidden(false);

	if (profile.passCount == 0)
	{
		m_frameItem->setText(0, "No profiling data yet");
		for (int col = 1; col < 5; ++col)
			m_frameItem->setText(col, QString());
		for (auto* row : m_rowPool)
			row->setHidden(true);
		return;
	}

	// GPU timestamps need device support AND a fence-signaled readback;
	// until then only CPU data is available.
	const bool gpuShown = profile.gpuTimingAvailable && profile.gpuReady;

	m_frameItem->setText(0, "Frame");
	m_frameItem->setText(1, QString::number(profile.cpuRecordMs, 'f', 2));
	m_frameItem->setText(2, gpuShown ? QString::number(profile.gpuTotalMs, 'f', 2) : "--");
	m_frameItem->setText(3, QString::number(profile.drawCalls));
	m_frameItem->setText(4, QString::number(profile.dispatches));

	EnsureRowPool(profile.passes.size());
	for (std::size_t i = 0; i < m_rowPool.size(); ++i)
	{
		QTreeWidgetItem* item = m_rowPool[i];
		if (i >= profile.passes.size())
		{
			item->setHidden(true);
			continue;
		}

		const auto& pass = profile.passes[i];
		item->setHidden(false);
		item->setText(0, QString::fromStdString(pass.name));
		item->setText(1, QString::number(pass.cpuMs, 'f', 2));
		item->setText(2, gpuShown ? QString::number(pass.gpuMs, 'f', 2) : "--");
		item->setText(3, QString::number(pass.drawCalls));
		item->setText(4, QString::number(pass.dispatches));
	}

	m_frameItem->setExpanded(true);

	// Fit content columns; keep numeric columns at a fixed readable width.
	m_tree->resizeColumnToContents(0);
	m_tree->resizeColumnToContents(3);
	m_tree->resizeColumnToContents(4);
}

} // namespace neurus
