/********************************************************************
	created:	Monday 2010/08/30 at 14:01
	filename: 	GestureEventData.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/
#ifndef __GestureEventData_h__30_8_2010__14_01_58__
#define __GestureEventData_h__30_8_2010__14_01_58__

#include <core_event/Event.h>

namespace Orkige
{
	/** \addtogroup EngineEvents
	*  @{ */
	//! Data that gets sent on InputManager::GestureBeganEvent, InputManager::GestureEndedEvent and InputManager::GestureCancelledEvent
	class ORKIGE_DLL GestureEventData : public Object
	{
		OOBJECT(GestureEventData,Object)
		//--- Types -------------------------------------------------
	public:
		enum GestureType
		{
			GT_Shake = 0,
			GT_None
		};
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
		GestureType type;
	protected:
	private:
		//--- Methods -----------------------------------------------
	public:
		inline GestureEventData() : type(GT_None) {}
	protected:
	private:
	};
	/** @} End of "addtogroup EngineEvents"*/
	//---------------------------------------------------------------
}

#endif //__GestureEventData_h__30_8_2010__14_01_58__
