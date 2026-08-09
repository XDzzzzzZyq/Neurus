/**
 * @file IEventQueue.h
 * @brief Controller-facing interface for the typed event dispatcher.
 *
 * Controllers depend only on this narrow interface, never on the concrete
 * EventQueue: they can subscribe handlers, enqueue events for deferred
 * dispatch (the frame-synchronization boundary), and emitNow events for
 * synchronous replay (operation undo/redo). The queue-management surface
 * (Process, PendingCount) stays out of the interface so controllers cannot
 * drive dispatch.
 *
 * subscribe/enqueue/emitNow are non-virtual template wrappers that forward to
 * the protected virtual implementation hooks, so the concrete EventQueue stays
 * a type-erased dispatch table while the interface remains template-friendly
 * (mirrors the IOperationSink dependency-inversion pattern).
 */

#pragma once

#include <functional>
#include <typeindex>
#include <utility>

namespace neurus {

/**
 * @brief Minimal event dispatch surface exposed to controllers.
 */
class IEventQueue
{
public:
	virtual ~IEventQueue() = default;

	/**
	 * @brief Subscribes a handler to events of type TEvent.
	 */
	template<typename TEvent>
	void subscribe(std::function<void(const TEvent&)> handler)
	{
		subscribeImpl(std::type_index(typeid(TEvent)),
			[h = std::move(handler)](const void* e) {
				h(*static_cast<const TEvent*>(e));
			}
		);
	}

	/**
	 * @brief Enqueues an event for DEFERRED dispatch (frame-synchronization
	 *        boundary). Enqueued events are drained by Process() at one point
	 *        per frame (Editor::Edit()), so all scene mutation happens at a
	 *        single predictable point. Use this for everything EXCEPT
	 *        operation replay.
	 */
	template<typename TEvent>
	void enqueue(const TEvent& event)
	{
		std::type_index ti(typeid(TEvent));
		enqueueImpl([this, ti, ev = event]() {
			dispatch(ti, &ev);
		});
	}

	/**
	 * @brief Dispatches an event SYNCHRONOUSLY, bypassing the deferred queue.
	 *
	 * Reserved for operation replay (Undo/Redo): the synthesized inverse event
	 * must apply in-place so it cannot be reordered against the user's
	 * still-queued events. Do NOT use this for real-time input; that must go
	 * through enqueue()/Process().
	 *
	 * @note Named emitNow (not "emit") because Qt reserves `emit` as a keyword
	 *       macro; this header is included in Qt translation units.
	 */
	template<typename TEvent>
	void emitNow(const TEvent& event)
	{
		dispatch(std::type_index(typeid(TEvent)), &event);
	}

protected:
	/** @brief Registers a type-erased handler for a TEvent type. */
	virtual void subscribeImpl(std::type_index type, std::function<void(const void*)> handler) = 0;

	/** @brief Queues a deferred dispatch thunk. */
	virtual void enqueueImpl(std::function<void()> thunk) = 0;

	/** @brief Synchronously invokes every handler registered for @p type. */
	virtual void dispatch(std::type_index type, const void* event) = 0;
};

} // namespace neurus
