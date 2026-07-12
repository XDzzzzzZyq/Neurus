/**
 * @file Selections.h
 * @brief Generic selection state management for objects, with serialization support.
 *
 * Selections provides a type-safe container for tracking user selections.
 * It supports single and multi-selection modes with fast lookup via hash set.
 * Unlike the original SelectionManager, Selections stores object IDs (int)
 * instead of raw pointers, enabling cereal serialization.
 *
 * Architecture:
 * - Template-based for use with any object type (ObjectID, Mesh, etc.)
 * - Scene owns Selections<ObjectID> for scene object selection (serializable)
 * - Editor delegates to Scene::selections via pointer, with a
 *   fallback instance for standalone (no-scene) usage
 * - UI queries selections to highlight/display selected objects
 * - Controllers mutate selections in response to user input
 *
 * Serialization:
 * - m_selectedIds (vector of int) is the serialized payload
 * - m_selectedPtrs and m_selectionCache are runtime caches rebuilt via
 *   ResolvePointers() after deserialization
 *
 * Selection Patterns:
 * - Single select: Replace current selection
 * - Multi-select (increment): Add to selection set
 * - Deselect: Remove from selection set
 * - Active object: Last selected object in list
 *
 * @note Core Layer: Selections is part of the core type system
 * @note Thread-safety: Not thread-safe. Must be used from main thread only.
 */

#pragma once

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>

/**
 * @brief Generic selection state manager supporting single and multi-selection,
 *        with ID-based storage for cereal serialization.
 *
 * Selections maintains a list of selected object IDs with fast lookup.
 * It distinguishes between:
 * - Selected objects: All objects in the selection set
 * - Active object: The last selected object (primary selection)
 *
 * ID-based storage (int) enables cereal serialization. Runtime pointer
 * access is provided via a parallel pointer cache that can be rebuilt
 * after deserialization via ResolvePointers().
 *
 * Usage Examples:
 * - Scene object selection in Outliner/Viewport
 * - Material selection in MaterialViewer
 * - Node selection in node editor
 *
 * Selection Modes:
 * - increment=false: Single selection (replace existing)
 * - increment=true: Multi-selection (add to set)
 *
 * Performance:
 * - Select: O(1) lookup + O(1) append
 * - IsSelected: O(1) hash lookup
 * - GetSelected: O(1) vector access
 *
 * @tparam T Base type of selectable objects (must provide GetObjectID() -> int)
 *
 * @note Active object is always the last selected object in the list
 * @note Duplicate selections are prevented via m_selectionCache
 */
template<class T>
class Selections
{
public:
	/** @brief Default constructor. */
	Selections() = default;

	/** @brief Virtual destructor for polymorphic use. */
	virtual ~Selections() = default;

	// Non-copyable (selections are unique per context)
	Selections(const Selections&) = delete;
	Selections& operator=(const Selections&) = delete;

	// Movable
	Selections(Selections&&) noexcept = default;
	Selections& operator=(Selections&&) noexcept = default;

	// -------------------------------------------------------------------
	// Selection API
	// -------------------------------------------------------------------

	/**
	 * @brief Selects an object.
	 *
	 * If increment=false (single selection):
	 * - Clears existing selection
	 * - Adds obj as the only selected object
	 *
	 * If increment=true (multi-selection):
	 * - If obj not in selection: adds to end (becomes active)
	 * - If obj already selected: moves to end (becomes active)
	 *
	 * @param obj Pointer to object to select (nullptr ignored)
	 * @param increment Whether to add to selection (true) or replace (false)
	 *
	 * @note Active object is always selected_objects.back()
	 */
	void Select(T* obj, bool increment)
	{
		if (obj == nullptr) return;

		int id = obj->GetObjectID();

		if (increment)
		{
			auto idx = std::find(m_selectedIds.begin(), m_selectedIds.end(), id);
			if (idx == m_selectedIds.end())
			{
				m_selectedIds.push_back(id);
				m_selectedPtrs.push_back(obj);
				m_selectionCache.insert(id);
			}
			else
			{
				// Move to end (becomes active) — swap both parallel vectors
				auto dist = std::distance(m_selectedIds.begin(), idx);
				auto lastIdx = m_selectedIds.size() - 1;
				if (static_cast<size_t>(dist) != lastIdx)
				{
					std::swap(m_selectedIds[dist], m_selectedIds[lastIdx]);
					std::swap(m_selectedPtrs[dist], m_selectedPtrs[lastIdx]);
				}
			}
		}
		else
		{
			m_selectedIds.clear();
			m_selectedIds.push_back(id);
			m_selectedPtrs.clear();
			m_selectedPtrs.push_back(obj);
			m_selectionCache.clear();
			m_selectionCache.insert(id);
		}
	}

	/**
	 * @brief Deselects an object.
	 *
	 * Removes obj from selection set. If obj was active, the previous
	 * object becomes active.
	 *
	 * @param obj Pointer to object to deselect (nullptr ignored)
	 * @param increment Unused parameter (kept for API consistency)
	 *
	 * @note No-op if obj is not selected
	 */
	void Deselect(T* obj, bool increment)
	{
		(void)increment;
		if (obj == nullptr) return;

		int id = obj->GetObjectID();
		auto idx = std::find(m_selectedIds.begin(), m_selectedIds.end(), id);
		if (idx == m_selectedIds.end()) return;

		auto dist = std::distance(m_selectedIds.begin(), idx);
		m_selectedIds.erase(idx);
		m_selectedPtrs.erase(m_selectedPtrs.begin() + dist);
		m_selectionCache.erase(id);
	}

	/**
	 * @brief Returns the active (last selected) object.
	 *
	 * The active object is the primary selection, typically used for
	 * displaying properties or applying operations.
	 *
	 * @return Pointer to active object, or nullptr if no selection
	 */
	T* GetActiveObject() const
	{
		if (m_selectedPtrs.empty())
			return nullptr;
		return m_selectedPtrs.back();
	}

	/**
	 * @brief Returns the first selected object.
	 *
	 * @return Pointer to first selected object, or nullptr if no selection
	 */
	T* GetSelectedObjects() const
	{
		if (m_selectedPtrs.empty())
			return nullptr;
		return m_selectedPtrs.front();
	}

	/**
	 * @brief Checks if an object is selected.
	 *
	 * Fast O(1) lookup using internal hash set of IDs.
	 *
	 * @param obj Pointer to object to check
	 * @return true if obj is in the selection set
	 */
	bool IsSelected(T* obj) const
	{
		if (obj == nullptr) return false;
		return m_selectionCache.find(obj->GetObjectID()) != m_selectionCache.end();
	}

	/**
	 * @brief Returns the number of selected objects.
	 * @return Count of selected objects.
	 */
	size_t GetSelectionCount() const
	{
		return m_selectedIds.size();
	}

	/**
	 * @brief Clears all selections.
	 */
	void ClearSelection()
	{
		m_selectedIds.clear();
		m_selectedPtrs.clear();
		m_selectionCache.clear();
	}

	// -------------------------------------------------------------------
	// Post-deserialization resolution
	// -------------------------------------------------------------------

	/**
	 * @brief Rebuilds pointer cache and lookup set from deserialized IDs.
	 *
	 * After deserialization, m_selectedIds contains valid IDs but
	 * m_selectedPtrs and m_selectionCache are empty. Call this method
	 * with the scene's object pool (e.g. obj_list) to resolve IDs
	 * to runtime pointers.
	 *
	 * @param pool Map of ID -> shared_ptr<T> containing all live objects.
	 * @note Dangling IDs (pointing to objects no longer in the pool) are
	 *       silently removed from m_selectedIds.
	 */
	void ResolvePointers(const std::unordered_map<int, std::shared_ptr<T>>& pool)
	{
		m_selectedPtrs.clear();
		m_selectionCache.clear();

		for (int id : m_selectedIds)
		{
			auto it = pool.find(id);
			if (it != pool.end())
			{
				m_selectedPtrs.push_back(it->second.get());
				m_selectionCache.insert(id);
			}
		}

		// Remove dangling IDs that could not be resolved
		m_selectedIds.erase(
			std::remove_if(m_selectedIds.begin(), m_selectedIds.end(),
				[this](int id) { return m_selectionCache.find(id) == m_selectionCache.end(); }),
			m_selectedIds.end());
	}

	// -------------------------------------------------------------------
	// Cereal serialization
	// -------------------------------------------------------------------

	/**
	 * @brief Serializes the selection state (IDs only, not runtime pointer caches).
	 * @tparam Archive Cereal archive type (input or output).
	 * @param ar Archive to serialize to/from.
	 */
	template<class Archive>
	void serialize(Archive& ar)
	{
		ar(cereal::make_nvp("selectedIds", m_selectedIds));
	}

private:
	/// Ordered list of selected object IDs (last = active). This is the
	/// serialized payload.
	std::vector<int> m_selectedIds;

	/// Runtime pointer cache parallel to m_selectedIds. Not serialized.
	std::vector<T*> m_selectedPtrs;

	/// Fast ID lookup set for O(1) IsSelected() queries.
	std::unordered_set<int> m_selectionCache;
};
