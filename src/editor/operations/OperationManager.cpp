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
		return;
	}

	// A new forward edit normally invalidates the redo timeline. Transparent
	// ops (e.g. selection) preserve it: because operations are absolute
	// state-sets, a pending redo still replays correctly after them.
	const bool preservesRedo = op->PreservesRedo();
	m_undo.push_back(std::move(op));
	if (!preservesRedo) m_redo.clear();
}

void OperationManager::Undo()
{
	if (m_undo.empty()) return;

	std::unique_ptr<Operation> g = std::move(m_undo.back());
	m_undo.pop_back();

	std::unique_ptr<Operation> inverse = g->Inverse();
	Replay(*inverse);
	m_redo.push_back(std::move(inverse));
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
}

void OperationManager::Clear()
{
	m_undo.clear();
	m_redo.clear();
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
