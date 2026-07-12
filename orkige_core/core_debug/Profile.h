/**************************************************************
	created:	2026/07/12 at 10:00
	filename: 	Profile.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __Profile_h__12_7_2026__10_00_00__
#define __Profile_h__12_7_2026__10_00_00__

#include "core_debug/ProfileManager.h"

namespace Orkige
{
	/** \addtogroup Debug
	*  @{ */
	//! @brief the RAII scope behind OPROFILE/OPROFILEFUNC: opens a named
	//! profiler scope for the enclosing block. The name must be a STATIC
	//! string (a literal) - the profiler stores the pointer, never a copy.
	//! Always compiled; when the profiler is disabled (Release default) the
	//! constructor is one relaxed atomic load and a branch.
	class Profile
	{
		//--- Variables ---------------------------------------------
	private:
		bool mStarted;	//!< only close a scope this object actually opened
		//--- Methods -----------------------------------------------
	public:
		explicit Profile(const char * name)
			: mStarted(ProfileManager::beginScope(name))
		{
		}
		~Profile()
		{
			if (mStarted)
			{
				ProfileManager::endScope();
			}
		}
		Profile(Profile const &) = delete;
		Profile & operator=(Profile const &) = delete;
	};
	/** @} End of "addtogroup Debug"*/
}

//! time the enclosing block as a named profiler scope (static string only)
#define OPROFILE(name) ::Orkige::Profile ___orkigeProfileScope(name)
//! time the enclosing function as a profiler scope
#define OPROFILEFUNC() OPROFILE(__FUNCTION__)

#endif //__Profile_h__12_7_2026__10_00_00__
