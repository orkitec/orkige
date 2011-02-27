/********************************************************************
	created:	Monday 2010/08/30 at 14:01
	filename: 	AccelerationEventData.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __AccelerationEventData_h__30_8_2010__14_01_58__
#define __AccelerationEventData_h__30_8_2010__14_01_58__

#include <core_event/Event.h>

namespace Orkige
{
	/** \addtogroup EngineEvents
	*  @{ */
	//! Data that gets sent on InputManager::AcceleartionEvent
	class ORKIGE_DLL AccelerationEventData : public Object
	{
		OOBJECT(AccelerationEventData,Object)
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
		//! relative x axis position
		float relX;
		//! relative y axis position
		float relY;
		//! relative z axis position
		float relZ;
		//! absolute x axis position
		float absX;
		//! absolute y axis position
		float absY;
		//! absolute z axis position
		float absZ;
	protected:
	private:
		//--- Methods -----------------------------------------------
	public:
		inline AccelerationEventData() : relX(0.f), relY(0.f), relZ(0.f), absX(0.f), absY(0.f), absZ(0.f) {}
	protected:
	private:
	};
	/** @} End of "addtogroup EngineEvents"*/
	//---------------------------------------------------------------
}

#endif //__AccelerationEventData_h__30_8_2010__14_01_58__
