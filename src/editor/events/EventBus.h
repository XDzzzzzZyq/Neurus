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

	// Non-copyable - singleton semantics
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
	 * @brief Enqueues an event for deferred dispatch.
	 */
	template<typename TEvent>
	void enqueue(const TEvent& event)
	{
		evt_eventQueue.push([this, ev = event]() {
			dispatch(ev);
		});
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
};

inline EventQueue& GetEventQueue()
{
	static EventQueue queue;
	return queue;
}

inline EventQueue& EventQueue()
{
	return GetEventQueue();
}

} // namespace neurus
