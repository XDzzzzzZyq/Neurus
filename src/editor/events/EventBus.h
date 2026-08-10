#pragma once

#include <functional>
#include <queue>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "editor/events/IEventQueue.h"

namespace neurus {

class EventQueue : public IEventQueue
{
public:
	EventQueue() = default;

	// Non-copyable - moved via Application ownership
	EventQueue(const EventQueue&) = delete;
	EventQueue& operator=(const EventQueue&) = delete;

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

	/**
	 * @brief Returns the number of pending events in the queue.
	 */
	size_t PendingCount() const { return evt_eventQueue.size(); }

protected:
	void subscribeImpl(std::type_index type, std::function<void(const void*)> handler) override
	{
		evt_handlers[type].push_back(std::move(handler));
	}

	void enqueueImpl(std::function<void()> thunk) override
	{
		evt_eventQueue.push(std::move(thunk));
	}

	void dispatch(std::type_index type, const void* event) override
	{
		auto it = evt_handlers.find(type);
		if (it == evt_handlers.end()) return;

		for (auto& fn : it->second)
		{
			fn(event);
		}
	}

private:
	std::unordered_map<
		std::type_index,
		std::vector<std::function<void(const void*)> >
	> evt_handlers;

	std::queue<std::function<void()> > evt_eventQueue;
};

} // namespace neurus
