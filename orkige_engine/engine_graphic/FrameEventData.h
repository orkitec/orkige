/********************************************************************
	created:	Tuesday 2010/09/07 at 12:14
	filename: 	FrameEventData.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __FrameEventData_h__7_9_2010__12_14_28__
#define __FrameEventData_h__7_9_2010__12_14_28__

#include <core_event/Event.h>
#include "engine_module/EnginePrerequisites.h"

namespace Orkige
{
	/** \addtogroup EngineEvents
	*  @{ */
	//! data that gets triggered on GraphicManager::Frame*Event's
	class FrameEventData : public Object
	{
		OOBJECT(FrameEventData,Object)
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
		//! Elapsed time in seconds since the last event.
		//! This gives you time between frame start & frame end,
		//! and between frame end and next frame start.
		//! @remarks
		//! This may not be the elapsed time but the average
		//! elapsed time between recently fired events.
		Ogre::Real timeSinceLastEvent;
		//! Elapsed time in seconds since the last event of the same type,
		//! i.e. time for a complete frame.
		//! @remarks
		//! This may not be the elapsed time but the average
		//! elapsed time between recently fired events of the same type.
		Ogre::Real timeSinceLastFrame;
	protected:
	private:
		//--- Methods -----------------------------------------------
	public:
	protected:
	private:
	};
	/** @} End of "addtogroup EngineEvents"*/
	//---------------------------------------------------------------
}

#endif //__FrameEventData_h__7_9_2010__12_14_28__
