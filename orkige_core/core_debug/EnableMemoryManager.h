/********************************************************************
	created:	Thursday 2010/08/12 at 17:44
	filename: 	EnableMemoryManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
	
	purpose:	Enable the Memory Manager
*********************************************************************/
#ifdef ORKIGE_ENABLE_MEMORYMANAGER
#ifdef WIN32
#ifdef _DEBUG

#include "DisableMemoryManager.h"

#define	new		(::Orkige::MemoryManager::getSingleton().m_setOwner  (__FILE__,__LINE__,__FUNCTION__),false) ? NULL : new
#define	delete		(::Orkige::MemoryManager::getSingleton().m_setOwner  (__FILE__,__LINE__,__FUNCTION__),false) ? ::Orkige::MemoryManager::getSingleton().m_setOwner("",0,"") : delete
#define	malloc(sz)	::Orkige::MemoryManager::getSingleton().m_allocator  (__FILE__,__LINE__,__FUNCTION__,::Orkige::MemoryManager::m_alloc_malloc,sz)
#define	calloc(num,sz)	::Orkige::MemoryManager::getSingleton().m_allocator  (__FILE__,__LINE__,__FUNCTION__,::Orkige::MemoryManager::m_alloc_calloc,num*sz)
#define	realloc(ptr,sz)	::Orkige::MemoryManager::getSingleton().m_reallocator(__FILE__,__LINE__,__FUNCTION__,::Orkige::MemoryManager::m_alloc_realloc,sz,ptr)
#define	free(ptr)	::Orkige::MemoryManager::getSingleton().m_deallocator(__FILE__,__LINE__,__FUNCTION__,::Orkige::MemoryManager::m_alloc_free,ptr)

#endif // _DEBUG
#endif // WIN32
#endif // ORKIGE_ENABLE_MEMORYMANAGER