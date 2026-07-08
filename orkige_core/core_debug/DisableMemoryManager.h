/********************************************************************
	created:	Thursday 2010/08/12 at 17:45
	filename: 	DisableMemoryManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
	
	purpose:	Disable The Memory Manager
*********************************************************************/
#ifdef ORKIGE_ENABLE_MEMORYMANAGER
#ifdef WIN32
//#ifdef _DEBUG

#ifdef	new
#undef	new
#endif

#ifdef	delete
#undef	delete
#endif

#ifdef	malloc
#undef	malloc
#endif

#ifdef	calloc
#undef	calloc
#endif

#ifdef	realloc
#undef	realloc
#endif

#ifdef	free
#undef	free
#endif

//#endif // _DEBUG
#endif // WIN32
#endif // ORKIGE_ENABLE_MEMORYMANAGER
