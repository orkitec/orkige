/**************************************************************
	created:	2026/07/12 at 14:00
	filename: 	ScriptEventBus.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __ScriptEventBus_h__12_7_2026__14_00_00__
#define __ScriptEventBus_h__12_7_2026__14_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"
#include "core_base/Object.h"
#include "core_script/ScriptRuntime.h"		// ScriptCallback (stored by value)
#include "core_script/ScriptEventPayload.h"
#include "core_event/Event.h"
#include "core_event/EventListener.h"

#include <deque>
#include <functional>
#include <map>
#include <vector>

namespace Orkige
{
	//! @brief transient carrier that rides an Event's data (optr<Object>) to
	//! move a bounded ScriptEventPayload through GlobalEventManager. Event data
	//! is never serialized (events are runtime-only), so unlike Value<T> this
	//! carrier deliberately does NOT archive its payload.
	class ORKIGE_CORE_DLL ScriptEventData : public Object
	{
		OOBJECT(ScriptEventData, Object)
	public:
		ScriptEventData() {}
		explicit ScriptEventData(ScriptEventPayload const & payload)
			: mPayload(payload) {}
		virtual ~ScriptEventData() {}
		ScriptEventPayload const & payload() const { return this->mPayload; }
	private:
		ScriptEventPayload mPayload;
	};

	//! @brief the script-facing bridge onto the engine's ONE event bus,
	//! core_event/GlobalEventManager - NOT a parallel event system. Scripts
	//! `events.subscribe(name, fn)` / `events.emit(name, payload)`; engine
	//! producers (gui / physics / app lifecycle) emit the SAME named events. A
	//! script emit becomes a GlobalEventManager `queueEvent`; a subscription
	//! binds ONE C++ adapter EventListener per event name that fans out to the
	//! name's sandbox-scoped Lua handlers. So C++ and Lua share one bus - a C++
	//! listener bound to an event name receives a script-emitted event of that
	//! name, and vice versa.
	//!
	//! DELIVERY RULE. Emissions QUEUE onto GlobalEventManager; nothing runs a
	//! handler inline. The player loop drains the queue ONCE per frame with
	//! `GlobalEventManager::tick()`, placed in the SCRIPT TICK PHASE (right after
	//! the component updates, before tweens/physics). Handlers run in
	//! SUBSCRIPTION ORDER within a name. The manager's DOUBLE-BUFFERED queue is
	//! the cascade-safety: an emit from inside a handler lands in the opposing
	//! buffer and is delivered at the NEXT tick, so a handler can never recurse
	//! into the same drain (no hand-rolled re-entrancy cap - the heritage queue
	//! already guarantees this). An emit that happens after the phase (the
	//! physics contact drain runs later in the tick) is delivered next frame; gui
	//! input is pumped before the script phase, so a widget event is seen the
	//! same frame it was clicked.
	//!
	//! SANDBOX SCOPING. Every subscription is tagged with the CURRENT OWNER - the
	//! script sandbox (ScriptInstance) executing when subscribe() was called, set
	//! through setCurrentOwner around every script entry point and around each
	//! handler dispatch. Retiring a sandbox (component removed, scene torn down,
	//! or a hot-reload swapping the instance) cancelOwner()s its subscriptions.
	//! Idiom: SUBSCRIBE IN init - a hot reload re-runs init and re-subscribes.
	//!
	//! Backend-neutral: the bridge stores ScriptCallbacks and forwards
	//! ScriptEventPayloads through Events; the Lua backend is behind the seam. In
	//! ORKIGE_SCRIPTING=OFF builds it still exists but stays inert (no
	//! ScriptCallback is ever valid) - the C++ bus itself keeps working.
	class ORKIGE_CORE_DLL ScriptEventBus
	{
		//--- Types -------------------------------------------
	public:
		typedef unsigned long long SubscriptionId;	//!< 0 = an invalid handle

		//! a script event flattened for the trace: the event name plus its
		//! top-level scalar fields as strings (like a contact trace event)
		struct FrameEvent
		{
			String name;
			std::vector<std::pair<String, String> > fields;
		};

		//! the log sink the bus reports handler errors through (the engine
		//! installs one that routes to the editor Console / log)
		typedef std::function<void(String const &)> ErrorSink;

		//! @brief RAII: make `owner` the current subscription owner for the
		//! duration of the scope, restoring the previous owner on exit.
		struct ORKIGE_CORE_DLL OwnerScope
		{
			explicit OwnerScope(void const * owner);
			~OwnerScope();
		private:
			void const * mPrevious;
			OwnerScope(OwnerScope const &) = delete;
			OwnerScope & operator=(OwnerScope const &) = delete;
		};
	private:
		//! one live subscription: a script function tagged with its sandbox owner
		struct Subscription
		{
			SubscriptionId	id = 0;
			void const *	owner = NULL;	//!< the sandbox (ScriptInstance) or NULL
			ScriptCallback	callback;
			bool			cancelled = false;	//!< mark-then-compact (dispatch-safe)
		};
		//--- Variables ---------------------------------------
	private:
		std::map<String, std::deque<Subscription> >	mSubscribers;	//!< per-name Lua handler lists
		std::map<String, optr<EventListener> >		mAdapters;		//!< the one C++ GlobalEventManager listener bound per subscribed name
		std::vector<FrameEvent>						mFrameEvents;	//!< this frame's emits (trace capture only)
		SubscriptionId	mNextId = 1;
		void const *	mCurrentOwner = NULL;
		bool			mTraceCapture = false;
		ErrorSink		mErrorSink;
		//--- Methods -----------------------------------------
	public:
		//! the process-wide bridge (self-initialising, deliberately never
		//! destroyed - it always outlives the ScriptRuntime/ScriptInstance dtors
		//! that call into it). Never NULL.
		static ScriptEventBus & getSingleton();
		//! @see getSingleton (never NULL)
		static ScriptEventBus * getSingletonPtr();

		//! @brief subscribe `callback` to the event `name`, tagged with the
		//! current owner. Binds the name's C++ adapter to GlobalEventManager on
		//! the first subscription. @return the handle id (0 when invalid)
		SubscriptionId subscribe(String const & name,
			ScriptCallback const & callback);
		//! @brief cancel one subscription by id. @return true when it was live.
		bool cancel(SubscriptionId id);
		//! is the subscription id still live (the handle:isActive poll)
		bool isSubscribed(SubscriptionId id) const;
		//! @brief cancel EVERY subscription owned by `owner` (a retired sandbox).
		//! @return how many were cancelled.
		int cancelOwner(void const * owner);

		//! @brief emit `name` with `payload`: queue it onto GlobalEventManager
		//! (delivered at the next tick). Dropped cheaply when nobody - Lua OR a
		//! C++ listener - is bound for the name and trace capture is off.
		void emit(String const & name, ScriptEventPayload const & payload);

		//! @brief the C++ adapter GlobalEventManager calls (per subscribed name)
		//! when a queued event of that name is drained: fans the event's payload
		//! out to the name's Lua subscribers in subscription order. Returns false
		//! (never consumes) so other listeners of the name still run.
		bool onBusEvent(Event const & event);

		//! set the current subscription owner (used through OwnerScope)
		void setCurrentOwner(void const * owner) { this->mCurrentOwner = owner; }
		//! @see setCurrentOwner
		void const * currentOwner() const { return this->mCurrentOwner; }

		//! @brief turn per-frame trace capture on/off. While on, emit() records
		//! each event (name + flattened top-level scalars) for the trace
		//! recorder; off, emit() records nothing (zero hot-path cost).
		void setTraceCapture(bool on) { this->mTraceCapture = on; }
		//! @see setTraceCapture
		bool traceCapture() const { return this->mTraceCapture; }
		//! move out (and clear) the events emitted since the last take
		std::vector<FrameEvent> takeFrameEvents();

		//! install the handler-error log sink (the engine routes it to the log)
		void setErrorSink(ErrorSink const & sink) { this->mErrorSink = sink; }

		//! how many LIVE subscribers a name has (introspection / tests)
		std::size_t subscriberCount(String const & name) const;
		//! @brief drop EVERY subscription (unbinding each adapter) and release the
		//! held script callbacks NOW - called before the scripting backend tears
		//! its Lua state down, so no sol reference outlives the state
		void clear();
	private:
		friend struct OwnerScope;
		ScriptEventBus();
		~ScriptEventBus();
		ScriptEventBus(ScriptEventBus const &) = delete;
		ScriptEventBus & operator=(ScriptEventBus const &) = delete;
		//! bind the name's adapter to GlobalEventManager if not already bound
		void ensureAdapter(String const & name);
		//! report an error through the sink (a no-op when none is installed)
		void reportError(String const & message);
		//! erase cancelled entries from a name's list; release the adapter when
		//! the list becomes empty
		void compact(String const & name);
	};

	//! @brief the value handle `events.subscribe` returns to a script: a tiny
	//! id wrapper exposing `handle:cancel()` and `handle:isActive()`. Registered
	//! as a Lua usertype in core_module/module.cpp, like TweenHandle.
	struct ORKIGE_CORE_DLL EventSubscription
	{
		ScriptEventBus::SubscriptionId	mId = 0;	//!< 0 = an invalid handle

		//! cancel the subscription. @return true when it was still live
		bool cancel();
		//! is the subscription still live (the completion poll)
		bool isActive() const;
	};
}

#endif //__ScriptEventBus_h__12_7_2026__14_00_00__
