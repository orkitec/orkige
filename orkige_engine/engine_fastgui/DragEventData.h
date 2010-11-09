/********************************************************************
	created:	2010/11/02
	filename: 	DragEventData.h
	author:		philipp.engelhard
	purpose:	
	copyright:	(c) 2010 kunst-stoff
***************************************************************/
#ifndef __ORKIGE__DragEventData_h__
#define __ORKIGE__DragEventData_h__

#include <core_event/Event.h>
#include "engine_module/EnginePrerequisites.h"

namespace Orkige
{
	// forward declaration
	class FastGuiDragDropButton;
	/** \addtogroup cc_gui
	*  @{ */
	//! data that gets triggered on drag
	class DragEventData : public Object
	{

		OOBJECT(DragEventData, Object);

		//--- Types -------------------------------------------------
	public:
		enum DragState
		{
			DS_DRAG_START,
			DS_DRAGGING,
			DS_DRAG_END,
			DS_DRAG_ABORT,
		};
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
		//! Button that is dragged
		optr< FastGuiDragDropButton >	button;
		//! State in which we're in
		DragState						state;
		//! Where are we dragging
		Ogre::Vector2					position;
	protected:
	private:
		//--- Methods -----------------------------------------------
	public:
	protected:
	private:
	};
	/** @} End of "addtogroup cc_gui"*/
	//---------------------------------------------------------------
}

#endif //__ORKIGE__DragEventData_h__
