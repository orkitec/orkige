/**************************************************************
	created:	2010/09/12 at 0:13
	filename: 	PythonFunction.hpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#ifndef __PythonFunction_hpp__12_9_2010__0_13_01__
#define __PythonFunction_hpp__12_9_2010__0_13_01__

#ifndef _boost_function_py_h_
#define _boost_function_py_h_
#ifndef ORKIGE_NOSCRIPT
//http://mail.python.org/pipermail/c++-sig/2006-May/010670.html
#include "core_base/Meta.h"
#include <boost/function.hpp>
#include <iostream>


namespace Orkige
{
	

	namespace function {
	#ifndef ORKIGE_LUA
		using namespace ::boost::python ;
		// We need a wrapper functor for Python objects to store in a boost::functionN
		// object, mostly because we need to extract the return type.

#define FUNCTION_OBJECT_FUNCTOR_DEBUG( n ) \
	//oDebugMsg("python",0,String("object_functor") + String(n +String(": "))

		template < typename Function ,
			unsigned int Arity = Function::arity >
		struct object_functor ;

#define FUNCTION_OBJECT_FUNCTOR_DECL \
	handle<> obj ; \
	typedef typename Function::result_type result_type ; \
	object_functor( PyObject* obj ) : obj( borrowed( obj ) ) {}

		//TODO(morrk): test if this is always ok
		//got crashs on exit application without it
		//when callback is a python class method
#define FUNCTION_OBJECT_FUNCTOR_DESTR \
	~object_functor(){obj.release();}

		template < typename Function >
		struct object_functor< Function , 0 >
		{
			FUNCTION_OBJECT_FUNCTOR_DECL

				result_type operator () () const
			{
				FUNCTION_OBJECT_FUNCTOR_DEBUG( 0 ) ;
				return call< result_type >( obj.get() ) ;
			}

			FUNCTION_OBJECT_FUNCTOR_DESTR

		} ;

		template < typename Function >
		struct object_functor< Function , 1 >
		{
			FUNCTION_OBJECT_FUNCTOR_DECL

				result_type operator () ( typename Function::arg1_type a1 ) const
			{
				FUNCTION_OBJECT_FUNCTOR_DEBUG( 1 ) ;
				return call< result_type >( obj.get() , a1 ) ;
			}

			FUNCTION_OBJECT_FUNCTOR_DESTR

		} ;

		template < typename Function >
		struct object_functor< Function , 2 >
		{
			FUNCTION_OBJECT_FUNCTOR_DECL

				result_type operator () ( typename Function::arg1_type a1 ,
				typename Function::arg1_type a2 ) const
			{
				FUNCTION_OBJECT_FUNCTOR_DEBUG( 2 ) ;
				return call< result_type >( obj.get() , a1 , a2 ) ;
			}

			FUNCTION_OBJECT_FUNCTOR_DESTR

		} ;

		// Etc. up to the minimum between the max arity of boost::function and
		// boost::python::call<>
		// It could be helped with BOOST_PP.


		// Solution v2:
		//
		// Export boost::function with class_, that is, make it an real class type to
		// Python, but keep the pyobject_to_function as before, striped out of the
		// implicit stuff, and then just push_back this converter for the
		// boost::function. If we do this push_back at the end, we can also profit from
		// implicit convertions.
		//
		// However, maybe we could still scan the chain of converters to avoid the
		// push_back order problem.
		//
		// With this solution, the code gets a lot simpler, we can always get a callable
		// object when a function returns a boost::function, whatever is
		// inside. However, if, in Python, we want to extract the content of a
		// boost::function, we will probably have to do something as in the first
		// version, but called manually instead of implicitly.

		template < typename Function >
		struct pyobject_to_function
		{
			static void* convertible( PyObject* obj )
			{
				// Is there anyway to test for the callable object's arity? Otherwise, I
				// don't see what else we can check here.
				return PyCallable_Check( obj ) ? obj : 0 ;
			}

			static void construct( PyObject* obj ,
				converter::rvalue_from_python_stage1_data* data )
			{
				using namespace boost::python::converter ;
				void* storage
					= ((rvalue_from_python_storage< Function >*)data)->storage.bytes ;

				new (storage) Function( object_functor< Function >( obj ) ) ;

				// record successful construction
				data->convertible = storage ;
			}
		} ;

		template < typename Function >
		void export_function( const char* name )
		{
			class_< Function >( name )
				.def( "__call__" , & Function::operator() )
				;
		}

		template < typename Function >
		void register_pyobject_to_function()
		{
			boost::python::converter::registry::push_back(
				& pyobject_to_function< Function >::convertible ,
				& pyobject_to_function< Function >::construct ,
				type_id< Function >()
				) ;
		}

		template < typename Functor ,
			typename Function >
			void export_functor( const char* name )
		{
			class_< Functor >( name )
				.def( "__call__" , & Functor::operator() )
				;
			implicitly_convertible< Functor , Function >() ;
		}
		#else
		template < typename TDummy >
			void export_function( const char* )
		{
		}
		
		template < typename TDummy >
			void register_pyobject_to_function()
		{
		}
		#endif
	} // end namespace function
}
#endif //ORKIGE_NOSCRIPT

#endif //__PythonFunction_hpp__12_9_2010__0_13_01__
