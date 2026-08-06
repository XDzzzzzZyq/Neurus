/**
 * @file HistoryView.h
 * @brief Read-only snapshot of the undo/redo stacks for display.
 *
 * A HistoryView is a flat, Qt-free / Vulkan-free copy of the operation labels
 * held by OperationManager, produced on demand for the UI's History panel. It
 * is a value type so the UI never touches the live stacks or the Operation
 * objects themselves.
 *
 * Ordering (both vectors read as a forward timeline top→bottom):
 * - `undo`  — already-applied operations, oldest first; the last entry is the
 *   next Undo target (the current position sits just after it).
 * - `redo`  — undone operations available to reapply, in the order Redo would
 *   replay them; the first entry is the next Redo target.
 *
 * `revision` monotonically increases whenever the stacks change, letting the
 * panel skip rebuilding its widgets when nothing happened.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace neurus {

/**
 * @brief Flat snapshot of the undo/redo history for read-only display.
 */
struct HistoryView
{
	std::vector<std::string> undo; ///< Applied ops, oldest → newest (next-undo last).
	std::vector<std::string> redo; ///< Undone ops, in replay order (next-redo first).
	uint64_t revision = 0;         ///< Bumped on every stack change (for change detection).
};

} // namespace neurus
