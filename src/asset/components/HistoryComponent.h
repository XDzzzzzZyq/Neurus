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
 * Layering: the header stays Vulkan-free and only forward-declares
 * OperationManager, so it lives with the other components in the asset layer.
 * The .cpp includes editor/operations headers (OperationManager,
 * OperationRegistration) to drive the undo/redo stacks — a reverse dependency
 * from asset to editor, kept in the .cpp only, since every final binary links
 * editor alongside asset.
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
