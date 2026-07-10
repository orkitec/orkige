/********************************************************************
created:	Thursday 2010/08/12 at 15:55
filename: 	MemoryManager.h
author:		steffen.roemer
notice:		This source file is part of orkige (orkitec Game engine)
			For the latest info, see http://www.orkitec.com/
copyright:	(c) 2009-2011 orkitec

---------------------------------------------------------------------
Restrictions & freedoms pertaining to usage and redistribution of this software:

- This software is 100% free
- If you use this software (in part or in whole) you must credit the author.
- This software may not be re-distributed (in part or in whole) in a modified
  form without clear documentation on how to obtain a copy of the original work.
- You may not use this software to directly or indirectly cause harm to others.
- This software is provided as-is and without warrantee. Use at your own risk.

For more information, visit HTTP://www.FluidStudios.com

---------------------------------------------------------------------
Originally created on 12/22/2000 by Paul Nettle
Copyright 2000, Fluid Studios, Inc., all rights reserved.
---------------------------------------------------------------------

purpose:	Memory manager & tracking software
*********************************************************************/
#ifndef __MemoryManager_h__12_8_2010__15_55_37__
#define __MemoryManager_h__12_8_2010__15_55_37__

#include "core_module/OrkigePrerequisites.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <cstdarg>
#include <cmath>

// STL containers
#include <vector>
#include <map>
#include <string>
#include <set>
#include <list>
#include <deque>
#include <queue>
#include <bitset>


// STL algorithms & functions
#include <algorithm>
#include <functional>
#include <limits>

// C++ Stream stuff
#include <streambuf>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <mutex>

extern "C" {

#   include <sys/types.h>
#   include <sys/stat.h>

}

#include "core_util/Singleton.h"

//! For systems that don't have the __FUNCTION__ variable, we can just define it here
#ifndef __FUNCTION__
#define __FUNCTION__ "???"
#endif

#ifdef ORKIGE_ENABLE_MEMORYMANAGER
//#ifdef _DEBUG
#ifdef WIN32

//! Variations of global operators new & delete
void	*operator new(size_t reportedSize);
void	*operator new[](size_t reportedSize);
void	*operator new(size_t reportedSize, const char *sourceFile, int sourceLine);
void	*operator new[](size_t reportedSize, const char *sourceFile, int sourceLine);
void	operator delete(void *reportedAddress);
void	operator delete[](void *reportedAddress);

namespace Orkige
{
	/** \addtogroup Debug
	*  @{ */
	//! Memory manager & tracking software
	class MemoryManager : public Singleton<MemoryManager>
	{
		friend void * ::operator new(size_t);
		friend void * ::operator new[](size_t);
		friend void	* ::operator new(size_t reportedSize, const char *sourceFile, int sourceLine);
		friend void	* ::operator new[](size_t reportedSize, const char *sourceFile, int sourceLine);
		friend void ::operator delete(void*);
		friend void ::operator delete[](void*);
		DECL_OSINGLETON_ORKIGE_CORE_DLL(MemoryManager);
		//--- Types -------------------------------------------------
	public:
		//! Allocation Unit Info
		typedef	struct tag_au
		{
			size_t			actualSize;			//!< actual alloc size
			size_t			reportedSize;		//!< reported alloc size
			void			*actualAddress;		//!< actual allc adress
			void			*reportedAddress;	//!< reported alloc adress
			char			sourceFile[255];	//!< alloc sourcefile
			char			sourceFunc[64];		//!< alloc function
			unsigned int	sourceLine;			//!< alloc line
			unsigned int	allocationType;		//!< type of allocation
			bool			breakOnDealloc;		//!< should MeoryManager break on this allocation?
			bool			breakOnRealloc;		//!< should MeoryManager break on realloc this allocation?
			unsigned int	allocationNumber;	//!< number of allocation
			struct tag_au	*next;				//!< next AllocUnit
			struct tag_au	*prev;				//!< previous AllocUnit
		} AllocUnit;

		//! memory stats
		typedef	struct
		{
			unsigned int	totalReportedMemory;		//!< reported total memory size used
			unsigned int	totalActualMemory;			//!< actual memory size used
			unsigned int	peakReportedMemory;			//!< peak of reported memory
			unsigned int	peakActualMemory;			//!< peak of actual memory 
			unsigned int	accumulatedReportedMemory;	//!< accumulated reported memory
			unsigned int	accumulatedActualMemory;	//!< accumulated actual memory
			unsigned int	accumulatedAllocUnitCount;	//!< accumulated AllocUnit Count
			unsigned int	totalAllocUnitCount;		//!< total AllocUnit Count
			unsigned int	peakAllocUnitCount;			//!< peak AllocUnit Count
		} MemStats;

		//! Defaults for the constants & statics in the MemoryManager class
		enum
		{
			alloc_unknown        = 0,
			alloc_new            = 1,
			alloc_new_array      = 2,
			alloc_malloc         = 3,
			alloc_calloc         = 4,
			alloc_realloc        = 5,
			alloc_delete         = 6,
			alloc_delete_array   = 7,
			alloc_free           = 8
		};
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		std::mutex memoryMutex;
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		MemoryManager();
		//! destructor
		virtual ~MemoryManager();

		//! These are the standard new/new[] operators. They are merely interface functions that operate like normal new/new[], 
		//!  but use our memory tracking routines.
		ORKIGE_CORE_DLL void *operator_new_p(size_t reportedSize);
		//! @see MemoryManager::operator_new_p
		ORKIGE_CORE_DLL void *operator_new_a(size_t reportedSize);
		//! These are the standard new/new[] operators as used by Microsoft's memory tracker. We don't want them interfering with our memory
		//!  tracking efforts. Like the previous versions, these are merely interface functions that operate like normal new/new[], but use
		//!  our memory tracking routines.
		ORKIGE_CORE_DLL void *operator_new_p(size_t reportedSize, const char *sourceFile, int sourceLine);
		//! @see MemoryManager::operator_new_p
		ORKIGE_CORE_DLL void *operator_new_a(size_t reportedSize, const char *sourceFile, int sourceLine);
		//! These are the standard delete/delete[] operators. They are merely interface functions that operate like normal delete/delete[],
		//! but use our memory tracking routines.
		ORKIGE_CORE_DLL void operator_delete_p(void *reportedAddress);
		//! @see MemoryManager::operator_delete_p
		ORKIGE_CORE_DLL void operator_delete_a(void *reportedAddress);

		//! Used by the macros
		ORKIGE_CORE_DLL void setOwner(const char *file, const unsigned int line, const char *func);

		// Allocation breakpoints
		
		//! Simply call this routine with the address of an allocated block of RAM, to cause it to force a breakpoint when it is reallocated.
		ORKIGE_CORE_DLL bool &breakOnRealloc(void *reportedAddress);
		//! Simply call this routine with the address of an allocated block of RAM, to cause it to force a breakpoint when it is deallocated.
		ORKIGE_CORE_DLL bool &breakOnDealloc(void *reportedAddress);

		// The meat of the memory tracking software
		
		//! Allocate memory and track it
		ORKIGE_CORE_DLL void *allocator(const char *sourceFile, const unsigned int sourceLine, const char *sourceFunc, const unsigned int allocationType, const size_t reportedSize);
		//! Reallocate memory and track it
		ORKIGE_CORE_DLL void *reallocator(const char *sourceFile, const unsigned int sourceLine, const char *sourceFunc, const unsigned int reallocationType, const size_t reportedSize, void *reportedAddress);
		//! Deallocate memory and track it
		ORKIGE_CORE_DLL void deallocator(const char *sourceFile, const unsigned int sourceLine, const char *sourceFunc, const unsigned int deallocationType, const void *reportedAddress);

		// The following utilitarian allow you to become proactive in tracking your own memory, or help you narrow in on those tough bugs.
		//! validate given memory adress
		ORKIGE_CORE_DLL bool validateAddress(const void *reportedAddress);
		//! validate given AllocUnit
		ORKIGE_CORE_DLL bool validateAllocUnit(const AllocUnit *allocUnit);
		//! validate all AlllocUnit's
		ORKIGE_CORE_DLL bool validateAllAllocUnits();

		//! Unused RAM calculation routines. Use these to determine how much of your RAM is unused (in bytes)
		ORKIGE_CORE_DLL unsigned int calcUnused(const AllocUnit *allocUnit);
		//! @see MemoryManager::calcUnused
		ORKIGE_CORE_DLL unsigned int calcAllUnused();

		// The following functions are for logging and statistics reporting.
		//! dump AllocUnit to log
		ORKIGE_CORE_DLL void dumpAllocUnit(const AllocUnit *allocUnit, const char *prefix = "");
		//! write memory report
		ORKIGE_CORE_DLL void dumpMemoryReport(const char *filename = "memreport.log", const bool overwrite = true);
		//! get the memory statistics
		ORKIGE_CORE_DLL MemStats getMemoryStatistics();
		//! Force a validation of all allocation units each time we enter this software
		ORKIGE_CORE_DLL bool	&setGetAlwaysValidateAll();
		//! Force a log of every allocation & deallocation into memory.log
		ORKIGE_CORE_DLL bool	&setGetAlwaysLogAll();
		//! Force this software to always wipe memory with a pattern when it is being allocated/dallocated
		ORKIGE_CORE_DLL bool	&setGetAlwaysWipeAll();
		//! Force this software to use a random pattern when wiping memory -- good for stress testing
		ORKIGE_CORE_DLL bool	&setGetRandomeWipe();
		//! When tracking down a difficult bug, use this routine to force a breakpoint on a specific allocation count
		ORKIGE_CORE_DLL void	setBreakOnAllocation(unsigned int count);
	protected:
	private:
		void dumpAllocations(FILE *fp);
		void dumpLeakReport();
		void log(const char *format, ...);
		void doCleanupLogOnFirstRun();
		const char	*sourceFileStripper(const char *sourceFile);
		const char	*ownerString(const char *sourceFile, const unsigned int sourceLine, const char *sourceFunc, bool stripSourceFile = true);
		const char	*insertCommas(unsigned int value);
		const char	*memorySizeString(unsigned long size);
		AllocUnit	*findAllocUnit(const void *reportedAddress);
		size_t	calculateActualSize(const size_t reportedSize);
		size_t	calculateReportedSize(const size_t actualSize);
		void	*calculateReportedAddress(const void *actualAddress);
		void	wipeWithPattern(AllocUnit *allocUnit, unsigned long pattern, const unsigned int originalReportedSize = 0);


		void	resetGlobals();
	};
	/** @} End of "addtogroup Debug"*/
	//---------------------------------------------------------------
	ORKIGE_CORE_DLL int createOrkigeMemoryManager();
	ORKIGE_CORE_DLL void operator_delete_p_unmanaged(void *reportedAddress);
	ORKIGE_CORE_DLL void operator_delete_a_unmanaged(void *reportedAddress);
}

#if defined(__cplusplus_cli)
#pragma managed(push, off)
#endif
//-------------------------------------------------------------------
//! Overridden global new([])/delete([]) functions
inline void *operator new(size_t reportedSize)
{
	if(!Orkige::MemoryManager::getSingletonPtr())
		Orkige::createOrkigeMemoryManager();

	return Orkige::MemoryManager::getSingleton().operator_new_p( reportedSize);
}
//-------------------------------------------------------------------
inline void *operator new[](size_t reportedSize)
{
	if(!Orkige::MemoryManager::getSingletonPtr())
		Orkige::createOrkigeMemoryManager();

	return Orkige::MemoryManager::getSingleton().operator_new_a( reportedSize);
}
//-------------------------------------------------------------------
inline void	* operator new(size_t reportedSize, const char *sourceFile, int sourceLine)
{
	if(!Orkige::MemoryManager::getSingletonPtr())
		Orkige::createOrkigeMemoryManager();

	return Orkige::MemoryManager::getSingleton().operator_new_p( reportedSize, sourceFile, sourceLine);

}
//-------------------------------------------------------------------
inline void	* operator new[](size_t reportedSize, const char *sourceFile, int sourceLine)
{
	if(!Orkige::MemoryManager::getSingletonPtr())
		Orkige::createOrkigeMemoryManager();

	return Orkige::MemoryManager::getSingleton().operator_new_a( reportedSize, sourceFile, sourceLine);
}
//-------------------------------------------------------------------
inline void operator delete(void *reportedAddress)
{
	if(Orkige::MemoryManager::getSingletonPtr())
		Orkige::MemoryManager::getSingleton().operator_delete_p( reportedAddress);
	else
		Orkige::operator_delete_p_unmanaged( reportedAddress);
}
//-------------------------------------------------------------------
inline void operator delete[](void *reportedAddress)
{
	if(Orkige::MemoryManager::getSingletonPtr())
		Orkige::MemoryManager::getSingleton().operator_delete_a( reportedAddress);
	else
		Orkige::operator_delete_a_unmanaged( reportedAddress);
}
//-------------------------------------------------------------------
#if defined(__cplusplus_cli)
#pragma managed(pop)
#endif
#ifndef CREATE_ORKIGE_MEMORY_MANAGER
#	define CREATE_ORKIGE_MEMORY_MANAGER ::Orkige::createOrkigeMemoryManager();
#endif
#include "core_debug/EnableMemoryManager.h"

#endif // WIN32
//#endif // _DEBUG
#endif // ORKIGE_ENABLE_MEMORYMANAGER
#endif //__MemoryManager_h__12_8_2010__15_55_37__
#ifndef CREATE_ORKIGE_MEMORY_MANAGER
#	define CREATE_ORKIGE_MEMORY_MANAGER 
#endif
