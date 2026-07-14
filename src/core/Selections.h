/**
 * @file Selections.h
 * @brief Generic selection state management for objects, using value semantics.
 *
 * Selections provides a type-safe container for tracking user selections.
 * It supports single and multi-selection modes with fast lookup via hash set.
 * Stores T values directly — caller chooses the type (int, ObjectID*, etc.).
 *
 * Architecture:
 * - Template-based for use with any value type
 * - Scene owns Selections<ObjectID*> for scene object selection
 * - Editor delegates to Scene::selections via pointer
 * - UI queries selections to highlight/display selected objects
 * - Controllers mutate selections in response to user input
 *
 * Selection Patterns:
 * - Single select: Replace current selection
 * - Multi-select (increment): Add to selection set
 * - Deselect: Remove from selection set
 * - Active object: Tracked via m_activePtr, separate from selection list
 *
 * @note Core Layer: Selections is part of the core type system
 * @note Thread-safety: Not thread-safe. Must be used from main thread only.
 */

#pragma once

#include <algorithm>
#include <unordered_set>
#include <vector>

#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>

namespace neurus
{

/**
 * @brief Generic selection state manager supporting single and multi-selection,
 *        storing T values directly.
 *
 * Selections maintains an ordered list of selected values with fast
 * O(1) hash lookup. The active object is tracked explicitly via
 * m_activePtr, separate from the selection list ordering.
 *
 * T must be equality-comparable and hashable via std::hash<T>.
 * T{} is used as the "null" sentinel (nullptr for pointers, 0 for ints).
 *
 * Usage Examples:
 * - Selections<int>       for ID-based tests
 * - Selections<ObjectID*> for scene object selection
 * - Selections<Material*> for material selection
 *
 * Selection Modes:
 * - increment=false: Single selection (replace existing)
 * - increment=true: Multi-selection (add to set)
 *
 * @tparam T Value type to store. Must be default-constructible,
 *           equality-comparable, and hashable.
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
	 * @brief Selects a value.
	 *
	 * If increment=false (single selection):
	 * - Clears existing selection
	 * - Adds obj as the only selected value
	 *
	 * If increment=true (multi-selection):
	 * - If obj not in selection: adds to end and becomes active
	 * - If obj already selected: becomes active (moved to end of list)
	 */
	void Select(const T& obj, bool increment)
	{
		if (increment)
		{
			auto it = m_selectionSet.find(obj);
			if (it == m_selectionSet.end())
			{
				m_selectedList.push_back(obj);
				m_selectionSet.insert(obj);
			}
			else
			{
				// Move to end (becomes active) — swap with last element
				auto itVec = std::find(m_selectedList.begin(), m_selectedList.end(), obj);
				auto lastIdx = m_selectedList.size() - 1;
				if (static_cast<size_t>(std::distance(m_selectedList.begin(), itVec)) != lastIdx)
				{
					std::swap(*itVec, m_selectedList.back());
				}
			}
			m_activePtr = obj;
		}
		else
		{
			m_selectedList.clear();
			m_selectedList.push_back(obj);
			m_selectionSet.clear();
			m_selectionSet.insert(obj);
			m_activePtr = obj;
		}
	}

	/**
	 * @brief Deselects a value.
	 *
	 * Removes obj from selection set. If obj was the active object,
	 * the last remaining value becomes active, or T{} if empty.
	 *
	 * @note No-op if obj is not selected
	 */
	void Deselect(const T& obj, bool increment)
	{
		(void)increment;
		auto it = m_selectionSet.find(obj);
		if (it == m_selectionSet.end()) return;

		m_selectionSet.erase(it);

		auto itVec = std::find(m_selectedList.begin(), m_selectedList.end(), obj);
		if (itVec != m_selectedList.end())
		{
			m_selectedList.erase(itVec);
		}

		if (m_activePtr == obj)
		{
			m_activePtr = m_selectedList.empty() ? T{} : m_selectedList.back();
		}
	}

	/**
	 * @brief Returns the active (primary) selected value.
	 *
	 * @return Active value, or T{} if no selection
	 *         (nullptr for pointer types, 0 for int types).
	 */
	T GetActiveObject() const
	{
		if (m_selectedList.empty())
			return T{};
		return m_activePtr;
	}

	/**
	 * @brief Returns the first selected value.
	 *
	 * @return First selected value, or T{} if no selection.
	 */
	T GetSelectedObjects() const
	{
		if (m_selectedList.empty())
			return T{};
		return m_selectedList.front();
	}

	/**
	 * @brief Checks if a value is selected.
	 *
	 * Fast O(1) lookup using internal hash set.
	 */
	bool IsSelected(const T& obj) const
	{
		return m_selectionSet.find(obj) != m_selectionSet.end();
	}

	/**
	 * @brief Returns the number of selected values.
	 */
	size_t GetSelectionCount() const
	{
		return m_selectedList.size();
	}

	/**
	 * @brief Returns the ordered list of selected values.
	 */
	const std::vector<T>& GetSelectedList() const
	{
		return m_selectedList;
	}

	/**
	 * @brief Clears all selections and resets the active value to T{}.
	 */
	void ClearSelection()
	{
		m_selectedList.clear();
		m_selectionSet.clear();
		m_activePtr = T{};
	}

	// -------------------------------------------------------------------
	// Cereal serialization
	// -------------------------------------------------------------------

	/**
	 * @brief Serializes the selection list (ordered list of values).
	 *
	 * For Selections<int> (test usage), this directly serializes int values.
	 * For Selections<ObjectID*> (scene usage), serialization requires
	 * a custom archive or pointer registration — not stored by default.
	 *
	 * @tparam Archive Cereal archive type (input or output).
	 * @param ar Archive to serialize to/from.
	 */
	template<class Archive>
	void serialize(Archive& ar)
	{
		ar(cereal::make_nvp("selectedValues", m_selectedList));

		// On deserialization, rebuild the hash set from the loaded list
		// and set the active pointer to the last element.
		if constexpr (Archive::is_loading::value)
		{
			m_selectionSet.clear();
			m_selectionSet.insert(m_selectedList.begin(), m_selectedList.end());
			m_activePtr = m_selectedList.empty() ? T{} : m_selectedList.back();
		}
	}

private:
	/// Ordered list of selected values (preserves selection order).
	std::vector<T> m_selectedList;

	/// Fast hash lookup set for O(1) IsSelected() queries.
	std::unordered_set<T> m_selectionSet;

	/// Active (primary) selected value, T{} if no selection.
	T m_activePtr = T{};
};

} // namespace neurus
