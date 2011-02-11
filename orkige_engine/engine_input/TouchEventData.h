/********************************************************************
	created:	Monday 2010/08/30 at 14:01
	filename: 	TouchEventData.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/
#ifndef __TouchEventData_h__30_8_2010__14_01_58__
#define __TouchEventData_h__30_8_2010__14_01_58__

#include <core_event/Event.h>

namespace Orkige
{
	/** \addtogroup EngineEvents
	*  @{ */
	//! Data that gets sent on InputManager::TouchPressedEvent, InputManager::TouchReleasedEvent, InputManager::TouchMovedEvent and InputManager::TouchCancelledEvent
	class ORKIGE_DLL TouchEventData : public Object
	{
		OOBJECT(TouchEventData,Object)
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
		//! relative x axis position
		int relX;
		//! relative y axis position
		int relY;
		//! relative z axis position
		int relZ;
		//! absolute x axis position
		int absX;
		//! absolute y axis position
		int absY;
		//! absolute z axis position
		int absZ;
		//! ID which touch event sequence
		int sequenceId;
	protected:
	private:
		//--- Methods -----------------------------------------------
	public:
		inline TouchEventData() : relX(0), relY(0), relZ(0), absX(0), absY(0), absZ(0), sequenceId (0) {}
	protected:
	private:
	};
	/** @} End of "addtogroup EngineEvents"*/
	//---------------------------------------------------------------
}

#endif //__TouchEventData_h__30_8_2010__14_01_58__
