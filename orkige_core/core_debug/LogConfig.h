/********************************************************************
	created:	Tuesday 2010/08/10 at 16:26
	filename: 	LogConfig.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
#ifndef __LogConfig_h__10_8_2010__16_26_26__
#define __LogConfig_h__10_8_2010__16_26_26__

#include "core_base/Meta.h"

namespace Orkige
{
	/** \addtogroup Debug
	*  @{ */
	//! LogManager Config loader
	class ORKIGE_CORE_DLL LogConfig
	{
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
		//! load given config and setup LogManager
		bool readConfig(const char* configFileName);
	protected:
	private:
	};
	/** @} End of "addtogroup Debug"*/
	//---------------------------------------------------------------
}

#endif //__LogConfig_h__10_8_2010__16_26_26__
