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

// sol2 based Lua meta backend (rewritten 2026; replaces the dead luabind
// implementation). Semantical deviations from the luabind era:
//  * call policies do not exist in sol2, ownership follows from the returned
//    type instead: OFUNCR/OFUNCIR/OFUNCCR and the R/CR overload variants
//    degrade to plain OFUNC registrations.
//  * luabind def() accumulated overloads under one name; sol2 table
//    assignment overwrites: for OFUNC_OVERL*/OTPLFUNC* chains that register
//    the same function name repeatedly the LAST registration wins.
//  * constructors cannot be accumulated per-macro in sol2. Every
//    OCONSTRUCTORn(...) registers a factory function "new<n>" on the class
//    table (returning an optr; the FIRST OCONSTRUCTOR of a given arity in a
//    block wins) and the first OCONSTRUCTOR of the block is additionally
//    wired to the Lua call syntax ClassName(...).
//  * OEXPORTMAP/VECTOR/... container exports are no-ops: sol2 binds std
//    containers natively when they cross the language boundary.
//  * OWRAPPER_*/OVIRTUAL_*: deriving engine classes in Lua is out of scope;
//    the OVIRTUAL macros register the plain class, the wrapper macros are
//    no-ops.
//  * optr is std::shared_ptr, which sol2 understands natively - the luabind
//    get_pointer/get_const_holder shims are gone.
// When no Orkige::ScriptManager singleton exists the registrations target a
// private throwaway Lua state (ScriptManager::metaExportState()), so module
// initialisation - which also performs TypeManager and component factory
// registration inside the same export functions - keeps working in
// applications that never boot scripting.

#include "core_script/ScriptManager.h"

//! the backend-neutral property-reflection registration macros - the Lua
//! OPROPERTY* family below emits these (schema registration) AND a sol2
//! property registration, so the neutral registry and the Lua surface are both
//! populated from ONE declaration (@see core_base/PropertyMacros.h).
#include "core_base/PropertyMacros.h"

#include <sol/sol.hpp>
#include <memory>

//! usertype opener helper - the exposed type is passed as __VA_ARGS__ so
//! template arguments containing commas survive macro expansion
#define ORKIGE_LUA_USERTYPE(LuaName, ...)												\
	typedef __VA_ARGS__ ExposedClassType;												\
	[[maybe_unused]] bool orkigeLuaCtorBound = false;									\
	sol::usertype<ExposedClassType> py_class =											\
		Orkige::ScriptManager::metaExportState().new_usertype<ExposedClassType>(		\
			LuaName, sol::no_constructor);

//! like ORKIGE_LUA_USERTYPE but also registers the base class
#define ORKIGE_LUA_USERTYPE_BASED(LuaName, BaseClassName, ...)							\
	typedef __VA_ARGS__ ExposedClassType;												\
	[[maybe_unused]] bool orkigeLuaCtorBound = false;									\
	sol::usertype<ExposedClassType> py_class =											\
		Orkige::ScriptManager::metaExportState().new_usertype<ExposedClassType>(		\
			LuaName, sol::no_constructor,												\
			sol::base_classes, sol::bases<BaseClassName>());

//! shared factory registration for the OCONSTRUCTORn macros: "new<n>" on the
//! class table (first of an arity wins) plus the Lua call syntax for the
//! first constructor of the block
#define ORKIGE_LUA_REGISTER_CONSTRUCTOR(NewName)										\
		if (!py_class[NewName].valid())													\
		{																				\
			py_class[NewName] = orkigeLuaFactory;										\
		}																				\
		if (!orkigeLuaCtorBound)														\
		{																				\
			orkigeLuaCtorBound = true;													\
			py_class[sol::call_constructor] = sol::factories(orkigeLuaFactory);		\
		}

//! backend-neutral usertype openers for hand-written OrkigeMetaExport bodies
//! (TypeInfo.cpp, AttributeHolder.h) - every backend defines this pair
#define OUSERTYPE(ExportName, ...)									ORKIGE_LUA_USERTYPE(ExportName, __VA_ARGS__)
#define OUSERTYPE_BASED(ExportName, BaseClassName, ...)				ORKIGE_LUA_USERTYPE_BASED(ExportName, BaseClassName, __VA_ARGS__)

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
	void ClassName::OrkigeMetaExport(const char * currentOrkigeModuleName) {								\
		ORKIGE_LUA_USERTYPE(ClassName::getClassTypeInfo().getName(), ClassName)

//used for classes that derive from interface but not from object
#define OABSTRACT_IMPL(ClassName)																			\
	OTYPE_INFO_IMPL(ClassName,ClassName)																	\
	void ClassName::OrkigeMetaExport(const char * currentOrkigeModuleName) {								\
		ORKIGE_LUA_USERTYPE_BASED(ClassName::getClassTypeInfo().getName(), OParent, ClassName)

//class that inherits object an has default constructor
#define OOBJECT_IMPL(ClassName)																				\
	OTYPE_INFO_IMPL(ClassName,ClassName)																	\
	void ClassName::OrkigeMetaExport(const char * currentOrkigeModuleName) {								\
		Orkige::TypeManager::getSingleton().registerType<ClassName>(#ClassName);							\
		ORKIGE_LUA_USERTYPE_BASED(ClassName::getClassTypeInfo().getName(), OParent, ClassName)

#define OOBJECT_TEMPLATE_IMPL(ClassName,TemplateArgument)													\
	OTEMPLATE_TYPE_INFO_IMPL(ClassName<TemplateArgument>,ClassName##TemplateArgument)								\
	template <> void ClassName<TemplateArgument>::OrkigeMetaExport(const char * currentOrkigeModuleName) {	\
		ORKIGE_LUA_USERTYPE_BASED((ClassName<TemplateArgument>::getClassTypeInfo().getName()), OParent, ClassName<TemplateArgument>)

#define OMACRO_COMMA_SEPERATOR ,

//class that inherits object an has default constructor
#define OOBJECT_TEMPLATE_IMPL2(ClassName,TemplateArgument1,TemplateArgument2)																						\
	OTEMPLATE_TYPE_INFO_IMPL(ClassName<TemplateArgument1 OMACRO_COMMA_SEPERATOR TemplateArgument2>,ClassName##TemplateArgument1##TemplateArgument2)							\
	template <> void ClassName<TemplateArgument1,TemplateArgument2>::OrkigeMetaExport(const char * currentOrkigeModuleName) {											\
		ORKIGE_LUA_USERTYPE_BASED((ClassName<TemplateArgument1,TemplateArgument2>::getClassTypeInfo().getName()), OParent, ClassName<TemplateArgument1,TemplateArgument2>)

//class that inherits object an has default constructor
#define OVIRTUAL_OBJECT_IMPL(ClassName)\
	OOBJECT_IMPL(ClassName)

#define OVIRTUAL_INTERFACE_IMPL(ClassName)
//class that inherits from 2 base classes an has default constructor
#define OOBJECT_IMPL2(ClassName)

//expose a foreign (e.g. Ogre) class inside an ORKIGE_MODULE body; nested
//OFUNC*/OENUM_* macros work like inside an OOBJECT_IMPL block
#define OSIMPLEEXPORT(ClassName,ExportName)																	\
	{																										\
		ORKIGE_LUA_USERTYPE(#ExportName, ClassName)

//like OSIMPLEEXPORT but registers a base class too - REQUIRED whenever a
//bound member (variable or function) is physically declared on that base:
//sol2 resolves base member pointers only through the registered base list.
//Base types containing commas (templates) go through a typedef at the call
//site (macro arguments cannot carry bare commas).
#define OSIMPLEEXPORT_BASED(ClassName,BaseClassName,ExportName)												\
	{																										\
		ORKIGE_LUA_USERTYPE_BASED(#ExportName, BaseClassName, ClassName)

#define OSIMPLEEXPORT_END																					\
	}

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
//static functions stay unregistered (like both historical backends; some
//call sites name overloaded statics that cannot be resolved generically)
#define OSTATICFUNC(FunctionName)
//standard function
#define OSTATICFUNCR(FunctionName,callPolicy)
//standard function
#define OSTATICFUNCR2(FunctionName,cp_pt1,cp_pt2)

//standard function
#define OFUNC(FunctionName)									py_class[#FunctionName] = &ExposedClassType::FunctionName;

namespace Orkige
{
	namespace MetaLuaDetail
	{
		//! @brief wrap a woptr-returning member function for OFUNCWEAK: Lua
		//! receives the locked optr (nil when expired) - sol2 understands
		//! std::shared_ptr natively but cannot push a std::weak_ptr
		template<typename ResultType, typename ClassType, typename... ArgTypes>
		inline auto lockedResult(ResultType (ClassType::*function)(ArgTypes...))
		{
			return [function](ClassType & object, ArgTypes... args)
			{
				return (object.*function)(static_cast<ArgTypes>(args)...).lock();
			};
		}
	}
}

//member function returning a woptr - registered so Lua gets the locked optr
#define OFUNCWEAK(FunctionName)								py_class[#FunctionName] = Orkige::MetaLuaDetail::lockedResult(&ExposedClassType::FunctionName);

#define OFUNCOVERL(FunctionName, CCast)						py_class[#FunctionName] = static_cast<CCast>(&ExposedClassType::FunctionName);

//standard function
#define OVIRTUAL_FUNC(FunctionName)							OFUNC(FunctionName)

#define OVIRTUAL_NOTWRAPPED_FUNC(FunctionName)				OFUNC(FunctionName)

#define OVIRTUAL_NOTWRAPPED_FUNCR(FunctionName,callPolicy)	OFUNC(FunctionName)

#define OVIRTUAL_NOTWRAPPED_FUNCCR(FunctionName)			OFUNC(FunctionName)
//function with other name in lua than in c++
#define OFUNC_REN(FunctionName,PythonFunctionName)			py_class[#PythonFunctionName] = &ExposedClassType::FunctionName;

//function with call policy (no call policies in sol2 - plain registration)
#define OFUNCR(FunctionName,callPolicy)						OFUNC(FunctionName)

//function with internal reference call policy (no call policies in sol2)
#define OFUNCIR(FunctionName)								OFUNC(FunctionName)

//function with const reference call policy (no call policies in sol2)
#define OFUNCCR(FunctionName)								OFUNC(FunctionName)

#define OARG(arg_name)

#define ODEFARG(arg_name,arg_default_value)

#define OARGNONE(arg_name)

#define OTPLFUNCDEFARGS(TypeName,FunctionName,args)			OTPLFUNC(TypeName,FunctionName)

//template function - repeated registrations under the same name overwrite
//each other in sol2 (last one wins)
#define OTPLFUNC(TypeName,FunctionName)						py_class[#FunctionName] = &ExposedClassType::FunctionName<TypeName>;

//template function
#define OTPLFUNCR(FunctionName,TypeName,callPolicy)			OTPLFUNC(TypeName,FunctionName)

//function overloads - repeated registrations under the same name overwrite
//each other in sol2 (last one wins)
#define OFUNC_OVERL(ReturnValue,FunctionName)				{ReturnValue (ExposedClassType::*OVERLOAD_##FunctionName##_ReturnValue)() = &ExposedClassType::FunctionName;\
															py_class[#FunctionName] = OVERLOAD_##FunctionName##_ReturnValue;}

#define OFUNC_OVERL1(ReturnValue,FunctionName,ParamType1)	{ReturnValue (ExposedClassType::*OVERLOAD_##FunctionName##_ReturnValue)(ParamType1) = &ExposedClassType::FunctionName;\
															py_class[#FunctionName] = OVERLOAD_##FunctionName##_ReturnValue;}

#define OFUNC_OVERL2(ReturnValue,FunctionName,ParamType1,ParamType2)	{ReturnValue (ExposedClassType::*OVERLOAD_##FunctionName##_ReturnValue)(ParamType1,ParamType2) = &ExposedClassType::FunctionName;\
															py_class[#FunctionName] = OVERLOAD_##FunctionName##_ReturnValue;}

#define OFUNCR_OVERL(ReturnValue,FunctionName,callPolicy)	OFUNC_OVERL(ReturnValue,FunctionName)
#define OFUNCCR_OVERL(ReturnValue,FunctionName)				OFUNC_OVERL(ReturnValue,FunctionName)

//property (read-only when only a getter exists)
#define OPROP(PropertyName,FunctionName)					py_class[PropertyName] = sol::property(&ExposedClassType::FunctionName);

//! reflected property - DUAL emission: the neutral schema registration (so the
//! registry populates in every config, incl. ORKIGE_SCRIPTING=OFF) AND a sol2
//! read/write property (so the Lua surface gains the field). Coexists with the
//! existing OVAR/OPROP/OFUNC registrations - it neither removes nor shadows the
//! method bindings a component already exposes (a NEW property key is added).
#define OPROPERTY(PropName,Kind,Getter,Setter,Flags)						\
	OPROPERTY_REGISTER(PropName,Kind,Getter,Setter,Flags)					\
	py_class[PropName] = sol::property(&ExposedClassType::Getter,			\
		&ExposedClassType::Setter);
//! reflected READ-ONLY property - DUAL emission: the neutral registry (getter
//! only, no setter => read-only descriptor) AND a getter-only sol2 property.
#define OPROPERTY_RO(PropName,Kind,Getter,Flags)							\
	OPROPERTY_READONLY_REGISTER(PropName,Kind,Getter,Flags)				\
	py_class[PropName] = sol::property(&ExposedClassType::Getter);
#define OPROPERTY_META(PropName,Kind,Getter,Setter,Flags,MetaExpr)			\
	OPROPERTY_REGISTER_META(PropName,Kind,Getter,Setter,Flags,MetaExpr)		\
	py_class[PropName] = sol::property(&ExposedClassType::Getter,			\
		&ExposedClassType::Setter);
#define OPROPERTY_ENUM(PropName,EnumTypeName,Getter,Setter,Flags)			\
	OPROPERTY_ENUM_REGISTER(PropName,EnumTypeName,Getter,Setter,Flags)		\
	py_class[PropName] = sol::property(&ExposedClassType::Getter,			\
		&ExposedClassType::Setter);
#define OPROPERTY_REF(PropName,RefKind,Hint,Getter,Setter,Flags)			\
	OPROPERTY_REF_REGISTER(PropName,RefKind,Hint,Getter,Setter,Flags)		\
	py_class[PropName] = sol::property(&ExposedClassType::Getter,			\
		&ExposedClassType::Setter);

//member variable
#define OVAR(VariableName)									py_class[#VariableName] = &ExposedClassType::VariableName;

//member variable exposed read-only
#define OSTATICVAR(VariableName)							py_class[#VariableName] = sol::readonly(&ExposedClassType::VariableName);

//member variable exposed read-only
#define OCONSTVAR(VariableName)								py_class[#VariableName] = sol::readonly(&ExposedClassType::VariableName);

//default constructor
#define OCONSTRUCTOR0()										{																			\
																auto orkigeLuaFactory = []()											\
																{ return std::shared_ptr<ExposedClassType>(new ExposedClassType()); };	\
																ORKIGE_LUA_REGISTER_CONSTRUCTOR("new0")									\
															}

//constructor with 1 parameter
#define OCONSTRUCTOR1(param1)								{																			\
																auto orkigeLuaFactory = [](param1 orkigeArg1)							\
																{ return std::shared_ptr<ExposedClassType>(new ExposedClassType(		\
																	orkigeArg1)); };													\
																ORKIGE_LUA_REGISTER_CONSTRUCTOR("new1")									\
															}

#define OCONSTRUCTOR2(param1,param2)						{																			\
																auto orkigeLuaFactory = [](param1 orkigeArg1, param2 orkigeArg2)		\
																{ return std::shared_ptr<ExposedClassType>(new ExposedClassType(		\
																	orkigeArg1, orkigeArg2)); };										\
																ORKIGE_LUA_REGISTER_CONSTRUCTOR("new2")									\
															}

#define OCONSTRUCTOR3(param1,param2,param3)					{																			\
																auto orkigeLuaFactory = [](param1 orkigeArg1, param2 orkigeArg2,		\
																	param3 orkigeArg3)													\
																{ return std::shared_ptr<ExposedClassType>(new ExposedClassType(		\
																	orkigeArg1, orkigeArg2, orkigeArg3)); };							\
																ORKIGE_LUA_REGISTER_CONSTRUCTOR("new3")									\
															}

//! singleton-aware 3-parameter constructor: a script constructing a class
//! whose ONE live instance still exists gets an honest Lua error (catchable
//! with pcall) instead of the Singleton base's process abort - the previous
//! scene's instance may be kept alive by outstanding references, and a script
//! must never be able to bring the whole engine down. The error points at
//! getSingleton() as the reuse path.
#define OSINGLETON_CONSTRUCTOR3(param1,param2,param3)		{																					\
																auto orkigeLuaFactory = [](param1 orkigeArg1, param2 orkigeArg2,		\
																	param3 orkigeArg3)													\
																{																						\
																	if (ExposedClassType::getSingletonPtr())							\
																		throw std::runtime_error(											\
																			"this singleton already exists - use getSingleton() "		\
																			"(one instance per run; the previous scene's "				\
																			"instance may still be alive)");								\
																	return std::shared_ptr<ExposedClassType>(new ExposedClassType(		\
																		orkigeArg1, orkigeArg2, orkigeArg3));							\
																};																					\
																ORKIGE_LUA_REGISTER_CONSTRUCTOR("new3")									\
															}

#define OCONSTRUCTOR4(param1,param2,param3,param4)			{																			\
																auto orkigeLuaFactory = [](param1 orkigeArg1, param2 orkigeArg2,		\
																	param3 orkigeArg3, param4 orkigeArg4)								\
																{ return std::shared_ptr<ExposedClassType>(new ExposedClassType(		\
																	orkigeArg1, orkigeArg2, orkigeArg3, orkigeArg4)); };				\
																ORKIGE_LUA_REGISTER_CONSTRUCTOR("new4")									\
															}

#define OCONSTRUCTOR5(param1,param2,param3,param4,param5)	{																			\
																auto orkigeLuaFactory = [](param1 orkigeArg1, param2 orkigeArg2,		\
																	param3 orkigeArg3, param4 orkigeArg4, param5 orkigeArg5)			\
																{ return std::shared_ptr<ExposedClassType>(new ExposedClassType(		\
																	orkigeArg1, orkigeArg2, orkigeArg3, orkigeArg4, orkigeArg5)); };	\
																ORKIGE_LUA_REGISTER_CONSTRUCTOR("new5")									\
															}

#define OVIRTUAL_CONSTRUCTOR0()								OCONSTRUCTOR0()

#define OVIRTUAL_CONSTRUCTOR1(param1)						OCONSTRUCTOR1(param1)

#define OVIRTUAL_CONSTRUCTOR2(param1,param2)				OCONSTRUCTOR2(param1,param2)

#define OVIRTUAL_CONSTRUCTOR3(param1,param2,param3)			OCONSTRUCTOR3(param1,param2,param3)

#define OVIRTUAL_CONSTRUCTOR4(param1,param2,param3,param4)	OCONSTRUCTOR4(param1,param2,param3,param4)

#define OVIRTUAL_CONSTRUCTOR5(p1,p2,p3,p4,p5)				OCONSTRUCTOR5(p1,p2,p3,p4,p5)

#define OPICKABLE()

//expose the singleton accessor (raw pointer - Lua never owns a singleton)
#define OSINGLETON()										py_class["getSingleton"] = []() { return &ExposedClassType::getSingleton(); };

//enums become a plain table of int values registered on the class table
#define OENUM_START(EnumName)								{																			\
																sol::table orkigeLuaEnum =												\
																	Orkige::ScriptManager::metaExportState().create_table();			\
																const char * orkigeLuaEnumName = #EnumName;

#define OENUM_VALUE(ValueName)									orkigeLuaEnum[#ValueName] = static_cast<int>(ExposedClassType::ValueName);

#define OENUM_END												py_class[orkigeLuaEnumName] = orkigeLuaEnum;							\
															}

#define OOBJECT_END																;};

#define OEXPORT(ClassName)									ClassName::OrkigeMetaExport(CURRENT_ORKIGE_MODULE_NAME);

//sol2 binds std containers natively - nothing to export
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
															const char* CURRENT_ORKIGE_MODULE_NAME = #ModuleName;\
															oDebugMsg("core",0,"*** Init Module "<<CURRENT_ORKIGE_MODULE_NAME<<"!");

#define ORKIGE_MODULE_END									};

#define OLOAD_MODULE_STATIC(ModuleName)						void init_module_##ModuleName();\
															init_module_##ModuleName()

#endif //__Meta_Lua_h__9_9_2010__18_37_39__
