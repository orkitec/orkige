/********************************************************************
	created:	Thursday 2010/09/09 at 18:38
	filename: 	Meta_None.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
#ifndef __Meta_None_h__9_9_2010__18_38_06__
#define __Meta_None_h__9_9_2010__18_38_06__


//! backend-neutral usertype openers for hand-written OrkigeMetaExport bodies
//! (TypeInfo.cpp, AttributeHolder.h) - no scripting: nothing to register.
//! The exposed type is passed as __VA_ARGS__ so template arguments containing
//! commas survive macro expansion; ExportName may be a runtime expression.
#define OUSERTYPE(ExportName, ...)
#define OUSERTYPE_BASED(ExportName, BaseClassName, ...)

#define OMETACLASS(ClassName)															\
	public:																				\
		/** @brief Export and init ClassName meta information */						\
		static void OrkigeMetaExport(const char * currentOrkigeModuleName);				\
	private:

#define OINTERFACE(ClassName)															\
	OTYPE_INFO(ClassName)																\
	OMETACLASS(ClassName)
	
#define OINTERFACE_EXPORT(ClassName, EXPORTDEFINITION)									\
	OTYPE_INFO_EXPORT(ClassName, EXPORTDEFINITION)										\
	OMETACLASS(ClassName)

//used for all objects and derived objects
#define OOBJECT(ClassName, BaseClassName)																	\
	friend class Orkige::TypeManager;																		\
	friend class ObjectFactory<Orkige::Interface * (), Orkige::String>;										\
	ORKIGETTI(ClassName,BaseClassName)																		\
	OINTERFACE(ClassName)

//used for all objects and derived objects
#define OOBJECT_EXPORT(ClassName, BaseClassName, EXPORTDEFINITION)											\
	friend class Orkige::TypeManager;																		\
	friend class ObjectFactory<Orkige::Interface * (), Orkige::String>;										\
	ORKIGETTI(ClassName,BaseClassName)																		\
	OINTERFACE_EXPORT(ClassName, EXPORTDEFINITION)

//object with default constructor
#define OOBJECT_WD(ClassName,BaseClassName)																	\
	OOBJECT(ClassName,BaseClassName)																		\
	public:																									\
		ClassName(){}																						\
	private:																								

#define OOBJECT2(ClassName,BaseClassName,BaseClassName2)													\
	friend class Orkige::TypeManager;																		\
	friend class ObjectFactory<Orkige::Interface * (), Orkige::String>;														\
	ORKIGETTI2(ClassName,BaseClassName,BaseClassName2)														\
	OINTERFACE(ClassName)


#define OOBJECT_TEMPLATE(ClassName,TemplateArgument,BaseClassName)											\
	friend class Orkige::TypeManager;																		\
	friend class ObjectFactory<Orkige::Interface * (), Orkige::String>;														\
	ORKIGETTI(ClassName,BaseClassName)																		\
	OINTERFACE(ClassName)

//only used for Core Interface class
#define OINTERFACE_IMPL(ClassName)																			\
	OTYPE_INFO_IMPL(ClassName,ClassName)																	\
	void ClassName::OrkigeMetaExport(const char * currentOrkigeModuleName) {

//used for classes that derive from interface but not from object
#define OABSTRACT_IMPL(ClassName)																			\
	OTYPE_INFO_IMPL(ClassName,ClassName)																	\
	void ClassName::OrkigeMetaExport(const char * currentOrkigeModuleName) {	

//class that inherits object an has default constructor
#define OOBJECT_IMPL(ClassName)																				\
	OTYPE_INFO_IMPL(ClassName,ClassName)																	\
	void ClassName::OrkigeMetaExport(const char * currentOrkigeModuleName) {									\
		Orkige::TypeManager::getSingleton().registerType<ClassName>(#ClassName);

#define OOBJECT_TEMPLATE_IMPL(ClassName,TemplateArgument)													\
	OTEMPLATE_TYPE_INFO_IMPL(ClassName<TemplateArgument>,ClassName##TemplateArgument)								\
	template <> void ClassName<TemplateArgument>::OrkigeMetaExport(const char * currentOrkigeModuleName) {

#define OMACRO_COMMA_SEPERATOR ,

//class that inherits object an has default constructor
#define OOBJECT_TEMPLATE_IMPL2(ClassName,TemplateArgument1,TemplateArgument2)																						\
	OTEMPLATE_TYPE_INFO_IMPL(ClassName<TemplateArgument1 OMACRO_COMMA_SEPERATOR TemplateArgument2>,ClassName##TemplateArgument1##TemplateArgument2)							\
	template <> void ClassName<TemplateArgument1,TemplateArgument2>::OrkigeMetaExport(const char * currentOrkigeModuleName) {

//class that inherits object an has default constructor
#define OVIRTUAL_OBJECT_IMPL(ClassName)\
	OOBJECT_IMPL(ClassName)

#define OVIRTUAL_INTERFACE_IMPL(ClassName)
//class that inherits from 2 base classes an has default constructor
#define OOBJECT_IMPL2(ClassName)
#define OSIMPLEEXPORT(ClassName,ExportName)
#define OSIMPLEEXPORT_BASED(ClassName,BaseClassName,ExportName)
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
#define OFUNC(FunctionName)

//member function returning a woptr
#define OFUNCWEAK(FunctionName)

#define OFUNCOVERL(FunctionName, CCast)

//standard function
#define OVIRTUAL_FUNC(FunctionName)							

#define OVIRTUAL_NOTWRAPPED_FUNC(FunctionName)				

#define OVIRTUAL_NOTWRAPPED_FUNCR(FunctionName,callPolicy)	

#define OVIRTUAL_NOTWRAPPED_FUNCCR(FunctionName)			
//function with other name in python than in c++
#define OFUNC_REN(FunctionName,PythonFunctionName)			

//function with call policy 
#define OFUNCR(FunctionName,callPolicy)						

//function with internal reference call policy 
#define OFUNCIR(FunctionName)								

//function with const reference call policy 
#define OFUNCCR(FunctionName)								

#define OARG(arg_name)										

#define ODEFARG(arg_name,arg_default_value)					

#define OARGNONE(arg_name)									

#define OTPLFUNCDEFARGS(TypeName,FunctionName,args)			

//template function
#define OTPLFUNC(TypeName,FunctionName)						

//template function
#define OTPLFUNCR(FunctionName,TypeName,callPolicy)			

//function overloads
//#define OFUNC_OVERL1(ReturnValue,FunctionName,ParamType1)	py_class.def(#FunctionName,(ReturnValue(ExposedClassType::*)(ParamType1))&ExposedClassType::FunctionName);

//function overloads
#define OFUNC_OVERL(ReturnValue,FunctionName)				

#define OFUNC_OVERL1(ReturnValue,FunctionName,ParamType1)	

#define OFUNC_OVERL2(ReturnValue,FunctionName,ParamType1,ParamType2)	

#define OFUNCR_OVERL(ReturnValue,FunctionName,callPolicy)	
#define OFUNCCR_OVERL(ReturnValue,FunctionName)				

//python property
#define OPROP(PropertyName,FunctionName)					

//python property
#define OVAR(VariableName)									

//python property
#define OSTATICVAR(VariableName)							

//python property
#define OCONSTVAR(VariableName)								

//default constructor
#define OCONSTRUCTOR0()										

//constructor with 1 paramter
#define OCONSTRUCTOR1(param1)								

#define OCONSTRUCTOR2(param1,param2)						

#define OCONSTRUCTOR3(param1,param2,param3)					

#define OCONSTRUCTOR4(param1,param2,param3,param4)			

#define OCONSTRUCTOR5(param1,param2,param3,param4,param5)	

#define OVIRTUAL_CONSTRUCTOR0()								

#define OVIRTUAL_CONSTRUCTOR1(param1)						

#define OVIRTUAL_CONSTRUCTOR2(param1,param2)				

#define OVIRTUAL_CONSTRUCTOR3(param1,param2,param3)			

#define OVIRTUAL_CONSTRUCTOR4(param1,param2,param3,param4)	

#define OVIRTUAL_CONSTRUCTOR5(p1,p2,p3,p4,p5)				

#define OPICKABLE()											

#define OSINGLETON()										

#define OENUM_START(EnumName)								

#define OENUM_VALUE(ValueName)								

#define OENUM_END											

#define OOBJECT_END																;};										

#define OEXPORT(ClassName)									ClassName::OrkigeMetaExport(CURRENT_ORKIGE_MODULE_NAME);

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

#define ORKIGE_MODULE(ModuleName)							void init_module_##ModuleName()\
															{\
															CREATE_ORKIGE_MEMORY_MANAGER\
															const char* CURRENT_ORKIGE_MODULE_NAME = #ModuleName;\
															oDebugMsg("core",0,"*** Init Module "<<CURRENT_ORKIGE_MODULE_NAME<<"!");

#define ORKIGE_MODULE_END									};

#define OLOAD_MODULE_STATIC(ModuleName)						void init_module_##ModuleName();\
															init_module_##ModuleName()

#endif //__Meta_None_h__9_9_2010__18_38_06__
