/********************************************************************
	created:	Monday 2010/08/09 at 18:38
	filename: 	Interface.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __Interface_h__9_8_2010__18_38_30__
#define __Interface_h__9_8_2010__18_38_30__

#include "core_base/Meta.h"

//! orkige main namespace
namespace Orkige
{
	//! java like Interface base class for all Objects
	class ORKIGE_CORE_DLL Interface
	{
		OINTERFACE(Interface);
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		//--- Methods -----------------------------------------------
	public:
		//! destructor
		virtual ~Interface();
	protected:
		//! protected destructor object can only be derived
		inline explicit Interface();
	private:
	};
	//---------------------------------------------------------------
	inline Interface::Interface()
	{

	}
	//---------------------------------------------------------------
}

#endif //__Interface_h__9_8_2010__18_38_30__
