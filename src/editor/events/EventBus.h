#pragma once

#include <functional>
#include <queue>
#include <typeindex>
#include <unordered_map>
#include <vector>

// MOC (Qt Meta-Object Compiler) scans all headers in AUTOMOC targets.
// This header contains template code that MOC's limited C++ parser cannot
// handle. Guard everything so MOC sees an empty header.
#ifndef Q_MOC_RUN

namespace neurus {

// ---------------------------------------------------------------------------
// Typed Event Pool - deferred event dispatch with FIFO queue
// ---------------------------------------------------------------------------

/**
 * @brief Type-safe event dispatcher with deferred execution queue.
 *
 * EventPool enables publish-subscribe with compile-time type safety. Components
 * emit events without knowing subscribers, preventing direct coupling. Events
 * are enqueued on emit() and dispatched on Process() in FIFO order.
 *
 * Usage:
 * @code
 *   auto& pool = EventBus();
 *
 *   pool.subscribe<ObjectSelected>([](const ObjectSelected& e) {
 *       inspector.showEntity(e.objectId);
 *   });
 *
 *   pool.emit(ObjectSelected{42});
 *   pool.Process();  // dispatches all queued events
 * @endcode
 *
 * @note Execution model:
 *   - emit() enqueues an event for deferred dispatch.
 *   - Process() drains the queue, dispatching all pending events in FIFO order.
 *   - Events emitted from within a handler are appended to the end of the queue,
 *     ensuring deterministic breadth-first ordering for event chains.
 *   - Call Process() once per frame, after the UI layer has finished rendering.
 *
 * @note Thread-safety: Not thread-safe. All subscribe/emit/Process must occur
 *       on main thread.
 */
class EventPool
{
public:
	EventPool() = default;

	// Non-copyable - singleton semantics
	EventPool(const EventPool&) = delete;
	EventPool& operator=(const EventPool&) = delete;

	/**
	 * @brief Handler function type for events of type TEvent.
	 * @tparam TEvent The event type to handle.
	 */
	template<typename TEvent>
	using Handler = std::function<void(const TEvent&)>;

	/**
	 * @brief Subscribes a handler to events of type TEvent.
	 *
	 * Registers a callback to be invoked whenever an event of type TEvent is
	 * dispatched via Process(). Multiple handlers can subscribe to the same
	 * event type and will be called in subscription order.
	 *
	 * @tparam TEvent The event type to subscribe to.
	 * @param handler Callback function receiving const TEvent&.
	 */
	template<typename TEvent>
	void subscribe(Handler<TEvent> handler)
	{
		auto& vec = m_handlers[std::type_index(typeid(TEvent))];
		vec.push_back(
			[h = std::move(handler)](const void* e) {
				h(*static_cast<const TEvent*>(e));
			}
		);
	}

	/**
	 * @brief Enqueues an event for deferred dispatch.
	 *
	 * The event is copied and stored in the internal queue. It will not be
	 * dispatched until Process() is called. If called from within a handler
	 * during Process(), the new event is appended to the end of the queue
	 * and processed in the same Process() call.
	 *
	 * @tparam TEvent The event type to emit.
	 * @param event The event instance to enqueue (copied into the queue).
	 */
	template<typename TEvent>
	void enqueue(const TEvent& event)
	{
		m_eventQueue.push([this, ev = event]() {
			dispatch(ev);
		});
	}

	/**
	 * @brief Dispatches all enqueued events in FIFO order.
	 *
	 * Processes every event currently in the queue, up to @p maxEvents per
	 * call. Events emitted by handlers during processing are appended to the
	 * end of the queue and handled within the same call, guaranteeing
	 * deterministic breadth-first ordering for event chains.
	 *
	 * @param maxEvents Maximum number of events to dispatch in a single call
	 *                  (default: 1000). Prevents infinite loops from re-entrant
	 *                  emits.
	 * @note Call once per frame, after the UI layer has finished rendering.
	 */
	void Process(int maxEvents = 1000)
	{
		int count = 0;
		while (!m_eventQueue.empty() && count < maxEvents)
		{
			auto fn = std::move(m_eventQueue.front());
			m_eventQueue.pop();
			fn();
			++count;
		}
	}

private:
	/**
	 * @brief Immediately dispatches an event to all registered handlers.
	 * @tparam TEvent The event type to dispatch.
	 * @param event The event instance to pass to handlers.
	 */
	template<typename TEvent>
	void dispatch(const TEvent& event)
	{
		auto it = m_handlers.find(std::type_index(typeid(TEvent)));
		if (it == m_handlers.end()) return;

		for (auto& fn : it->second)
		{
			fn(&event);
		}
	};

	/// Maps event types to type-erased handler lists.
	std::unordered_map<
		std::type_index,
		std::vector<std::function<void(const void*)>>
	> m_handlers;

	/// Queue of pending event dispatch closures.
	std::queue<std::function<void()>> m_eventQueue;
};

// ---------------------------------------------------------------------------
// Singleton Accessor
// ---------------------------------------------------------------------------

/**
 * @brief Returns the global singleton EventPool instance.
 *
 * This is the primary entry point for all cross-layer event communication.
 * Editor and Renderer layers use this to emit and subscribe to typed events
 * without coupling to Qt signals.
 *
 * Usage:
 * @code
 *   auto& pool = EventBus();
 *   pool.subscribe<SceneLoaded>([](const SceneLoaded& e) { ... });
 * @endcode
 */
inline EventPool& EventBus()
{
	static EventPool pool;
	return pool;
}

} // namespace neurus

#endif // Q_MOC_RUN
