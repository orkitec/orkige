/**************************************************************
	created:	2010/08/20 at 0:21
	filename: 	Singleton.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
/* Original version Copyright (C) Scott Bilas, 2000.
* All rights reserved worldwide.
*
* This software is provided "as is" without express or implied
* warranties. You may freely copy and compile this source into
* applications you distribute provided that the copyright text
* below is included in the resulting source code, for example:
* "Portions Copyright (C) Scott Bilas, 2000"
*/
#ifndef __Singleton_h__20_8_2010__0_21_47__
#define __Singleton_h__20_8_2010__0_21_47__

#include "core_debug/DebugMacros.h"

namespace Orkige
{
	//! generic singleton base class
	template <typename T> class Singleton
	{
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		static T* singleton;	//!< the singleton instance
	private:
		//--- Methods -----------------------------------------
	public:
		//! constructor
		Singleton( void )
		{
			oAssert( !singleton );
#if defined( _MSC_VER ) && _MSC_VER < 1200	 
			int offset = (int)(T*)1 - (int)(Singleton <T>*)(T*)1;
			singleton = (T*)((int)this + offset);
#else
			singleton = static_cast< T* >( this );
#endif
		}
		//! destructor
		~Singleton( void )
		{  
			oAssert( singleton );
			singleton = 0;  
		}
	protected:
	private:
	};
	//---------------------------------------------------------
	template <typename T> T* Singleton <T>::singleton = 0;
	//---------------------------------------------------------
#define DECL_OSINGLETON(ClassName)												\
public:																			\
	/** get singleton instance of ClassName */									\
	static ClassName& getSingleton( void );										\
	/** get singleton ptr of ClassName or NULL if singleton wasn't created.*/	\
	static ClassName* getSingletonPtr( void );									\
private:

	//! default singleton implementation
#define IMPL_OSINGLETON(ClassName)												\
	ClassName& ClassName::getSingleton( void )									\
	{																			\
		oAssert( singleton );													\
		return ( *singleton );													\
	}																			\
	ClassName* ClassName::getSingletonPtr( void )								\
	{																			\
		return singleton;														\
	}

	//! singleton implementation that gets created on first getSingleton call and gets destroyed when application closes
	//! (only works for classes with public default constructor)
#define IMPL_OSINGLETON_GETCREATE(ClassName)									\
	ClassName* ClassName::getSingletonPtr( void )								\
	{																			\
		static ClassName _singleton;											\
		return singleton;														\
	}																			\
	ClassName& ClassName::getSingleton( void )									\
	{																			\
		static ClassName* _singleton = ClassName::getSingletonPtr();			\
		oAssert( singleton );													\
		return ( *singleton );													\
	}
}

#endif //__Singleton_h__20_8_2010__0_21_47__
