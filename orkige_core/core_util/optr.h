/**************************************************************
	created:	2010/09/12 at 0:00
	filename: 	optr.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __optr_h__12_9_2010__0_00_30__
#define __optr_h__12_9_2010__0_00_30__

#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/type_traits.hpp>
#include <boost/utility.hpp>

namespace Orkige
{
	//! helper for not destroying a shared ptr needed when you have to create a shared ptr from an c pointer of a already managed pointer
	//! should be used with care
	struct NullDeleter
	{
		//! operator overload
		void operator()(void const *) const
		{
		}
	};

	//#define	oBadPointer(type,cpointer) boost::shared_ptr<type>(cpointer,NullDeleter())
	template<class Type> 
	static boost::shared_ptr<Type> oBadPointer(Type * t)
	{
		return boost::shared_ptr<Type>(t,NullDeleter());
	}

	//! defines a shared pointer
#define optr boost::shared_ptr
	//! defines a weak shared pointer
#define woptr boost::weak_ptr

	//! alloc optr
	template<class Type> 
	static boost::shared_ptr<Type> onew(Type * t)
	{
		return boost::shared_ptr<Type>(t);
	}

	//! create and empty optr
	template<class Type> 
	static boost::shared_ptr<Type> oNull()
	{
		return boost::shared_ptr<Type>((Type*)NULL);
	}

	//! create and empty optr
#define oNULL(type) oNull<type>()


	//! template check if given type is a optr
	template <typename T>
	struct is_optr 
	{
		const static bool value = false; //!< no optr
	};
	//! template check if given type is a optr
	template <typename T>
	struct is_optr<optr<T> >
	{
		const static bool value = true;	//!< optr
	};
	//! template check if given type is a optr
	template <typename T>
	struct is_optr<optr<const T> >
	{
		const static bool value = true; //!< optr
	};
	//! template check if given type is a optr
	template <typename T>
	struct is_optr<const optr<T> >
	{
		const static bool value = true; //!< optr
	};
	//! template check if given type is a optr
	template <typename T>
	struct is_optr<const optr<const T> >
	{
		const static bool value = true; //!< optr
	};

	//---------------------------------------------------------
}

#endif //__optr_h__12_9_2010__0_00_30__
