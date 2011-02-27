/********************************************************************
	created:	Thursday 2010/08/12 at 17:44
	filename: 	EnableMemoryManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
	
	purpose:	Enable the Memory Manager
*********************************************************************/
#ifdef ORKIGE_ENABLE_MEMORYMANAGER
#ifdef WIN32
//#ifdef _DEBUG

#include "DisableMemoryManager.h"

#define	new				(::Orkige::MemoryManager::getSingleton().setOwner  (__FILE__,__LINE__,__FUNCTION__),false) ? NULL : new
#define	delete			(::Orkige::MemoryManager::getSingleton().setOwner  (__FILE__,__LINE__,__FUNCTION__),false) ? ::Orkige::MemoryManager::getSingleton().setOwner("",0,"") : delete
#define	malloc(sz)		::Orkige::MemoryManager::getSingleton().allocator  (__FILE__,__LINE__,__FUNCTION__,::Orkige::MemoryManager::alloc_malloc,sz)
#define	calloc(num,sz)	::Orkige::MemoryManager::getSingleton().allocator  (__FILE__,__LINE__,__FUNCTION__,::Orkige::MemoryManager::alloc_calloc,num*sz)
#define	realloc(ptr,sz)	::Orkige::MemoryManager::getSingleton().reallocator(__FILE__,__LINE__,__FUNCTION__,::Orkige::MemoryManager::alloc_realloc,sz,ptr)
#define	free(ptr)		::Orkige::MemoryManager::getSingleton().deallocator(__FILE__,__LINE__,__FUNCTION__,::Orkige::MemoryManager::alloc_free,ptr)

//#endif // _DEBUG
#endif // WIN32
#endif // ORKIGE_ENABLE_MEMORYMANAGER