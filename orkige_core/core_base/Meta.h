/********************************************************************
	created:	Thursday 2010/09/09 at 18:37
	filename: 	Meta.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/
#ifndef __Meta_h__9_9_2010__18_37_07__
#define __Meta_h__9_9_2010__18_37_07__

#ifdef _GLIBCXX_USE_C99
namespace std
{
	int snprintf(char *str, size_t size, const char *format, va_list args);
}
#endif

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

#include "core_debug/MemoryManager.h"
#include "core_module/OrkigePrerequisites.h"



#include <sstream>

//http://www.boost.org/libs/python/doc/tutorial/doc/html/python/exposing.html#python.constructors
#include "core_base/TypeManager.h"
#include "core_debug/LogManager.h"

#define ORKIGETTI(ClassName,BaseClassName)												\
	private:																			\
		typedef ClassName		OSelf;													\
		typedef BaseClassName	OParent;														

#define ORKIGETTI2(ClassName,BaseClassName,BaseClassName2)								\
	ORKIGETTI(ClassName, BaseClassName)													\
	private:																			\
		typedef BaseClassName2	OParent2;

#define OTYPE_INFO(ClassType)															\
	public:																				\
		/** @brief get the TypeInfo of this class. */									\
		static Orkige::TypeInfo const & getClassTypeInfo();								\
		/** @brief get the TypeInfo of this class instance. */							\
		virtual Orkige::TypeInfo const & getTypeInfo();									\
		/** @brief get the TypeInfo of this class instance. */							\
		virtual Orkige::TypeInfo const & getTypeInfo() const;							\
	private:

#define OTYPE_INFO_IMPL(ClassType,ClassName)											\
	Orkige::TypeInfo const & ClassType::getClassTypeInfo()								\
	{																					\
		static Orkige::TypeInfo typeInfo(#ClassName);									\
		return typeInfo;																\
	}																					\
	Orkige::TypeInfo const & ClassType::getTypeInfo()									\
	{																					\
		return ClassType::getClassTypeInfo();											\
	}																					\
	Orkige::TypeInfo const & ClassType::getTypeInfo() const								\
	{																					\
		return ClassType::getClassTypeInfo();											\
	}

#define OTEMPLATE_TYPE_INFO_IMPL(ClassType,ClassName)									\
	template <> Orkige::TypeInfo const & ClassType::getClassTypeInfo()					\
	{																					\
		static Orkige::TypeInfo typeInfo(#ClassName);									\
		return typeInfo;																\
	}																					\
	template <> Orkige::TypeInfo const & ClassType::getTypeInfo()						\
	{																					\
		return ClassType::getClassTypeInfo();											\
	}																					\
	template <> Orkige::TypeInfo const & ClassType::getTypeInfo() const					\
	{																					\
		return ClassType::getClassTypeInfo();											\
	}

#define OSTRANGEGCC_TEMPLATE_TYPE_INFO_IMPL(ClassType,ClassName)						\
	template <> template <> Orkige::TypeInfo const & ClassType::getClassTypeInfo()		\
	{																					\
		static Orkige::TypeInfo typeInfo(#ClassName);									\
		return typeInfo;																\
	}																					\
	template <> template <> Orkige::TypeInfo const & ClassType::getTypeInfo()			\
	{																					\
		return ClassType::getClassTypeInfo();											\
	}																					\
	template <> template <> Orkige::TypeInfo const & ClassType::getTypeInfo() const		\
	{																					\
		return ClassType::getClassTypeInfo();											\
	}
#ifdef ORKIGE_NOSCRIPT
#include "core_base/Meta_None.h"
#elif ORKIGE_LUA
#include "core_base/Meta_Lua.h"
#else
#include "core_base/Meta_Python.h"
#endif


#endif //__Meta_h__9_9_2010__18_37_07__
