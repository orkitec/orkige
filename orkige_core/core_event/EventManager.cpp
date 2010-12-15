/**************************************************************
	created:	2010/07/26 at 13:52
	filename: 	EventManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/

#include "core_event/EventManager.h"
#include "core_util/Timer.h"
#include "core_debug/Profile.h"

#ifdef ORKIGE_NDS
#include <extras.h>
#endif

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	EventManager::EventManager() :  activeQueue(0)
	{
	}
	//---------------------------------------------------------
	EventManager::~EventManager()
	{
		for ( EventListenerMap::iterator it = this->registry.begin(), itEnd = this->registry.end(); it != itEnd; ++it )
		{
			EventType::TypeId const & kEventId = it->first;
			EventListenerTable & table    = it->second;
			table.clear();
		}
		this->registry.clear();
		this->activeQueue = 0;
		//oDebugMsg("eventmanager",0,"EventManager destroyed!");
	}
	//---------------------------------------------------------
	optr<EventListener> EventManager::bind(EventType const & inType,EventHandlerFunction const & handler)
	{
		//oDebugMsg("eventmanager",0,inType.getName() + String(" bound to EventListener("<<this->getObjectID()<<")!"));
		optr<EventListener> listener = onew(new EventListener(handler));
		if ( ! this->addListener(listener,	inType ) )
		{
			oAssert(!"Could Not Add Listener");
		}
		return listener;
	}
	//---------------------------------------------------------
	bool EventManager::addListener (optr<EventListener> & inListener, EventType const & inType )
	{
		if ( ! this->validateType( inType ) )
			return false;

		// check / update type list
		EventTypeSet::iterator evIt = this->typeList.find( inType );

		if ( evIt == this->typeList.end() )
		{
			// was not in the list, add it ...

 			EventTypeSetIRes ires = this->typeList.insert( inType );

			// insert failed for some reason
			if ( ires.second == false )
				return false;

			// somehow we inserted and left the list empty!?!?!
			if ( ires.first == this->typeList.end() )
				return false;

			evIt = ires.first; // store for later use
		}

		// find listener map entry, create one if no table already
		// exists for this entry ...
		EventListenerMap::iterator elmIt = this->registry.find( inType.getId() );

		if ( elmIt == this->registry.end() )
		{
			EventListenerMapIRes elmIRes = this->registry.insert(
				EventListenerMapEnt( inType.getId(),
				EventListenerTable() ) );

			// whoops, could not insert into map!?!?
			if ( elmIRes.second == false )
				return false;

			// should not be possible, how did we insert and create
			// an empty table!?!?!
			if ( elmIRes.first == this->registry.end() )
				return false;

			// store it so we can update the mapped list next ...
			elmIt = elmIRes.first;
		}

		// update the mapped list of listeners, walk the existing
		// list (if any entries) to prevent duplicate addition of
		// listeners. This is a bit more costly at registration time
		// but will prevent the hard-to-notice duplicate event
		// propagation sequences that would happen if double-entries
		// were allowed.

		// note: use reference to make following code more simple
		EventListenerTable & evlTable = (*elmIt).second;

		for ( EventListenerTable::iterator it = evlTable.begin(), itEnd = evlTable.end(); it != itEnd ; ++it )
		{
			bool bListenerMatch = ( (*it) == inListener );

			if ( bListenerMatch )
				return false;
		}

		// okay, event type validated, event listener validated,
		// event listener not already in map, add it

		evlTable.push_back( inListener );

		//sort the listeners by their priority
		evlTable.sort(EventListenerOptrCmp());
		return true;
	}
	//---------------------------------------------------------
	bool EventManager::delListener (optr<EventListener> const & inListener, EventType const & inType )
	{
		if ( ! this->validateType( inType ) )
			return false;

		bool rc = false;

		// brute force method, iterate through all existing mapping
		// entries looking for the matching listener and remove it.
		for ( EventListenerMap::iterator it = this->registry.begin(), itEnd = this->registry.end(); it != itEnd; ++it )
		{
			EventType::TypeId const & kEventId = it->first;
			EventListenerTable & table    = it->second;

			for ( EventListenerTable::iterator it2 = table.begin(), it2End = table.end(); it2 != it2End; ++it2 )
			{
				if ( inListener == (*it2))
				{
					// found match, remove from table,
					table.erase( it2 );

					// update return code
					rc = true;

					// and early-quit the inner loop as addListener()
					// code ensures that each listener can only
					// appear in one event's processing list once.
					break;
				}
			}
		}

		return rc;
	}
	//---------------------------------------------------------
	bool EventManager::trigger (Event const & inEvent) const
	{
		OPROFILE(String(__FUNCTION__) + "( " + inEvent.getObjectID() + " )");
		//if ( ! this->validateType( inEvent.getType() ) )
		//	return false;

		if(this->registry.empty())
			return false;

		const EventListenerMap::const_iterator it = this->registry.find( inEvent.getType().getId() );

		if ( it == this->registry.end() )
			return false;

		EventListenerTable const & table = it->second;

		
		for ( EventListenerTable::const_iterator it2 = table.begin(), it2End = table.end(); it2 != it2End; ++it2 )
		{
			// only set to true, if processing eats the messages
			if((*it2)->eventHandlerFunction(inEvent))
			{
				return true;
			}
		}

		return false;
	}
	//---------------------------------------------------------
	bool EventManager::queueEvent ( optr<Event> const & inEvent )
	{
		oAssert ( this->activeQueue >= 0 );
		oAssert ( this->activeQueue < NumEventQueues );

		if ( ! this->validateType( inEvent->getType() ) )
			return false;

		EventListenerMap::const_iterator it = this->registry.find( inEvent->getType().getId() );

		if ( it == this->registry.end() )
		{		
			// if global listener is not active, then abort queue add
			EventListenerMap::const_iterator itWC = this->registry.find( 0 );

			if ( itWC == this->registry.end() )
			{
				// no listeners for this event, skipit
				return false;
			}
		}

		this->queues[this->activeQueue].push_back( inEvent );

		return true;
	}
	//---------------------------------------------------------
	bool EventManager::abortEvent ( EventType const & inType, bool allOfType )
	{
		oAssert ( this->activeQueue >= 0 );
		oAssert ( this->activeQueue < NumEventQueues );

		if ( ! this->validateType( inType ) )
			return false;

		EventListenerMap::iterator it = this->registry.find( inType.getId() );

		if ( it == this->registry.end() )
			return false; // no listeners for this event, skipit

		bool rc = false;

		for ( EventQueue::iterator it = this->queues[this->activeQueue].begin(), itEnd = this->queues[this->activeQueue].end(); it != itEnd; ++it )
		{
			if ( (*it)->getType() == inType )
			{
				this->queues[this->activeQueue].erase(it);
				rc = true;
				if ( !allOfType )
					break;
			}
		}

		return rc;
	}
	//---------------------------------------------------------	
	bool EventManager::tick ( unsigned long maxMillis )
	{
		OPROFILEFUNC();
		unsigned long curMs = Timer::getMilliseconds();
		unsigned long maxMs = maxMillis == EventManager::InfiniteProcessTime ? EventManager::InfiniteProcessTime : (curMs + maxMillis );

		EventListenerMap::const_iterator itWC = this->registry.find( 0 );

		// swap active queues, make sure new queue is empty after the
		// swap ...

		int queueToProcess = this->activeQueue;

		this->activeQueue = ( this->activeQueue + 1 ) % NumEventQueues;

		this->queues[this->activeQueue].clear();

		// now process as many events as we can ( possibly time
		// limited ) ... always do AT LEAST one event, if ANY are
		// available ...

		while ( this->queues[queueToProcess].size() > 0 )
		{
			optr<Event> event = this->queues[queueToProcess].front();

			this->queues[queueToProcess].pop_front();

			EventType const & eventType = event->getType();

			EventListenerMap::const_iterator itListeners =
				this->registry.find( eventType.getId() );

			if ( itWC != this->registry.end() )
			{
				EventListenerTable const & table = itWC->second;

				bool processed = false;

				for ( EventListenerTable::const_iterator it2 = table.begin(), it2End = table.end(); it2 != it2End; ++it2 )
				{
					(*it2)->eventHandlerFunction( *event );
				}
			}

			// no listerners currently for this event type, skipit
			if ( itListeners == this->registry.end() )
				continue;

			EventType::TypeId const & kEventId = itListeners->first;
			EventListenerTable const & table = itListeners->second;

			for ( EventListenerTable::const_iterator it = table.begin(), end = table.end(); it != end ; ++it)
			{
				if ( (*it)->eventHandlerFunction( *event ) )
				{
					break;
				}
			}

			curMs = Timer::getMilliseconds();

			if ( maxMillis != EventManager::InfiniteProcessTime )
			{

				if ( curMs >= maxMs )
				{
					// time ran about, abort processing loop
					break;
				}
			}
		}

		// if any events left to process, push them onto the active
		// queue.
		//
		// Note: to preserver sequencing, go bottom-up on the
		// raminder, inserting them at the head of the active
		// queue...

		bool queueFlushed = ( this->queues[queueToProcess].size() == 0 );

		if ( !queueFlushed )
		{
			while ( this->queues[queueToProcess].size() > 0 )
			{
				optr<Event> event = this->queues[queueToProcess].back();

				this->queues[queueToProcess].pop_back();

				this->queues[this->activeQueue].push_front( event );
			}
		}

		// all done, this pass

		return queueFlushed;
	}
	//---------------------------------------------------------
	bool EventManager::validateType( EventType const & inType ) const
	{
		OPROFILEFUNC();
		if ( inType.getName().empty() )
			return false;

		if ( ( inType.getId() == 0 ) && (inType.getName().compare(WildcardEventType) != 0) )
			return false;

		EventTypeSet::const_iterator evIt =	this->typeList.find( inType );

		if ( evIt != this->typeList.end() )
		{
			// verify that the text signature is the same as already
			// known ...

			EventType const & known = *evIt;

			// tag mismatch for ident value, not accepted
			if( known.getId() != inType.getId())
				return false;

		}

		return true;
	}
	//---------------------------------------------------------
	EventListenerList EventManager::getListenerList ( EventType const & eventType ) const
	{
		// invalid event type, so sad
		if ( ! this->validateType( eventType ) )
			return EventListenerList();

		EventListenerMap::const_iterator itListeners = this->registry.find( eventType.getId() );

		// no listerners currently for this event type, so sad
		if ( itListeners == this->registry.end() )
			return EventListenerList();

		EventListenerTable const & table = itListeners->second;

		// there was, but is not now, any listerners currently for
		// this event type, so sad
		if ( table.size() == 0 )
			return EventListenerList();

		EventListenerList result;

		result.reserve( table.size() );

		for ( EventListenerTable::const_iterator it = table.begin(), end = table.end(); it != end ; ++it )
		{
			result.push_back( *it );
		}

		return result;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(EventManager)
		OCONSTRUCTOR0()
		OFUNC(addListener)
		OFUNC(delListener)
		OFUNC(trigger)
#ifdef ORKIGE_NDS
		OFUNCOVERL(bind,optr<EventListener>(EventManager::*)(String const &,EventHandlerFunction const &))
#else
		OFUNC(bind)	
#endif

	OOBJECT_END
}
