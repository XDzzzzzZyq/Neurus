#pragma once

#include <functional>
#include <queue>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace neurus {

class EventQueue
{
public:
	EventQueue() = default;

	// Non-copyable - moved via Application ownership
	EventQueue(const EventQueue&) = delete;
	EventQueue& operator=(const EventQueue&) = delete;

	/**
	 * @brief Subscribes a handler to events of type TEvent.
	 */
	template<typename TEvent>
	void subscribe(std::function<void(const TEvent&)> handler)
	{
		auto& vec = evt_handlers[std::type_index(typeid(TEvent))];
		vec.push_back(
			[h = std::move(handler)](const void* e) {
				h(*static_cast<const TEvent*>(e));
			}
		);
	}

	/**
	 * @brief Enqueues an event for DEFERRED dispatch (frame-synchronization boundary).
	 *
	 * This is the default path for real-time input and downstream notifications.
	 * Enqueued events are drained together by Process() at one point per frame
	 * (Editor::Edit()), so all scene mutation happens at a single predictable
	 * point. Use this for everything EXCEPT operation replay.
	 */
	template<typename TEvent>
	void enqueue(const TEvent& event)
	{
		evt_eventQueue.push([this, ev = event]() {
			this->dispatch(ev);
		});
	}

	/**
	 * @brief Dispatches an event SYNCHRONOUSLY, bypassing the deferred queue.
	 *
	 * Handlers run immediately, nested in the caller. This is reserved for
	 * operation replay (Undo/Redo): the synthesized inverse event must apply
	 * in-place so it cannot be reordered against the user's still-queued events
	 * (a correctness requirement — see the Operation system design). Do NOT use
	 * this for real-time input; that must go through enqueue()/Process().
	 *
	 * @note Named EmitNow (not "emit") because Qt reserves `emit` as a keyword
	 *       macro; this header is included in Qt translation units.
	 */
	template<typename TEvent>
	void EmitNow(const TEvent& event)
	{
		this->dispatch(event);
	}

	/**
	 * @brief Dispatches all enqueued events in FIFO order.
	 */
	void Process(int maxEvents = 1000)
	{
		int count = 0;
		while (!evt_eventQueue.empty() && count < maxEvents)
		{
			auto fn = std::move(evt_eventQueue.front());
			evt_eventQueue.pop();
			fn();
			++count;
		}
	}

private:
	template<typename TEvent>
	void dispatch(const TEvent& event)
	{
		auto it = evt_handlers.find(std::type_index(typeid(TEvent)));
		if (it == evt_handlers.end()) return;

		for (auto& fn : it->second)
		{
			fn(&event);
		}
	}

	std::unordered_map<
		std::type_index,
		std::vector<std::function<void(const void*)> >
	> evt_handlers;

	std::queue<std::function<void()> > evt_eventQueue;
	/**
	 * @brief Returns the number of pending events in the queue.
	 */
	size_t PendingCount() const { return evt_eventQueue.size(); }
};

} // namespace neurus
