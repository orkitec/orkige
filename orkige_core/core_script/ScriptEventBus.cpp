/**************************************************************
	created:	2026/07/12 at 14:00
	filename: 	ScriptEventBus.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_script/ScriptEventBus.h"
#include "core_event/GlobalEventManager.h"
#include "core_event/EventType.h"
#include "core_debug/MemoryManager.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- ScriptEventData -------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(ScriptEventData)
		OCONSTRUCTOR0()
	OOBJECT_END
	//---------------------------------------------------------
	//--- OwnerScope ------------------------------------------
	//---------------------------------------------------------
	ScriptEventBus::OwnerScope::OwnerScope(void const * owner)
	{
		ScriptEventBus & bus = ScriptEventBus::getSingleton();
		this->mPrevious = bus.currentOwner();
		bus.setCurrentOwner(owner);
	}
	//---------------------------------------------------------
	ScriptEventBus::OwnerScope::~OwnerScope()
	{
		ScriptEventBus::getSingleton().setCurrentOwner(this->mPrevious);
	}
	//---------------------------------------------------------
	//--- ScriptEventBus --------------------------------------
	//---------------------------------------------------------
	ScriptEventBus::ScriptEventBus()
	{
	}
	//---------------------------------------------------------
	ScriptEventBus::~ScriptEventBus()
	{
	}
	//---------------------------------------------------------
	ScriptEventBus & ScriptEventBus::getSingleton()
	{
		// self-initialising process-wide instance. DELIBERATELY LEAKED (a
		// never-destroyed heap object) so it always outlives every other static -
		// the ScriptRuntime destructor and the ScriptInstance destructors call
		// into it during teardown, and a function-local static could be destroyed
		// before them (a static-destruction-order crash). ScriptRuntime::~clears
		// the held script callbacks before the Lua state closes.
		static ScriptEventBus * instance = new ScriptEventBus();
		return *instance;
	}
	//---------------------------------------------------------
	ScriptEventBus * ScriptEventBus::getSingletonPtr()
	{
		return &ScriptEventBus::getSingleton();
	}
	//---------------------------------------------------------
	ScriptEventBus::SubscriptionId ScriptEventBus::subscribe(
		String const & name, ScriptCallback const & callback)
	{
		if (!callback.valid())
		{
			// nothing to call (nil / not a function / scripting off): an honest
			// invalid handle, never a live subscription
			return 0;
		}
		Subscription subscription;
		subscription.id = this->mNextId++;
		subscription.owner = this->mCurrentOwner;
		subscription.callback = callback;
		subscription.cancelled = false;
		// per-name list, append: delivery order == subscription order
		this->mSubscribers[name].push_back(subscription);
		// bind the C++ adapter that GlobalEventManager will call for this name
		this->ensureAdapter(name);
		return subscription.id;
	}
	//---------------------------------------------------------
	bool ScriptEventBus::cancel(SubscriptionId id)
	{
		if (id == 0)
		{
			return false;
		}
		for (auto & entry : this->mSubscribers)
		{
			for (Subscription & subscription : entry.second)
			{
				if (subscription.id == id && !subscription.cancelled)
				{
					// mark-then-compact: a handle:cancel() may fire from inside a
					// handler (mid GlobalEventManager::tick), so only mark here;
					// the deque compaction happens without touching the manager's
					// listener registry (the adapter stays bound - see ensureAdapter)
					subscription.cancelled = true;
					this->compact(entry.first);
					return true;
				}
			}
		}
		return false;
	}
	//---------------------------------------------------------
	bool ScriptEventBus::isSubscribed(SubscriptionId id) const
	{
		if (id == 0)
		{
			return false;
		}
		for (auto const & entry : this->mSubscribers)
		{
			for (Subscription const & subscription : entry.second)
			{
				if (subscription.id == id)
				{
					return !subscription.cancelled;
				}
			}
		}
		return false;
	}
	//---------------------------------------------------------
	int ScriptEventBus::cancelOwner(void const * owner)
	{
		if (owner == NULL)
		{
			return 0;	// owner-less (console) subscriptions are never swept
		}
		int cancelled = 0;
		for (auto & entry : this->mSubscribers)
		{
			for (Subscription & subscription : entry.second)
			{
				if (subscription.owner == owner && !subscription.cancelled)
				{
					subscription.cancelled = true;
					++cancelled;
				}
			}
			this->compact(entry.first);
		}
		return cancelled;
	}
	//---------------------------------------------------------
	void ScriptEventBus::emit(String const & name,
		ScriptEventPayload const & payload)
	{
		// trace capture (recorder only): record the emission regardless of
		// whether anyone is listening - the trace is a readback of what happened
		if (this->mTraceCapture)
		{
			FrameEvent frameEvent;
			frameEvent.name = name;
			payload.flattenScalars(frameEvent.fields);
			this->mFrameEvents.push_back(std::move(frameEvent));
		}
		GlobalEventManager * manager = GlobalEventManager::getSingletonPtr();
		if (!manager)
		{
			return;	// no bus booted (a raw unit context) - honest no-op
		}
		// QUEUE onto the ONE engine bus: delivered at the next
		// GlobalEventManager::tick() (the player loop's script phase). The
		// manager drops the event itself when no listener - Lua adapter OR a C++
		// listener - is bound for the name, so an unheard event never dispatches.
		// tracked allocation seam: an emit mints the event + payload objects
		// (the queue node itself is counted at the manager's push)
		MemoryManager::countAlloc(MemoryManager::TAG_EVENTS);
		manager->queueEvent(onew(new Event(EventType(name),
			onew(new ScriptEventData(payload)))));
	}
	//---------------------------------------------------------
	bool ScriptEventBus::onBusEvent(Event const & event)
	{
		const String & name = event.getType().getName();
		std::map<String, std::deque<Subscription> >::iterator it =
			this->mSubscribers.find(name);
		if (it == this->mSubscribers.end())
		{
			return false;	// no Lua subscribers (a C++-only listener chain)
		}
		// the payload rides the event as a ScriptEventData; a null/foreign data
		// object yields an empty payload (an event carrying no data)
		const ScriptEventPayload emptyPayload;
		optr<ScriptEventData> data =
			std::dynamic_pointer_cast<ScriptEventData>(event.getData());
		ScriptEventPayload const & payload =
			data ? data->payload() : emptyPayload;

		// snapshot the count: a subscribe() from a handler appends (std::deque
		// keeps references to existing elements valid on push_back), and the new
		// subscription fires on the NEXT event, not this one
		std::deque<Subscription> & list = it->second;
		const std::size_t count = list.size();
		for (std::size_t i = 0; i < count; ++i)
		{
			Subscription & subscription = list[i];
			if (subscription.cancelled)
			{
				continue;
			}
			// attribute a subscribe() made from inside the handler to the
			// handler's OWN sandbox
			this->setCurrentOwner(subscription.owner);
			String error;
			const bool ok =
				subscription.callback.invokePayload(payload, &error);
			this->setCurrentOwner(NULL);
			if (!ok)
			{
				// a handler error is logged once and the subscription dropped
				// (never per-frame spam); the owning script keeps running
				this->reportError("event handler for '" + name + "': " + error +
					" (unsubscribed)");
				subscription.cancelled = true;
			}
		}
		// erase everything cancelled during the fan-out (the deque is our own
		// container; we never touch the manager's listener registry mid-tick)
		this->compact(name);
		return false;	// never consume - other listeners of the name still run
	}
	//---------------------------------------------------------
	std::vector<ScriptEventBus::FrameEvent> ScriptEventBus::takeFrameEvents()
	{
		std::vector<FrameEvent> events;
		events.swap(this->mFrameEvents);
		return events;
	}
	//---------------------------------------------------------
	std::size_t ScriptEventBus::subscriberCount(String const & name) const
	{
		std::map<String, std::deque<Subscription> >::const_iterator it =
			this->mSubscribers.find(name);
		if (it == this->mSubscribers.end())
		{
			return 0;
		}
		std::size_t live = 0;
		for (Subscription const & subscription : it->second)
		{
			if (!subscription.cancelled)
			{
				++live;
			}
		}
		return live;
	}
	//---------------------------------------------------------
	void ScriptEventBus::clear()
	{
		// release every adapter from GlobalEventManager, then drop all state
		// (this runs at ScriptRuntime teardown, outside any tick, so unbinding is
		// safe here). Releasing the ScriptCallbacks frees their sol references
		// while the Lua state is still open.
		if (GlobalEventManager::getSingletonPtr())
		{
			GlobalEventManager & manager = GlobalEventManager::getSingleton();
			for (auto & entry : this->mAdapters)
			{
				manager.delListener(entry.second, EventType(entry.first));
			}
		}
		this->mAdapters.clear();
		this->mSubscribers.clear();
		this->mFrameEvents.clear();
		this->mCurrentOwner = NULL;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	void ScriptEventBus::ensureAdapter(String const & name)
	{
		if (this->mAdapters.find(name) != this->mAdapters.end())
		{
			return;	// already bound
		}
		GlobalEventManager * manager = GlobalEventManager::getSingletonPtr();
		if (!manager)
		{
			return;	// no bus yet - the adapter binds on a later subscribe
		}
		// ONE C++ listener per name, all routed to onBusEvent (which reads the
		// event's name and fans out). Kept bound for the process (released only
		// in clear) so we never delListener during a tick dispatch.
		this->mAdapters[name] =
			manager->bind(EventType(name), &ScriptEventBus::onBusEvent, this);
	}
	//---------------------------------------------------------
	void ScriptEventBus::reportError(String const & message)
	{
		if (this->mErrorSink)
		{
			this->mErrorSink(message);
		}
	}
	//---------------------------------------------------------
	void ScriptEventBus::compact(String const & name)
	{
		std::map<String, std::deque<Subscription> >::iterator it =
			this->mSubscribers.find(name);
		if (it == this->mSubscribers.end())
		{
			return;
		}
		std::deque<Subscription> & list = it->second;
		for (std::size_t i = 0; i < list.size(); )
		{
			if (list[i].cancelled)
			{
				list.erase(list.begin() + i);
			}
			else
			{
				++i;
			}
		}
		// the adapter stays bound even when the list empties (releasing it here
		// could delListener during a tick); it is a cheap no-op with no
		// subscribers and is dropped wholesale in clear()
	}
	//---------------------------------------------------------
	//--- EventSubscription -----------------------------------
	//---------------------------------------------------------
	bool EventSubscription::cancel()
	{
		return ScriptEventBus::getSingleton().cancel(this->mId);
	}
	//---------------------------------------------------------
	bool EventSubscription::isActive() const
	{
		return ScriptEventBus::getSingleton().isSubscribed(this->mId);
	}
}
