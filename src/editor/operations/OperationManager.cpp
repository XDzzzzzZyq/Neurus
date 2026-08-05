/**
 * @file OperationManager.cpp
 * @brief Undo/redo stack management and synchronous operation replay.
 */

#include "editor/operations/OperationManager.h"

#include "editor/operations/OperationContext.h"
#include "editor/events/EventBus.h"
#include "scene/Scene.h"

namespace neurus {

OperationManager::OperationManager(EventQueue& bus, std::function<Scene*()> sceneProvider)
	: m_bus(bus)
	, m_sceneProvider(std::move(sceneProvider))
{}

void OperationManager::Submit(std::unique_ptr<Operation> op)
{
	// Replay re-runs the originating handler, which calls Submit again;
	// suppress it so history is not corrupted by its own playback.
	if (m_phase == Phase::Replaying) return;
	if (!op) return;

	// Coalesce a stream of same-key edits (e.g. camera scroll/drag) into the
	// undo-stack top so a continuous manipulation is a single undo step. The
	// live state is already up to date; merging only extends the recorded range.
	const std::string key = op->MergeKey();
	if (!key.empty() && !m_undo.empty() && m_undo.back()->MergeKey() == key)
	{
		m_undo.back()->MergeFrom(*op);
		m_redo.clear();
		++m_revision;
		return;
	}

	// A new forward edit normally invalidates the redo timeline. Transparent
	// ops (e.g. selection) preserve it: because operations are absolute
	// state-sets, a pending redo still replays correctly after them.
	const bool preservesRedo = op->PreservesRedo();
	m_undo.push_back(std::move(op));
	if (!preservesRedo) m_redo.clear();
	++m_revision;
}

void OperationManager::Undo()
{
	if (m_undo.empty()) return;

	std::unique_ptr<Operation> g = std::move(m_undo.back());
	m_undo.pop_back();

	std::unique_ptr<Operation> inverse = g->Inverse();
	Replay(*inverse);
	m_redo.push_back(std::move(inverse));
	++m_revision;
}

void OperationManager::Redo()
{
	if (m_redo.empty()) return;

	std::unique_ptr<Operation> gInverse = std::move(m_redo.back());
	m_redo.pop_back();

	// (g⁻¹)⁻¹ = g — the original forward edit.
	std::unique_ptr<Operation> forward = gInverse->Inverse();
	Replay(*forward);
	m_undo.push_back(std::move(forward));
	++m_revision;
}

void OperationManager::Clear()
{
	m_undo.clear();
	m_redo.clear();
	++m_revision;
}

void OperationManager::RestoreHistory(std::vector<std::unique_ptr<Operation>> undo,
                                      std::vector<std::unique_ptr<Operation>> redo)
{
	m_undo = std::move(undo);
	m_redo = std::move(redo);
	++m_revision;
}

HistoryView OperationManager::GetHistoryView() const
{
	HistoryView view;
	view.revision = m_revision;

	// Undo entries: oldest → newest (front → back); the last is the next Undo.
	view.undo.reserve(m_undo.size());
	for (const auto& op : m_undo)
		view.undo.push_back(op->Label());

	// Redo entries: replay order — the next Redo is m_redo.back(), so walk the
	// stack top → bottom to list them in the order Redo would reapply them.
	view.redo.reserve(m_redo.size());
	for (auto it = m_redo.rbegin(); it != m_redo.rend(); ++it)
		view.redo.push_back((*it)->Label());

	return view;
}

void OperationManager::Replay(Operation& op)
{
	Scene* scene = m_sceneProvider ? m_sceneProvider() : nullptr;
	if (!scene) return;

	OperationContext ctx{ *scene, m_bus };

	m_phase = Phase::Replaying;
	op.Emit(ctx);
	m_phase = Phase::Idle;
}

} // namespace neurus
