/********************************************************************
	created:	Tuesday 2010/10/26 at 18:24
	filename: 	FastGuiManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/
#ifndef __FastGuiManager_h__26_10_2010__18_24_45__
#define __FastGuiManager_h__26_10_2010__18_24_45__

#include "engine_fastgui/Gorilla.h"
#include <core_util/Singleton.h>
#include <core_base/Interface.h>

namespace Orkige
{
	class FastGuiManager : public Singleton<FastGuiManager>, public Interface
	{
		OOBJECT(FastGuiManager, Interface);
		DECL_OSINGLETON(FastGuiManager);
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		optr<Gorilla::Silverback> silverback;
		Gorilla::Screen* defaultScreen;
		
	private:
		//--- Methods -----------------------------------------------
	public:
		FastGuiManager();
		virtual ~FastGuiManager();
	protected:
	private:
	};
	//---------------------------------------------------------------
}

#endif //__FastGuiManager_h__26_10_2010__18_24_45__