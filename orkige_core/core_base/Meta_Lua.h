/********************************************************************
	created:	Thursday 2010/09/09 at 18:37
	filename: 	Meta_Lua.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
#ifndef __Meta_Lua_h__9_9_2010__18_37_39__
#define __Meta_Lua_h__9_9_2010__18_37_39__

#ifndef ORKIGE_NOSCRIPT
#include <boost/shared_ptr.hpp>
extern "C"
{
    #include "lua.h"
}
namespace luabind {
    template<class T> T* get_pointer(boost::shared_ptr<T>& p) 
	{  
		return p.get();   
	}
    template<class A> boost::shared_ptr<const A>* get_const_holder(boost::shared_ptr<A>*)  
	{ 
		return 0;  
	} 
}
#include <luabind/luabind.hpp>

namespace bp = luabind;

#define OMETACLASS(ClassName)															\		
	public:																				\
		static bp::scope OrkigeMetaExport(const char * currentOrkigeModuleName);			\
	private:	

#define OINTERFACE(ClassName)															\
	OTYPE_INFO(ClassName)																\
	OMETACLASS(ClassName)

//used for all objects and derived objects
#define OOBJECT(ClassName, BaseClassName)																	\
	friend class Orkige::TypeManager;																		\
	friend class ObjectFactory<Interface * (), String>;																\
	friend struct bp::class_< ClassName, bp::bases<BaseClassName>,boost::shared_ptr<BaseClassName> >;		\
	ORKIGETTI(ClassName,BaseClassName)																		\
	OINTERFACE(ClassName)

//object with default constructor
#define OOBJECT_WD(ClassName,BaseClassName)																	\
	protected:																								\
	ClassName(){}																							\
	private:																								\
	OOBJECT(ClassName,BaseClassName)

#define OOBJECT2(ClassName,BaseClassName,BaseClassName2)													\
	friend class Orkige::TypeManager;																				\
	friend class ObjectFactory<Interface * (), String>;																		\
	friend struct bp::class_< ClassName, bp::bases<BaseClassName,BaseClassName2>,boost::shared_ptr<BaseClassName> >;\
	ORKIGETTI2(ClassName,BaseClassName,BaseClassName2)														\
	OINTERFACE(ClassName)

#define OOBJECT_TEMPLATE(ClassName,TemplateArgument,BaseClassName)											\
	friend class Orkige::TypeManager;																				\
	friend class ObjectFactory<Interface * (), String>;																		\
	friend struct bp::class_< ClassName, bp::bases<BaseClassName>,boost::shared_ptr<ClassName> >;					\
	ORKIGETTI(ClassName,BaseClassName)																		\
	OINTERFACE(ClassName)

//only used for Core Interface class
#define OINTERFACE_IMPL(ClassName)			\
	OTYPE_INFO_IMPL(ClassName,ClassName)																	\
	bp::scope ClassName::OrkigeMetaExport(const char * currentOrkigeModuleName) {									\
		typedef ClassName ExposedClassType;\
		bp::class_< ClassName , /*boost::noncopyable,*/ boost::shared_ptr<ClassName> > py_class( ClassName::getClassTypeInfo().getName().c_str()  );

//used for classes that derive from interface but not from object
#define OABSTRACT_IMPL(ClassName)				\
	OTYPE_INFO_IMPL(ClassName,ClassName)\
	bp::scope ClassName::OrkigeMetaExport(const char * currentOrkigeModuleName) {			\
		typedef ClassName ExposedClassType;		\
		bp::class_< OSelf, bp::bases<OParent> ,/*boost::noncopyable,*/ boost::shared_ptr<OParent> > py_class( ClassName::getClassTypeInfo().getName().c_str()  );

//class that inherits object an has default constructor
#define OOBJECT_IMPL(ClassName)					\
	OTYPE_INFO_IMPL(ClassName,ClassName)\
	bp::scope ClassName::OrkigeMetaExport(const char * currentOrkigeModuleName) {			\
		typedef ClassName ExposedClassType;		\
		bp::class_< ClassName, bp::bases<OParent>,boost::shared_ptr<OParent> > py_class( ClassName::getClassTypeInfo().getName().c_str() );

#define OOBJECT_TEMPLATE_IMPL(ClassName,TemplateArgument)																											\
	OTYPE_INFO_IMPL(ClassName<TemplateArgument>,ClassName##TemplateArgument)																						\
	bp::scope ClassName<TemplateArgument>::OrkigeMetaExport(const char * currentOrkigeModuleName) {																		\
	typedef ClassName<TemplateArgument> ExposedClassType;																											\
	bp::class_<ExposedClassType, bp::bases<OParent>,boost::shared_ptr<ExposedClassType> > py_class(ClassName::getClassTypeInfo().getName().c_str());

#define OMACRO_COMMA_SEPERATOR ,

//class that inherits object an has default constructor
#define OOBJECT_TEMPLATE_IMPL2(ClassName,TemplateArgument1,TemplateArgument2)																						\
	OTYPE_INFO_IMPL(ClassName<TemplateArgument1 OMACRO_COMMA_SEPERATOR TemplateArgument2>,ClassName##TemplateArgument1##TemplateArgument2)							\
	bp::scope ClassName<TemplateArgument1,TemplateArgument2>::OrkigeMetaExport(const char * currentOrkigeModuleName) {														\
	typedef ClassName<TemplateArgument1,TemplateArgument2> ExposedClassType;																						\
	bp::class_<ExposedClassType, bp::bases<OParent>,boost::shared_ptr<ExposedClassType> > py_class(ClassName::getClassTypeInfo().getName().c_str());


//class that inherits object an has default constructor
#define OVIRTUAL_OBJECT_IMPL(ClassName)\
OOBJECT_IMPL(ClassName)

#define OVIRTUAL_INTERFACE_IMPL(ClassName)																													\
	bp::scope ClassName::OrkigeMetaExport(const char * currentOrkigeModuleName) {																														\
		typedef ClassName					ExposedClassType;																								\
		typedef	ClassName##VirtualWrapper	OWRAPPER_HAS_TO_BE_DECLARED_BEFORE_VIRTUAL_EXPOSING_##ClassName;/*little hint for compile error ;)*/			\
		typedef	ClassName##VirtualWrapper	WrappedClass;																									\
		std::stringstream classname;																														\
		classname << "NotWrapped" << #ClassName ;																											\
		bp::class_<ClassName, /*boost::noncopyable, */boost::shared_ptr<ClassName> > py_class(classname.str().c_str(), bp::no_init);				\
		bp::class_<ClassName##VirtualWrapper, /*boost::noncopyable,*/ boost::shared_ptr<ClassName##VirtualWrapper> > py_virtual_class( #ClassName);


//class that inherits from 2 base classes an has default constructor
#define OOBJECT_IMPL2(ClassName)			\
	bp::scope ClassName::OrkigeMetaExport(const char * currentOrkigeModuleName) {		\
		typedef ClassName ExposedClassType;	\
		bp::class_< ClassName, bp::bases<OParent,OParent2>,boost::shared_ptr<OParent> > py_class(#ClassName);
		
#define OSIMPLEEXPORT(ClassName,ExportName)
#define OSIMPLEEXPORT_END

#define OWRAPPER_START(ClassName)
		
#define OWRAPPER_CONSTRUCTOR0()								

#define OWRAPPER_CONSTRUCTOR1(PARAM1)						

#define OWRAPPER_CONSTRUCTOR2(param1,param2)				

#define OWRAPPER_CONSTRUCTOR3(param1,param2,param3)			

#define OWRAPPER_CONSTRUCTOR4(param1,param2,param3,param4)	

#define OWRAPPER_CONSTRUCTOR5(p1,p2,p3,p4,p5)				

#define OWRAPPER_END										

#define OWRAPPER_FUNC(ReturnValueType,FunctionName)	

#define OWRAPPER_FUNC1(ReturnValueType,FunctionName,Param)
//standard function
#define OSTATICFUNC(FunctionName)	
//standard function
#define OSTATICFUNCR(FunctionName,callPolicy)	
//standard function
#define OSTATICFUNCR2(FunctionName,cp_pt1,cp_pt2)	

//standard function
#define OFUNC(FunctionName)									py_class.def(#FunctionName,&ExposedClassType::FunctionName);

#define OFUNCOVERL(FunctionName, CCast)						py_class.def(#FunctionName,(CCast)&ExposedClassType::FunctionName);

//standard function
#define OVIRTUAL_FUNC(FunctionName)							OFUNC(FunctionName)	

#define OVIRTUAL_NOTWRAPPED_FUNC(FunctionName)				OFUNC(FunctionName)

#define OVIRTUAL_NOTWRAPPED_FUNCR(FunctionName,callPolicy)	OFUNCR(FunctionName,callPolicy)

#define OVIRTUAL_NOTWRAPPED_FUNCCR(FunctionName)			OFUNCCR(FunctionName)
//function with other name in python than in c++
#define OFUNC_REN(FunctionName,PythonFunctionName)			py_class.def(#PythonFunctionName,&ExposedClassType::FunctionName);

//function with call policy 
#define OFUNCR(FunctionName,callPolicy)						py_class.def(#FunctionName,&ExposedClassType::FunctionName,callPolicy);

//function with internal reference call policy 
#define OFUNCIR(FunctionName)								OFUNC(FunctionName/*,bp::return_internal_reference<>()*/)

//function with const reference call policy 
#define OFUNCCR(FunctionName)								OFUNC(FunctionName/*,bp::return_value_policy<bp::copy_const_reference>()*/)

#define OARG(arg_name)										bp::object()

#define ODEFARG(arg_name,arg_default_value)					OARG(arg_name)=arg_default_value

#define OARGNONE(arg_name)									ODEFARG(arg_name,bp::object())

#define OTPLFUNCDEFARGS(TypeName,FunctionName,args)			py_class.def(#FunctionName,&ExposedClassType::FunctionName<TypeName>/*,args*/);

//template function
#define OTPLFUNC(TypeName,FunctionName)						py_class.def(#FunctionName,&ExposedClassType::FunctionName<TypeName>);

//template function
#define OTPLFUNCR(FunctionName,TypeName,callPolicy)			py_class.def(#FunctionName,&ExposedClassType::FunctionName<TypeName>,callPolicy);

//function overloads
//#define OFUNC_OVERL1(ReturnValue,FunctionName,ParamType1)	py_class.def(#FunctionName,(ReturnValue(ExposedClassType::*)(ParamType1))&ExposedClassType::FunctionName);

//function overloads
#define OFUNC_OVERL(ReturnValue,FunctionName)				{ReturnValue (ExposedClassType::*OVERLOAD_##FunctionName##_ReturnValue)() = &ExposedClassType::FunctionName;\
															py_class.def(#FunctionName, OVERLOAD_##FunctionName##_ReturnValue);}

#define OFUNC_OVERL1(ReturnValue,FunctionName,ParamType1)	{ReturnValue (ExposedClassType::*OVERLOAD_##FunctionName##_ReturnValue)(ParamType1) = &ExposedClassType::FunctionName;\
															py_class.def(#FunctionName, OVERLOAD_##FunctionName##_ReturnValue);}

#define OFUNC_OVERL2(ReturnValue,FunctionName,ParamType1,ParamType2)	{ReturnValue (ExposedClassType::*OVERLOAD_##FunctionName##_ReturnValue)(ParamType1,ParamType2) = &ExposedClassType::FunctionName;\
															py_class.def(#FunctionName, OVERLOAD_##FunctionName##_ReturnValue);}

#define OFUNCR_OVERL(ReturnValue,FunctionName,callPolicy)	{ReturnValue (ExposedClassType::*OVERLOAD_##FunctionName##_ReturnValue)() = &ExposedClassType::FunctionName;\
															py_class.def(#FunctionName, OVERLOAD_##FunctionName##_ReturnValue/*,callPolicy*/);}

#define OFUNCCR_OVERL(ReturnValue,FunctionName)				OFUNCR_OVERL(ReturnValue,FunctionName,bp::return_value_policy<bp::copy_const_reference>())

//python property
#define OPROP(PropertyName,FunctionName)					py_class.add_property(PropertyName,&ExposedClassType::FunctionName);

//python property
#define OVAR(VariableName)									py_class.def_readwrite(#VariableName,&ExposedClassType::VariableName);

//python property
#define OSTATICVAR(VariableName)							py_class.def_readonly(#VariableName,&ExposedClassType::VariableName);

//python property
#define OCONSTVAR(VariableName)								py_class.def_readonly(#VariableName,&ExposedClassType::VariableName);

//default constructor
#define OCONSTRUCTOR0()										py_class.def(bp::constructor<>());

//constructor with 1 paramter
#define OCONSTRUCTOR1(param1)								py_class.def(bp::constructor<param1>());

#define OCONSTRUCTOR2(param1,param2)						py_class.def(bp::constructor<param1,param2>());

#define OCONSTRUCTOR3(param1,param2,param3)					py_class.def(bp::constructor<param1,param2,param3>());

#define OCONSTRUCTOR4(param1,param2,param3,param4)			py_class.def(bp::constructor<param1,param2,param3,param4>());

#define OCONSTRUCTOR5(param1,param2,param3,param4,param5)	py_class.def(bp::constructor<param1,param2,param3,param4,param5>());

#define OVIRTUAL_CONSTRUCTOR0()								OCONSTRUCTOR0()

#define OVIRTUAL_CONSTRUCTOR1(param1)						OCONSTRUCTOR1(param1)

#define OVIRTUAL_CONSTRUCTOR2(param1,param2)				OCONSTRUCTOR2(param1,param2)

#define OVIRTUAL_CONSTRUCTOR3(param1,param2,param3)			OCONSTRUCTOR3(param1,param2,param3)

#define OVIRTUAL_CONSTRUCTOR4(param1,param2,param3,param4)	OCONSTRUCTOR4(param1,param2,param3,param4)

#define OVIRTUAL_CONSTRUCTOR5(p1,p2,p3,p4,p5)				OCONSTRUCTOR5(p1,p2,p3,p4,p5)

#define OPICKABLE()											py_class.enable_pickling();

#define OSINGLETON()										OSTATICFUNCR2(getSingleton,bp::return_value_policy< bp::reference_existing_object, bp::default_call_policies >())

#define OENUM_START(EnumName)								enum_<ExposedClassType::EnumName>( #EnumName )

#define OENUM_VALUE(ValueName)								.value(#ValueName, ValueName)

#define OENUM_END											;

#define OOBJECT_END											; return py_class; };

#define OEXPORT(ClassName)
		
#define OEXPORTCONTAINER(ContainerType, STLType)
#define OEXPORTMAPTYPE(		Type)	

#define OEXPORTVECTORTYPE(	Type)	

#define OEXPORTLISTTYPE(	Type)
#define OEXPORTDEQUETYPE(	Type)
#define OEXPORTSETTYPE(		Type)
#define OEXPORTPAIRTYPE(	Type)

#define OEXPORTMAP(		Name, KeyType, ValueType)
														
#define OEXPORTPAIR(	Name, KeyType, ValueType)							
#define OEXPORTVECTOR(	Name, ValueType)

#define OEXPORTLIST(	Name, ValueType)

#define OEXPORTDEQUE(	Name, ValueType)

#define OEXPORTSET(		Name, ValueType)

#define ORKIGE_MODULE(ModuleName)
    
#define ORKIGE_MODULE_END

#endif //ORKIGE_NOSCRIPT
#endif //__Meta_Lua_h__9_9_2010__18_37_39__
