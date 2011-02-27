/********************************************************************
	created:	Monday 2010/08/30 at 14:01
	filename: 	MouseEventData.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __MouseEventData_h__30_8_2010__14_01_58__
#define __MouseEventData_h__30_8_2010__14_01_58__

#include <core_event/Event.h>

namespace Orkige
{
	/** \addtogroup EngineEvents
	*  @{ */
	//! Data that gets sent on InputManager::MousePressedEvent, InputManager::MouseReleasedEvent and InputManager::MouseMovedEvent
	class ORKIGE_DLL MouseEventData : public Object
	{
		OOBJECT(MouseEventData,Object)
		//--- Types -------------------------------------------------
	public:
		//! Button ID for mouse devices
		enum MouseButtonID
		{
			MB_Left = 0, MB_Right, MB_Middle,
			MB_Button3, MB_Button4,	MB_Button5, MB_Button6,	MB_Button7
		};
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
		//! current pressed or released button id (invalid to request on mouse moved events)
		MouseButtonID button;
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
		//! represents all buttons - bit position indicates button down
		int buttons;
	protected:
	private:
		//--- Methods -----------------------------------------------
	public:
		inline MouseEventData() : relX(0), relY(0), relZ(0), absX(0), absY(0), absZ(0), buttons(0) {}
		//! Button down test
		inline bool buttonDown( MouseButtonID button ) const
		{
			return ((this->buttons & ( 1L << this->button )) == 0) ? false : true;
		}
	protected:
	private:
	};
	/** @} End of "addtogroup EngineEvents"*/
	//---------------------------------------------------------------
}

#endif //__MouseEventData_h__30_8_2010__14_01_58__
