/**************************************************************
	created:	2010/07/26 at 13:42
	filename: 	EventManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __EventManager_h__26_7_2010__13_42_12__
#define __EventManager_h__26_7_2010__13_42_12__

#include "core_event/EventListener.h"
#include "core_event/EventType.h"
#include "core_event/Event.h"
#include <boost/function.hpp>

namespace Orkige
{
	//! vector with EventEventListeners
	typedef std::vector< optr<EventListener> >		EventListenerList;
	//! vector with EventType definitions
	typedef std::vector<EventType>				EventTypeList;
	//! manages events
	class ORKIGE_CORE_DLL EventManager : public Object
	{
		OOBJECT(EventManager,Object)
		//--- Types -------------------------------------------
	public:
	protected:
		typedef std::set< EventType >								EventTypeSet;			//!< one global instance
		typedef std::pair< EventTypeSet::iterator, bool >			EventTypeSetIRes;		//!< insert result into event type set
		typedef std::list< optr<EventListener> >					EventListenerTable;		//!< one list per event type ( stored in the map )
		typedef std::map< EventType::TypeId , EventListenerTable >	EventListenerMap;		//!< mapping of event ident to listener list	
		typedef std::pair< EventType::TypeId, EventListenerTable >	EventListenerMapEnt;	//!< entry in the event listener map
		typedef std::pair< EventListenerMap::iterator, bool >		EventListenerMapIRes;	//!< insert result into listener map	
		typedef std::list< optr<Event> >							EventQueue;				//!< queue of pending- or processing-events
		//! queue constants
		enum Constants
		{
			NumEventQueues = 2,
			InfiniteProcessTime = 0xffffffff
		};
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		EventTypeSet		typeList;           //!< list of registered event types
		EventListenerMap	registry;           //!< mapping of event types to listeners
		EventQueue			queues[NumEventQueues]; //!< event processing queue, double buffered to prevent infinite cycles
		int					activeQueue;        //!< valid denoting which queue is actively processing, en-queing events goes to the opposing queue
	private:
		//--- Methods -----------------------------------------
	public:
		//! constructor
		explicit EventManager();
		//! destructor
		virtual ~EventManager();

		//! @brief Bind a function to an event.
		//!
		//! assert on failure
		//! @return optr<EventListener> an pointer to an Eventlistener that can be used to remove the listener when it is no longer needed
		optr<EventListener> bind(EventType const & inType, EventHandlerFunction const & handlerFunction);

		//! @brief Bind a class member function to an event.
		//!
		//! This function is used from c++ only
		//! assert on failure
		//! @param inType the EventType
		//! @param handlerFunction function that handles the event
		//! @param handlerClass class holding the handlerFunction
		//! @return optr<EventListener> an pointer to an Eventlistener that can be used to remove the listener when it is no longer needed
		template<class F,class T>
		optr<EventListener> bind(EventType const & inType, F const & handlerFunction, T * const handlerClass);

		//! @brief Register a listener for a specific event type, implicitly
		//! the event type will be added to the known event types if
		//! not already known.
		//!
		//! @return false on failure for any reason. 
		//!			The only really anticipated failure reason is if the input event type is bad 
		//!			( e.g.: known-ident number with different signature text, or signature text is empty)
		bool addListener ( optr<EventListener> & inListener, EventType const & inType );

		//! @brief Remove a listener/type pairing from the internal tables
		//!
		//! @return false if the pairing was not found.
		bool delListener ( optr<EventListener> const & inListener, EventType const & inType );

		//! @brief Fire off event - synchronous - do it NOW kind of thing -
		//! analogous to Win32 SendMessage() API.
		//!
		//! @return true if the event was consumed, false if not. Note
		//! that it is acceptable for all event listeners to act on an
		//! event and not consume it, this return signature exists to
		//! allow complete propagation of that shred of information
		//! from the internals of this system to outside users.
		bool trigger ( Event const & inEvent ) const;

		//! @brief Fire off event - asynchronous - do it WHEN the event
		//! system tick() method is called, normally at a judicious
		//! time during game-loop processing.
		//!
		//! @return true if the message was added to the processing
		//! queue, false otherwise.
		bool queueEvent ( optr<Event> const & inEvent );

		//! @brief Find the next-available instance of the named event type
		//! and remove it from the processing queue.
		//!
		//! This may be done up to the point that it is actively being
		//! processed ...  e.g.: is safe to happen during event
		//! processing itself.
		//!
		//! if 'allOfType' is input true, then all events of that type
		//! are cleared from the input queue.
		//!
		//! @return true if the event was found and removed, false
		//! otherwise
		bool abortEvent ( EventType const & inType,	bool allOfType );

		//! @brief Allow for processing of any queued messages.
		//! optionally specify a processing time limit so that the event
		//! processing does not take too long. Note the danger of
		//! using this artificial limiter is that all messages may not
		//! in fact get processed.
		//!
		//! @return true if all messages ready for processing were
		//! completed, false otherwise (e.g. timeout )
		bool tick ( unsigned long maxMillis = EventManager::InfiniteProcessTime);

		// --- information lookup functions ---

		//! @brief Validate an event type, this does NOT add it to the
		//! internal registry, only verifies that it is legal (
		//! e.g. either the ident number is not yet assigned, or it is
		//! assigned to matching signature text, and the signature
		//! text is not empty ).
		bool validateType( EventType const & inType ) const;

		//! Get the list of listeners associated with a specific event type
		EventListenerList getListenerList ( EventType const & eventType ) const;
	protected:
	private:
	};
	//---------------------------------------------------------
	template<class F,class T>
	optr<EventListener> EventManager::bind(EventType const & inType, F const & handlerFunction, T * const handlerClass)
	{
		return this->bind(inType,MakeEventHandlerFunction(handlerClass, handlerFunction));
	}
	//---------------------------------------------------------
}

#endif //__EventManager_h__26_7_2010__13_42_12__
