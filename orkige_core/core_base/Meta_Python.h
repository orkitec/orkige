/********************************************************************
	created:	Thursday 2010/09/09 at 18:38
	filename: 	Meta_Python.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
	
	purpose:	
*********************************************************************/
#ifndef __Meta_Python_h__9_9_2010__18_38_49__
#define __Meta_Python_h__9_9_2010__18_38_49__

#ifndef ORKIGE_NOSCRIPT
#include <boost/python/detail/wrap_python.hpp>
#include <boost/python.hpp>
#include "boost/python/suite/indexing/container_suite.hpp"
#include "boost/python/suite/indexing/map.hpp"
#include "boost/python/suite/indexing/vector.hpp"
#include "boost/python/suite/indexing/deque.hpp"
#include "boost/python/suite/indexing/list.hpp"
#include "boost/python/suite/indexing/pair.hpp"
#include "boost/python/suite/indexing/set.hpp"

namespace bp = boost::python;

#define OMETACLASS(ClassName)															\		
	public:																				\
		static void OrkigeMetaExport(const char * currentOrkigeModuleName);				\
	private:	

#define OINTERFACE(ClassName)															\
	OTYPE_INFO(ClassName)																\
	OMETACLASS(ClassName)
																		

//used for all objects and derived objects
//used for all objects and derived objects
#define OOBJECT(ClassName,BaseClassName)																	\
	friend Orkige::TypeManager;																				\
	friend Orkige::InterfaceTypeFactory;																		\
	friend bp::class_< ClassName, bp::bases<BaseClassName>,boost::shared_ptr<ClassName> >;					\
	friend bp::objects::pointer_holder<optr<ClassName>, ClassName>;											\
	ORKIGETTI(ClassName,BaseClassName)																		\
	OINTERFACE(ClassName)

//object with default constructor
#define OOBJECT_WD(ClassName,BaseClassName)																	\
	protected:																								\
		ClassName(){}																						\
	private:																								\
		OOBJECT(ClassName,BaseClassName)
	

#define OOBJECT_TEMPLATE(ClassName,TemplateArgument,BaseClassName)											\
	friend Orkige::TypeManager;																				\
	friend Orkige::InterfaceTypeFactory;																		\
	friend bp::class_< ClassName, bp::bases<BaseClassName>,boost::shared_ptr<ClassName> >;					\
	friend bp::objects::pointer_holder<optr<ClassName>, ClassName>;											\
	ORKIGETTI(ClassName,BaseClassName)																		\
	OINTERFACE(ClassName)

#define OOBJECT2(ClassName,BaseClassName,BaseClassName2)													\
	friend Orkige::TypeManager;																				\
	friend Orkige::InterfaceTypeFactory;																		\
	friend bp::class_< ClassName, bp::bases<BaseClassName,BaseClassName2>,boost::shared_ptr<ClassName> >;	\
	friend bp::objects::pointer_holder<optr<ClassName>, ClassName>;											\
	ORKIGETTI2(ClassName,BaseClassName,BaseClassName2)														\
	OINTERFACE(ClassName)

//only used for Core Interface class
#define OINTERFACE_IMPL(ClassName)																																\
	OTYPE_INFO_IMPL(ClassName,ClassName)																														\
	void ClassName::OrkigeMetaExport(const char * currentOrkigeModuleName) {																						\
		typedef ClassName ExposedClassType;																														\
		bp::class_< ClassName , boost::noncopyable, boost::shared_ptr<ClassName>> py_class( ClassName::getClassTypeInfo().getName().c_str() , bp::no_init );

//used for classes that derive from interface but not from object
#define OABSTRACT_IMPL(ClassName)																																	\
	OTYPE_INFO_IMPL(ClassName,ClassName)																															\
	void ClassName::OrkigeMetaExport(const char * currentOrkigeModuleName) {																							\
		typedef ClassName ExposedClassType;																															\
		bp::class_< OSelf, bp::bases<OParent> ,boost::noncopyable, boost::shared_ptr<OSelf>> py_class(ClassName::getClassTypeInfo().getName().c_str(),bp::no_init);

//class that inherits object an has default constructor
#define OOBJECT_IMPL(ClassName)																																\
	OTYPE_INFO_IMPL(ClassName,ClassName)																													\
	void ClassName::OrkigeMetaExport(const char * currentOrkigeModuleName) {																					\
		Orkige::TypeManager::getSingleton().registerType<ClassName>(#ClassName);																				\
		typedef ClassName ExposedClassType;																													\
		bp::class_< ClassName, bp::bases<OParent>,boost::shared_ptr<ClassName> > py_class(ClassName::getClassTypeInfo().getName().c_str(),bp::no_init);

//class that inherits object an has default constructor
#define OVIRTUAL_OBJECT_IMPL(ClassName)																														\
	OTYPE_INFO_IMPL(ClassName,ClassName)																													\
	void ClassName::OrkigeMetaExport(const char * currentOrkigeModuleName) {																					\
		typedef ClassName					ExposedClassType;																								\
		typedef	ClassName##VirtualWrapper	OWRAPPER_HAS_TO_BE_DECLARED_BEFORE_VIRTUAL_EXPOSING_##ClassName;/*little hint for compile error ;)*/			\
		typedef	ClassName##VirtualWrapper	WrappedClass;																									\
		std::stringstream classname;																														\
		classname << "NotWrapped" << #ClassName ;																											\
		boost::python::class_<ClassName, boost::python::bases<OParent> ,boost::noncopyable, boost::shared_ptr<ClassName>>									\
			py_class(classname.str().c_str(),bp::no_init);																									\
		boost::python::class_<ClassName##VirtualWrapper, boost::python::bases<OParent> ,boost::noncopyable, boost::shared_ptr<ClassName##VirtualWrapper>>	\
			py_virtual_class( ClassName::getClassTypeInfo().getName().c_str() ,bp::no_init);

#define OVIRTUAL_INTERFACE_IMPL(ClassName)																													\
	OTYPE_INFO_IMPL(ClassName,ClassName)																													\
	void ClassName::OrkigeMetaExport(const char * currentOrkigeModuleName) {																					\
		typedef ClassName					ExposedClassType;																								\
		typedef	ClassName##VirtualWrapper	OWRAPPER_HAS_TO_BE_DECLARED_BEFORE_VIRTUAL_EXPOSING_##ClassName;/*little hint for compile error ;)*/			\
		typedef	ClassName##VirtualWrapper	WrappedClass;																									\
		std::stringstream classname;																														\
		classname << "NotWrapped" << #ClassName ;																											\
		boost::python::class_<ClassName, /*boost::noncopyable, */boost::shared_ptr<ClassName>> py_class(classname.str().c_str(), bp::no_init);				\
		boost::python::class_<ClassName##VirtualWrapper, boost::noncopyable, boost::shared_ptr<ClassName##VirtualWrapper>> py_virtual_class( ClassName::getClassTypeInfo().getName().c_str(), bp::no_init);


//class that inherits from 2 base classes an has default constructor
#define OOBJECT_IMPL2(ClassName)																																	\
	OTYPE_INFO_IMPL(ClassName,ClassName)																															\
	void ClassName::OrkigeMetaExport(const char * currentOrkigeModuleName) {																							\
		Orkige::TypeManager::getSingleton().registerType<ClassName>(#ClassName);																						\
		typedef ClassName ExposedClassType;																															\
		bp::class_< ClassName, bp::bases<OParent,OParent2>,boost::shared_ptr<ClassName> > py_class(ClassName::getClassTypeInfo().getName().c_str(),bp::no_init);

//class that inherits object an has default constructor
#define OOBJECT_TEMPLATE_IMPL(ClassName,TemplateArgument)																											\
	OTYPE_INFO_IMPL(ClassName<TemplateArgument>,ClassName##TemplateArgument)																						\
	void ClassName<TemplateArgument>::OrkigeMetaExport(const char * currentOrkigeModuleName) {																		\
	typedef ClassName<TemplateArgument> ExposedClassType;																											\
	bp::class_<ExposedClassType, bp::bases<OParent>,boost::shared_ptr<ExposedClassType> > py_class(ClassName::getClassTypeInfo().getName().c_str(),bp::no_init);

#define OMACRO_COMMA_SEPERATOR ,

//class that inherits object an has default constructor
#define OOBJECT_TEMPLATE_IMPL2(ClassName,TemplateArgument1,TemplateArgument2)																						\
	OTYPE_INFO_IMPL(ClassName<TemplateArgument1 OMACRO_COMMA_SEPERATOR TemplateArgument2>,ClassName##TemplateArgument1##TemplateArgument2)							\
	void ClassName<TemplateArgument1,TemplateArgument2>::OrkigeMetaExport(const char * currentOrkigeModuleName) {														\
	typedef ClassName<TemplateArgument1,TemplateArgument2> ExposedClassType;																						\
	bp::class_<ExposedClassType, bp::bases<OParent>,boost::shared_ptr<ExposedClassType> > py_class(ClassName::getClassTypeInfo().getName().c_str(),bp::no_init);

#define OSIMPLEEXPORT(ClassName,ExportName)	\
	{										\
		typedef ClassName ExposedClassType;	\
		bp::class_<ClassName, boost::noncopyable, boost::shared_ptr<ClassName>> py_class(#ExportName,bp::no_init); 

#define OSIMPLEEXPORT_END ;};

#define OWRAPPER_START(ClassName)													\
	struct ORKIGE_CORE_DLL ClassName##VirtualWrapper : ClassName, bp::wrapper<ClassName>	\
	{																				\
		typedef ClassName##VirtualWrapper	VirtualWrapperClassName;				\
		typedef ClassName					VirtualWrapperBaseClassName;

#define OWRAPPER_CONSTRUCTOR0()								VirtualWrapperClassName(){}

#define OWRAPPER_CONSTRUCTOR1(param1)						VirtualWrapperClassName(param1 p1) : VirtualWrapperBaseClassName(p1){}

#define OWRAPPER_CONSTRUCTOR2(param1,param2)				VirtualWrapperClassName(param1 p1,param2 p2) : VirtualWrapperBaseClassName(p1,p2){}

#define OWRAPPER_CONSTRUCTOR3(param1,param2,param3)			VirtualWrapperClassName(param1 p1,param2 p2,param3 p3) : VirtualWrapperBaseClassName(p1,p2,p3){}

#define OWRAPPER_CONSTRUCTOR4(param1,param2,param3,param4)	VirtualWrapperClassName(param1 p1,param2 p2,param3 p3,param4 p4) : VirtualWrapperBaseClassName(p1,p2,p3,p4){}

#define OWRAPPER_CONSTRUCTOR5(p1,p2,p3,p4,p5)				VirtualWrapperClassName(p1 _p1,p2 _p2,p3 _p3,p4 _p4,p5 _p5) : VirtualWrapperBaseClassName(_p1,_p2,_p3,_p4,_p5){}

#define OWRAPPER_END										};

#define OWRAPPER_FUNC(ReturnValueType,FunctionName)															\
	ReturnValueType FunctionName()																			\
	{																										\
		if(bp::override f = this->get_override( #FunctionName ))											\
			return bp::call<ReturnValueType>(f.ptr());														\
		return VirtualWrapperBaseClassName::FunctionName();													\
	}																										\
	ReturnValueType default_##FunctionName() { return this->VirtualWrapperBaseClassName::FunctionName(); }

#define OWRAPPER_FUNC1(ReturnValueType,FunctionName,Param)													\
	ReturnValueType FunctionName(Param p)																	\
	{																										\
		if(bp::override f = this->get_override( #FunctionName ))											\
			return bp::call<ReturnValueType>(f.ptr(), p);													\
		return VirtualWrapperBaseClassName::FunctionName(p);												\
	}																										\
	ReturnValueType default_##FunctionName(Param p) { return this->VirtualWrapperBaseClassName::FunctionName(p); }
//standard function
#define OSTATICFUNC(FunctionName)							py_class.def(#FunctionName,&ExposedClassType::FunctionName);\
															py_class.staticmethod(#FunctionName);
//standard function
#define OSTATICFUNCR(FunctionName,callPolicy)				py_class.def(#FunctionName,&ExposedClassType::FunctionName,callPolicy);\
															py_class.staticmethod(#FunctionName);

#define OSTATICFUNCCR(FunctionName)							OSTATICFUNCR(FunctionName,bp::return_value_policy<bp::copy_const_reference>())
//standard function
#define OSTATICFUNCR2(FunctionName,cp_pt1,cp_pt2)			py_class.def(#FunctionName,&ExposedClassType::FunctionName,cp_pt1,cp_pt2);\
															py_class.staticmethod(#FunctionName);

//standard function
#define OFUNC(FunctionName)									py_class.def(#FunctionName,&ExposedClassType::FunctionName);

//standard function
#define OVIRTUAL_FUNC(FunctionName)							OFUNC(FunctionName)\
															py_virtual_class.def(#FunctionName, &ExposedClassType::FunctionName,&WrappedClass::default_##FunctionName);

#define OVIRTUAL_NOTWRAPPED_FUNC(FunctionName)				OFUNC(FunctionName)\
															py_virtual_class.def(#FunctionName, &ExposedClassType::FunctionName);

#define OVIRTUAL_NOTWRAPPED_FUNCR(FunctionName,callPolicy)	OFUNCR(FunctionName,callPolicy)\
															py_virtual_class.def(#FunctionName, &ExposedClassType::FunctionName,callPolicy);

#define OVIRTUAL_NOTWRAPPED_FUNCCR(FunctionName)			OFUNCCR(FunctionName)\
															py_virtual_class.def(#FunctionName, &ExposedClassType::FunctionName,bp::return_value_policy<bp::copy_const_reference>());
//function with other name in python than in c++
#define OFUNC_REN(FunctionName,PythonFunctionName)			py_class.def(#PythonFunctionName,&ExposedClassType::FunctionName);

//function with call policy 
#define OFUNCR(FunctionName,callPolicy)						py_class.def(#FunctionName,&ExposedClassType::FunctionName,callPolicy);

//function with internal reference call policy 
#define OFUNCIR(FunctionName)								OFUNCR(FunctionName,bp::return_internal_reference<>())

//function with const reference call policy 
#define OFUNCCR(FunctionName)								OFUNCR(FunctionName,bp::return_value_policy<bp::copy_const_reference>())

//template function
#define OTPLFUNC(TypeName,FunctionName)						py_class.def(#FunctionName,&ExposedClassType::FunctionName<TypeName>);

#define OARG(arg_name)										bp::arg(#arg_name)

#define ODEFARG(arg_name,arg_default_value)					OARG(arg_name)=arg_default_value

#define OARGNONE(arg_name)									ODEFARG(arg_name,bp::object())

#define OTPLFUNCDEFARGS(TypeName,FunctionName,args)			py_class.def(#FunctionName,&ExposedClassType::FunctionName<TypeName>,args);

//template function
#define OTPLFUNCR(FunctionName,TypeName,callPolicy)			py_class.def(#FunctionName,&ExposedClassType::FunctionName<TypeName>,callPolicy);

//function overloads
#define OFUNC_OVERL(ReturnValue,FunctionName)				{ReturnValue (ExposedClassType::*OVERLOAD_##FunctionName##_ReturnValue)() = &ExposedClassType::FunctionName;\
															py_class.def(#FunctionName, OVERLOAD_##FunctionName##_ReturnValue);}

#define OFUNC_OVERL1(ReturnValue,FunctionName,ParamType1)	{ReturnValue (ExposedClassType::*OVERLOAD_##FunctionName##_ReturnValue)(ParamType1) = &ExposedClassType::FunctionName;\
															py_class.def(#FunctionName, OVERLOAD_##FunctionName##_ReturnValue);}

#define OFUNC_OVERL2(ReturnValue,FunctionName,ParamType1,ParamType2)	{ReturnValue (ExposedClassType::*OVERLOAD_##FunctionName##_ReturnValue)(ParamType1,ParamType2) = &ExposedClassType::FunctionName;\
															py_class.def(#FunctionName, OVERLOAD_##FunctionName##_ReturnValue);}

#define OFUNCR_OVERL(ReturnValue,FunctionName,callPolicy)	{ReturnValue (ExposedClassType::*OVERLOAD_##FunctionName##_ReturnValue)() = &ExposedClassType::FunctionName;\
															py_class.def(#FunctionName, OVERLOAD_##FunctionName##_ReturnValue,callPolicy);}

#define OFUNCCR_OVERL(ReturnValue,FunctionName)				OFUNCR_OVERL(ReturnValue,FunctionName,bp::return_value_policy<bp::copy_const_reference>())
//python property
#define OPROP(PropertyName,FunctionName)					py_class.add_property(PropertyName,&ExposedClassType::FunctionName);

#define OGETSETPROP(PropertyName)							py_class.add_property(#PropertyName,&ExposedClassType::get##PropertyName,&ExposedClassType::set##PropertyName);

//python property
#define OVAR(VariableName)									py_class.def_readwrite(#VariableName,&ExposedClassType::VariableName);

//python property
#define OSTATICVAR(VariableName)							py_class.def_readonly(#VariableName,&ExposedClassType::VariableName);

//python property
#define OCONSTVAR(VariableName)								py_class.def_readonly(#VariableName,&ExposedClassType::VariableName);

//default constructor
#define OCONSTRUCTOR0()										py_class.def(bp::init<>());

//constructor with 1 paramter
#define OCONSTRUCTOR1(param1)								py_class.def(bp::init<param1>());

#define OCONSTRUCTOR2(param1,param2)						py_class.def(bp::init<param1,param2>());

#define OCONSTRUCTOR3(param1,param2,param3)					py_class.def(bp::init<param1,param2,param3>());

#define OCONSTRUCTOR4(param1,param2,param3,param4)			py_class.def(bp::init<param1,param2,param3,param4>());

#define OCONSTRUCTOR5(param1,param2,param3,param4,param5)	py_class.def(bp::init<param1,param2,param3,param4,param5>());

#define OVIRTUAL_CONSTRUCTOR0()								OCONSTRUCTOR0() \
															py_virtual_class.def(bp::init<>());

#define OVIRTUAL_CONSTRUCTOR1(param1)						OCONSTRUCTOR1(param1) \
															py_virtual_class.def(bp::init<param1>());

#define OVIRTUAL_CONSTRUCTOR2(param1,param2)				OCONSTRUCTOR2(param1,param2) \
															py_virtual_class.def(bp::init<param1,param2>());

#define OVIRTUAL_CONSTRUCTOR3(param1,param2,param3)			OCONSTRUCTOR3(param1,param2,param3) \
															py_virtual_class.def(bp::init<param1,param2,param3>());

#define OVIRTUAL_CONSTRUCTOR4(param1,param2,param3,param4)	OCONSTRUCTOR4(param1,param2,param3,param4) \
															py_virtual_class.def(bp::init<param1,param2,param3,param4>());

#define OVIRTUAL_CONSTRUCTOR5(p1,p2,p3,p4,p5)				OCONSTRUCTOR5(p1,p2,p3,p4,p5) \
															py_virtual_class.def(bp::init<p1,p2,p3,p4,p5>());

#define OPICKABLE()											py_class.enable_pickling();

#define OSINGLETON()										OSTATICFUNCR2(getSingleton,bp::return_value_policy< bp::reference_existing_object, bp::default_call_policies >())

#define OENUM_START(EnumName)								enum_<ExposedClassType::EnumName>( #EnumName )

#define OENUM_VALUE(ValueName)								.value(#ValueName, ValueName)

#define OENUM_END											;

#define OOBJECT_END											;};

#define OEXPORT(ClassName)									Orkige::ClassName::OrkigeMetaExport(CURRENT_ORKIGE_MODULE_NAME);

#define OEXPORTCONTAINER(ContainerType, STLType)														\
{																										\
	typedef bp::class_< ContainerType > ContainerType##_exposer_t;										\
	ContainerType##_exposer_t ContainerType##_exposer = ContainerType##_exposer_t( #ContainerType );	\
	bp::scope ContainerType##_scope( ContainerType##_exposer );											\
	ContainerType##_exposer.def( bp::indexing::STLType##_suite< ContainerType >() );					\
}

#define OEXPORTMAPTYPE(		Type)							OEXPORTCONTAINER(Type,	map		)

#define OEXPORTVECTORTYPE(	Type)							OEXPORTCONTAINER(Type,	vector	)

#define OEXPORTLISTTYPE(	Type)							OEXPORTCONTAINER(Type,	list	)

#define OEXPORTDEQUETYPE(	Type)							OEXPORTCONTAINER(Type,	deque	)

#define OEXPORTSETTYPE(		Type)							OEXPORTCONTAINER(Type,	set		)

#define OEXPORTPAIRTYPE(	Type)							OEXPORTCONTAINER(Type,	pair	)

#define OEXPORTMAP(		Name, KeyType, ValueType)			{typedef std::map<KeyType, ValueType>	Name;\
															OEXPORTMAPTYPE(Name)}

#define OEXPORTPAIR(	Name, KeyType, ValueType)			{typedef std::pair<KeyType,ValueType>	Name;\
															OEXPORTPAIRTYPE(Name)}
																													
#define OEXPORTVECTOR(	Name, ValueType)					{typedef std::vector<ValueType>			Name;\
															OEXPORTVECTORTYPE(Name)}

#define OEXPORTLIST(	Name, ValueType)					{typedef std::list<ValueType>			Name;\
															OEXPORTLISTTYPE(Name)}

#define OEXPORTDEQUE(	Name, ValueType)					{typedef std::deque<ValueType>			Name;\
															OEXPORTDEQUETYPE(Name)}

#define OEXPORTSET(		Name, ValueType)					{typedef std::set<ValueType>			Name;\
															OEXPORTSETTYPE(Name)}

#define ORKIGE_MODULE(ModuleName)							BOOST_PYTHON_MODULE(ModuleName) \
															{\
															const char* CURRENT_ORKIGE_MODULE_NAME = #ModuleName;\
															oDebugMsg("core",0,"*** Init Module "<<CURRENT_ORKIGE_MODULE_NAME<<"!");

#define ORKIGE_MODULE_END									};

#endif //ORKIGE_NOSCRIPT
#endif //__Meta_Python_h__9_9_2010__18_38_49__
