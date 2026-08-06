/**
 * @file HistoryComponent.h
 * @brief Serializable adapter that persists the undo/redo stacks in a project.
 *
 * Mirrors SceneComponent/ConfigComponent: it implements project::Serializable
 * and wraps a non-owning OperationManager pointer. Save writes both stacks as
 * arrays of polymorphic Operation pointers (cereal auto-emits each op's
 * concrete type); Load reconstructs them and hands them back to the manager
 * with RestoreHistory().
 *
 * Layering: this lives in the editor layer (not asset) because it depends on
 * editor/operations headers. It only implements the asset-layer Serializable
 * interface (a header-only pure abstract), which does not violate isolation.
 */

#pragma once

#include "asset/Serializable.h"

namespace neurus { class OperationManager; }

namespace neurus::project
{

/**
 * @brief Persists an OperationManager's undo/redo history in a project file.
 */
class HistoryComponent : public Serializable
{
public:
	explicit HistoryComponent(OperationManager& operations);

	const char* Key() const noexcept override { return "m_history"; }
	void Save(cereal::JSONOutputArchive& ar) const override;
	void Load(cereal::JSONInputArchive& ar) override;

private:
	OperationManager* m_operations;
};

} // namespace neurus::project
