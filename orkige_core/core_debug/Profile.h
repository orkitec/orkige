/********************************************************************
	created:	Monday 2010/08/16 at 12:29
	filename: 	Profile.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
	note:		Using code from
				Real-Time Hierarchical Profiling for Game Programming Gems 3
				by Greg Hjelstrom & Byon Garrabrant	
*********************************************************************/
#ifndef __Profile_h__16_8_2010__12_29_38__
#define __Profile_h__16_8_2010__12_29_38__

#include "core_debug/ProfileManager.h"

namespace Orkige
{
	/** \addtogroup Debug
	*  @{ */
	//!	@brief this is a profiler based on code you insert where you want to profile.
	//!	
	//!	use the following methods:
	//!	ProfileManager::getSingleton().reset() - reset/start the profiling
	//!	ProfileManager::getSingleton().incrementFrameCounter() - indicate a new frame or other event you want to count
	//!	ProfileManager::getSingleton().debugOutput() - print the profile statistics (uses oDebugMsg("profiler",0,...))
	//!	
	//!	at the beginning of a function or method you write:
	//!	OPROFILE("myLabel");
	class Profile
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
		//! creates a profile with the given label
		inline Profile(const char* name);
		//! creates a profile with the given label
		inline Profile(String const & name);
		//! stops the profile
		inline ~Profile();
	protected:
	private:
	};
	/** @} End of "addtogroup Debug"*/
	//---------------------------------------------------------------
	inline Profile::Profile(const char* name)
	{ 
		ProfileManager::getSingleton().startProfile(name); 
	}
	//---------------------------------------------------------------
	inline Profile::Profile(String const & name)
	{ 
		ProfileManager::getSingleton().startProfile(name); 
	}
	//---------------------------------------------------------------
	inline Profile::~Profile()					
	{ 
		ProfileManager::getSingleton().stopProfile(); 
	}
	//---------------------------------------------------------------
}

#if defined(ORKIGE_DEBUG) || defined(ORKIGE_STATS)
#	define	OPROFILE(name) ::Orkige::Profile ___orkige__profile(name)
#	define	OPROFILEFUNC() OPROFILE(__FUNCTION__)
#else
#	define	OPROFILE(name)
#	define	OPROFILEFUNC()
#endif

#endif //__Profile_h__16_8_2010__12_29_38__
