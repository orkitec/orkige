/**************************************************************
	created:	2010/07/26 at 15:27
	filename: 	GlobalEventManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __GlobalEventManager_h__26_7_2010__15_27_00__
#define __GlobalEventManager_h__26_7_2010__15_27_00__

#include "core_event/EventManager.h"

namespace Orkige
{
	//! global EventManager Singleton
	class ORKIGE_CORE_DLL GlobalEventManager : public Singleton<GlobalEventManager> , public EventManager
	{
		OOBJECT(GlobalEventManager,EventManager)
		DECL_OSINGLETON(GlobalEventManager)
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		//--- Methods -----------------------------------------
	public:
		//! constructor
		GlobalEventManager();
		//! destructor
		virtual ~GlobalEventManager();
	protected:
	private:
	};
	//---------------------------------------------------------
}

#endif //__GlobalEventManager_h__26_7_2010__15_27_00__
