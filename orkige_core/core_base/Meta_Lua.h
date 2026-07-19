/********************************************************************
	created:	Thursday 2010/09/09 at 18:37
	filename: 	Meta_Lua.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
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
#include <string>
#include <stdexcept>

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
		//! @brief the handle BASE for a returned leaf type. A woptr<Leaf>-returning
		//! accessor (OFUNCWEAK) hands Lua a LuaWeakHandle<base>. The primary maps a
		//! type to ITSELF (its own handle currency - GuiFactory, GuiToggleGroup,
		//! GameObject); the engine specialises every GuiWidget descendant to
		//! GuiWidget so ALL widget finders/create* share the ONE WidgetHandle (see
		//! engine_gui/GuiWidgetHandle.h). Must be visible, consistently, in every
		//! TU that registers a woptr<Leaf>-returning accessor for that leaf. The
		//! second parameter is an SFINAE slot for the engine's OHANDLE_BASE
		//! family-mapping specialisation (a whole subtree -> one handle base).
		template<typename T, typename = void>
		struct LuaHandleBase { using type = T; };

		//--- weak Lua handles (option C) --------------------------------------
		//! @brief a WEAK Lua-side proxy to an engine object. Lua NEVER owns the
		//! object: the handle holds a weak_ptr to the BASE type plus the leaf kind
		//! name + id captured at creation. Every bound method locks for the call
		//! and raises an honest (pcall-catchable) script error when the object is
		//! gone - a mistake is visible at the line that made it, with no zombie
		//! extending the object's lifetime and no silent no-op, safe under every
		//! destruction order. ONE handle type carries a whole surface; a leaf
		//! method dynamic_casts the locked base to its leaf.
		template<typename Base>
		struct LuaWeakHandle
		{
			std::weak_ptr<Base> weak;
			std::string kind;	//!< dynamic type name at creation (error text + cast target)
			std::string id;		//!< object id, for the error message
		};

		//! @brief lock a handle for a call or raise a dead-handle error. noun is
		//! the surface's word ("widget handle" / "handle") so the message reads
		//! e.g. "widget handle is dead (GuiLabel 'coinLabel')".
		template<typename Base>
		inline std::shared_ptr<Base> lockHandle(LuaWeakHandle<Base> const & handle, char const * noun)
		{
			std::shared_ptr<Base> locked = handle.weak.lock();
			if (!locked)
			{
				std::string message = noun;
				message += " is dead";
				if (!handle.kind.empty())
				{
					message += " (";
					message += handle.kind;
					if (!handle.id.empty())
					{
						message += " '";
						message += handle.id;
						message += "'";
					}
					message += ")";
				}
				throw std::runtime_error(message);
			}
			return locked;
		}

		//! @brief narrow a locked base pointer to a leaf, or raise a DISTINCT
		//! wrong-leaf error ("widget 'foo' is a GuiButton, not a GuiLabel") - a
		//! live-but-wrong-type call fails honestly instead of pretending death.
		//! noun is the surface's singular ("widget").
		template<typename Leaf, typename Base>
		inline Leaf * leafOrRaise(std::shared_ptr<Base> const & locked,
			LuaWeakHandle<Base> const & handle, char const * noun)
		{
			Leaf * leaf = dynamic_cast<Leaf *>(locked.get());
			if (!leaf)
			{
				std::string message = noun;
				message += " '";
				message += handle.id;
				message += "' is a ";
				message += handle.kind;
				message += ", not a ";
				message += Leaf::getClassTypeInfo().getName();
				throw std::runtime_error(message);
			}
			return leaf;
		}

		//! detect the Object surface (getTypeInfo / getObjectID) so a handle over a
		//! non-Object base (GuiFactory, GuiToggleGroup) still compiles - it just
		//! carries no dynamic kind / id for the error text.
		template<typename T, typename = void> struct HasTypeInfo : std::false_type {};
		template<typename T> struct HasTypeInfo<T, std::void_t<decltype(std::declval<T &>().getTypeInfo())>> : std::true_type {};
		template<typename T, typename = void> struct HasObjectID : std::false_type {};
		template<typename T> struct HasObjectID<T, std::void_t<decltype(std::declval<T &>().getObjectID())>> : std::true_type {};
		//! detect a component that carries no id of its own but knows its owning
		//! GameObject (getGameObject()): the handle then borrows the OWNER's id, so
		//! a component's dead-handle text names the object it belonged to
		//! ("component handle is dead (TransformComponent 'hero')").
		template<typename T, typename = void> struct HasGameObjectOwner : std::false_type {};
		template<typename T> struct HasGameObjectOwner<T, std::void_t<decltype(std::declval<T &>().getGameObject()->getObjectID())>> : std::true_type {};

		//! @brief construct a handle-or-nil from a woptr<Leaf>: nil when empty
		//! (absent / wrong type - preserves the typed-finder contract), else a weak
		//! handle over the deduced base, capturing the leaf's DYNAMIC kind + id now
		//! (blank for a non-Object base - its dead-handle text is just the noun).
		template<typename Leaf>
		inline sol::optional<LuaWeakHandle<typename LuaHandleBase<Leaf>::type>>
			makeHandle(std::weak_ptr<Leaf> const & weak)
		{
			using Base = typename LuaHandleBase<Leaf>::type;
			std::shared_ptr<Leaf> locked = weak.lock();
			if (!locked)
			{
				return sol::nullopt;
			}
			std::string kind;
			std::string id;
			if constexpr (HasTypeInfo<Leaf>::value)
			{
				kind = locked->getTypeInfo().getName();		// dynamic leaf type
			}
			if constexpr (HasGameObjectOwner<Leaf>::value)
			{
				// a component carries no meaningful id of its own (its inherited
				// Object id is blank) - borrow the OWNER's for the error text so
				// it names the object the component belonged to. Checked BEFORE
				// getObjectID so it wins for components; a GameObject has no
				// getGameObject() and falls through to its own id below. (owner
				// type deduced - core_base never names GameObject.)
				if (auto * owner = locked->getGameObject())
				{
					id = owner->getObjectID();
				}
			}
			else if constexpr (HasObjectID<Leaf>::value)
			{
				id = locked->getObjectID();					// object's own id
			}
			std::shared_ptr<Base> base = locked;	// upcast to the handle base
			return LuaWeakHandle<Base>{ std::weak_ptr<Base>(base), kind, id };
		}

		//! @brief OFUNCWEAK's registration: wrap a woptr-returning accessor so Lua
		//! receives a WEAK handle (never an owning optr). The handle base is
		//! deduced from the returned leaf via LuaHandleBase.
		template<typename Leaf, typename ClassType, typename... ArgTypes>
		inline auto handleResult(std::weak_ptr<Leaf> (ClassType::*function)(ArgTypes...))
		{
			return [function](ClassType & object, ArgTypes... args)
			{
				return makeHandle((object.*function)(static_cast<ArgTypes>(args)...));
			};
		}

		//! @brief lock-and-forward wrapper for a BASE method bound on a handle
		//! usertype: lock (raising on a dead handle), then call on the base.
		template<typename Base, typename ResultType, typename ClassType, typename... ArgTypes>
		inline auto forwardBase(char const * noun, ResultType (ClassType::*function)(ArgTypes...))
		{
			return [noun, function](LuaWeakHandle<Base> & handle, ArgTypes... args) -> ResultType
			{
				return (lockHandle(handle, noun).get()->*function)(static_cast<ArgTypes>(args)...);
			};
		}

		//! const-qualified overload of forwardBase (a base getter)
		template<typename Base, typename ResultType, typename ClassType, typename... ArgTypes>
		inline auto forwardBase(char const * noun, ResultType (ClassType::*function)(ArgTypes...) const)
		{
			return [noun, function](LuaWeakHandle<Base> & handle, ArgTypes... args) -> ResultType
			{
				return (lockHandle(handle, noun).get()->*function)(static_cast<ArgTypes>(args)...);
			};
		}

		//! @brief lock-and-forward wrapper for a LEAF method: lock, dynamic_cast
		//! to the leaf (raising the wrong-leaf error on a live widget of another
		//! type), then call.
		template<typename Base, typename Leaf, typename ResultType, typename ClassType, typename... ArgTypes>
		inline auto forwardLeaf(char const * deadNoun, char const * leafNoun,
			ResultType (ClassType::*function)(ArgTypes...))
		{
			return [deadNoun, leafNoun, function](LuaWeakHandle<Base> & handle, ArgTypes... args) -> ResultType
			{
				std::shared_ptr<Base> locked = lockHandle(handle, deadNoun);
				Leaf * leaf = leafOrRaise<Leaf>(locked, handle, leafNoun);
				return (leaf->*function)(static_cast<ArgTypes>(args)...);
			};
		}

		//! const-qualified overload of forwardLeaf (a single-leaf getter)
		template<typename Base, typename Leaf, typename ResultType, typename ClassType, typename... ArgTypes>
		inline auto forwardLeaf(char const * deadNoun, char const * leafNoun,
			ResultType (ClassType::*function)(ArgTypes...) const)
		{
			return [deadNoun, leafNoun, function](LuaWeakHandle<Base> & handle, ArgTypes... args) -> ResultType
			{
				std::shared_ptr<Base> locked = lockHandle(handle, deadNoun);
				Leaf * leaf = leafOrRaise<Leaf>(locked, handle, leafNoun);
				return (leaf->*function)(static_cast<ArgTypes>(args)...);
			};
		}

		//! @brief lock-and-forward wrapper for a base method taking ANOTHER
		//! widget as a parameter (setParent): lock self, and lock/convert the
		//! parameter handle INSIDE the wrapper (a passed-but-dead handle raises;
		//! nil detaches). This is what dissolves the setParent interim adapter - a
		//! widget-valued parameter is just a handle the wrapper locks.
		template<typename Base>
		inline auto forwardParent(char const * noun,
			void (Base::*function)(std::shared_ptr<Base> const &))
		{
			return [noun, function](LuaWeakHandle<Base> & handle,
				sol::optional<LuaWeakHandle<Base>> parent)
			{
				std::shared_ptr<Base> self = lockHandle(handle, noun);
				std::shared_ptr<Base> other;	// nil parent = detach
				if (parent)
				{
					other = lockHandle(*parent, noun);
				}
				(self.get()->*function)(other);
			};
		}

		//! @brief raise the multi-leaf wrong-kind error, naming the ACCEPTED kinds
		//! so the script author learns the contract from the error itself:
		//! "widget 'foo' is a GuiDecorWidget; setText needs GuiLabel or GuiTextEntry".
		template<typename Base>
		[[noreturn]] inline void raiseWrongKinds(LuaWeakHandle<Base> const & handle,
			char const * noun, char const * method, char const * accepted)
		{
			std::string message = noun;
			message += " '";
			message += handle.id;
			message += "' is a ";
			message += handle.kind;
			message += "; ";
			message += method;
			message += " needs ";
			message += accepted;
			throw std::runtime_error(message);
		}

		//! @brief lock-and-forward for a method whose Lua NAME is shared by TWO
		//! DISTINCT leaves that have no common base (a collision set). Tries each
		//! in turn and calls the one the live object actually is; if neither,
		//! raises naming the accepted kinds. The candidates share the Lua-facing
		//! signature (deduced from the first); the second's member pointer may be
		//! const-qualified (Fn2 is opaque). @see OWEAKHANDLE_LEAFMETHOD2.
		template<typename Base, typename Leaf1, typename Leaf2,
			typename Fn2, typename ResultType, typename... ArgTypes>
		inline auto forwardLeaf2(char const * deadNoun, char const * leafNoun,
			char const * method, char const * accepted,
			ResultType (Leaf1::*fn1)(ArgTypes...), Fn2 fn2)
		{
			return [=](LuaWeakHandle<Base> & handle, ArgTypes... args) -> ResultType
			{
				std::shared_ptr<Base> locked = lockHandle(handle, deadNoun);
				if (Leaf1 * l1 = dynamic_cast<Leaf1 *>(locked.get()))
					return (l1->*fn1)(static_cast<ArgTypes>(args)...);
				if (Leaf2 * l2 = dynamic_cast<Leaf2 *>(locked.get()))
					return (l2->*fn2)(static_cast<ArgTypes>(args)...);
				raiseWrongKinds(handle, leafNoun, method, accepted);
			};
		}

		//! @brief the three-leaf collision-set sibling of forwardLeaf2.
		template<typename Base, typename Leaf1, typename Leaf2, typename Leaf3,
			typename Fn2, typename Fn3, typename ResultType, typename... ArgTypes>
		inline auto forwardLeaf3(char const * deadNoun, char const * leafNoun,
			char const * method, char const * accepted,
			ResultType (Leaf1::*fn1)(ArgTypes...), Fn2 fn2, Fn3 fn3)
		{
			return [=](LuaWeakHandle<Base> & handle, ArgTypes... args) -> ResultType
			{
				std::shared_ptr<Base> locked = lockHandle(handle, deadNoun);
				if (Leaf1 * l1 = dynamic_cast<Leaf1 *>(locked.get()))
					return (l1->*fn1)(static_cast<ArgTypes>(args)...);
				if (Leaf2 * l2 = dynamic_cast<Leaf2 *>(locked.get()))
					return (l2->*fn2)(static_cast<ArgTypes>(args)...);
				if (Leaf3 * l3 = dynamic_cast<Leaf3 *>(locked.get()))
					return (l3->*fn3)(static_cast<ArgTypes>(args)...);
				raiseWrongKinds(handle, leafNoun, method, accepted);
			};
		}

		//! @brief lock-and-forward for a base method that itself RETURNS a
		//! woptr<Leaf2> (a factory create* on a FactoryHandle): lock self, call,
		//! and wrap the result as its own weak handle-or-nil.
		template<typename Base, typename Leaf, typename ClassType, typename... ArgTypes>
		inline auto forwardHandle(char const * noun, std::weak_ptr<Leaf> (ClassType::*function)(ArgTypes...))
		{
			return [noun, function](LuaWeakHandle<Base> & handle, ArgTypes... args)
			{
				std::shared_ptr<Base> self = lockHandle(handle, noun);
				return makeHandle((self.get()->*function)(static_cast<ArgTypes>(args)...));
			};
		}

		//! @brief lock-and-forward for a method taking ANOTHER family's handle as a
		//! parameter narrowed to a leaf (GuiToggleGroup::addMember takes a checkbox,
		//! passed as a WidgetHandle): lock self, lock the parameter handle (its base
		//! deduced via LuaHandleBase), narrow it to ParamLeaf (raising the distinct
		//! wrong-kind error), and forward the owning leaf pointer. nil parameter
		//! forwards null.
		template<typename SelfBase, typename ParamLeaf>
		inline auto forwardHandleParam(char const * selfNoun, char const * paramNoun,
			char const * paramLeafNoun, void (SelfBase::*function)(std::shared_ptr<ParamLeaf> const &))
		{
			using ParamBase = typename LuaHandleBase<ParamLeaf>::type;
			return [=](LuaWeakHandle<SelfBase> & self, sol::optional<LuaWeakHandle<ParamBase>> param)
			{
				std::shared_ptr<SelfBase> s = lockHandle(self, selfNoun);
				std::shared_ptr<ParamLeaf> leaf;
				if (param)
				{
					std::shared_ptr<ParamBase> locked = lockHandle(*param, paramNoun);
					(void)leafOrRaise<ParamLeaf>(locked, *param, paramLeafNoun);
					leaf = std::static_pointer_cast<ParamLeaf>(locked);
				}
				(s.get()->*function)(leaf);
			};
		}
	}
}

//! a woptr-returning accessor: hands Lua a WEAK handle (locks per method call,
//! raises an honest error when the object is gone) - never an owning optr. The
//! handle base is deduced from the returned leaf via LuaHandleBase (all GuiWidget
//! descendants share the ONE WidgetHandle). The name is finally true: OFUNCWEAK IS
//! the weak-handle registration, the sole accessor macro; there is no owning path.
#define OFUNCWEAK(FunctionName)								py_class[#FunctionName] = Orkige::MetaLuaDetail::handleResult(&ExposedClassType::FunctionName);

//! map a whole class SUBTREE to ONE handle base: every descendant of BaseClass
//! (and BaseClass itself) returned by an OFUNCWEAK accessor hands Lua a
//! LuaWeakHandle<BaseClass> - the shared currency for that family (all widgets ->
//! WidgetHandle). Emit ONCE at namespace scope in an engine header with BaseClass
//! complete + <type_traits> included; no-op in the noscript backend. Do NOT use a
//! raw #ifdef for this - the macro is the sanctioned seam.
#define OHANDLE_BASE(BaseClass)								namespace Orkige { namespace MetaLuaDetail {							\
																template<typename OrkigeHandleLeaf>									\
																struct LuaHandleBase<OrkigeHandleLeaf,								\
																	std::enable_if_t<std::is_base_of_v<BaseClass, OrkigeHandleLeaf>>>	\
																{ using type = BaseClass; };										\
															} }

#define OFUNCOVERL(FunctionName, CCast)						py_class[#FunctionName] = static_cast<CCast>(&ExposedClassType::FunctionName);

//! bind a custom callable (typically a lambda adapting a signature the
//! script layer cannot pass directly) under the given script-facing name.
//! Variadic so the callable may contain commas.
#define OFUNC_CUSTOM(FunctionName, ...)						py_class[#FunctionName] = __VA_ARGS__;

//--- weak Lua handle macros (option C) --------------------------------------
//! open a weak-handle usertype for BaseClass under the Lua name LuaName; the
//! enclosed OWEAKHANDLE_* entries bind lock-and-forward wrappers on it.
//! DeadNoun / LeafNoun are the surface's error vocabulary.
#define OWEAKHANDLE_BEGIN(BaseClass, LuaName, DeadNoun, LeafNoun)	{								\
			typedef BaseClass WeakHandleBase;															\
			[[maybe_unused]] char const * whDeadNoun = DeadNoun;										\
			[[maybe_unused]] char const * whLeafNoun = LeafNoun;										\
			[[maybe_unused]] sol::usertype<Orkige::MetaLuaDetail::LuaWeakHandle<BaseClass>> wh_class =	\
				Orkige::ScriptManager::metaExportState()												\
					.new_usertype<Orkige::MetaLuaDetail::LuaWeakHandle<BaseClass>>(					\
						LuaName, sol::no_constructor);

//! bind an inherited/base method (member of the handle base) as lock-and-forward
#define OWEAKHANDLE_BASEMETHOD(Method)						wh_class[#Method] = Orkige::MetaLuaDetail::forwardBase<WeakHandleBase>(whDeadNoun, &WeakHandleBase::Method);

//! bind an OVERLOADED base method, disambiguated by the member-pointer cast type
//! CCast (the handle-surface counterpart of OFUNCOVERL) as lock-and-forward
#define OWEAKHANDLE_BASEMETHOD_OVERL(Method, CCast)			wh_class[#Method] = Orkige::MetaLuaDetail::forwardBase<WeakHandleBase>(whDeadNoun, static_cast<CCast>(&WeakHandleBase::Method));

//! bind a READ-ONLY property (Lua field syntax obj.PropName) whose getter is the
//! base Getter, locked per read (the handle-surface counterpart of OPROP). Keeps
//! a script's `obj.id` field access working across the raw-pointer -> handle flip.
#define OWEAKHANDLE_PROPERTY_RO(PropName, Getter)			wh_class[PropName] = sol::property(Orkige::MetaLuaDetail::forwardBase<WeakHandleBase>(whDeadNoun, &WeakHandleBase::Getter));

//! bind a CUSTOM callable (a lambda taking LuaWeakHandle<WeakHandleBase>&) as a
//! handle method - the escape hatch (the handle-surface counterpart of
//! OFUNC_CUSTOM) for a method whose result is neither a plain value nor a
//! standard handle: e.g. getLayer, which locks the widget then hands back a
//! view-keyed GuiLayerHandle. Variadic so the lambda may contain commas.
#define OWEAKHANDLE_CUSTOM(Method, ...)						wh_class[#Method] = __VA_ARGS__;

//! bind a LEAF method (member of LeafClass): locks then dynamic_casts, with a
//! distinct wrong-leaf error on a live widget of another type
#define OWEAKHANDLE_LEAFMETHOD(LeafClass, Method)			wh_class[#Method] = Orkige::MetaLuaDetail::forwardLeaf<WeakHandleBase, LeafClass>(whDeadNoun, whLeafNoun, &LeafClass::Method);

//! bind a base method taking another handle as a PARAMETER (setParent): both
//! self and the parameter handle are locked/converted inside the wrapper
#define OWEAKHANDLE_PARENTMETHOD(Method)					wh_class[#Method] = Orkige::MetaLuaDetail::forwardParent<WeakHandleBase>(whDeadNoun, &WeakHandleBase::Method);

//! COLLISION SET: one Lua method NAME implemented by SEVERAL distinct leaves that
//! share no common base (e.g. setText on GuiLabel and GuiTextEntry). One usertype
//! binds one function per name, so the candidates are listed HERE, declaratively,
//! and the wrapper tries each in turn (a live wrong-kind object raises naming the
//! accepted kinds). MAINTENANCE RULE: a new widget type that adds a method name
//! ALREADY in a collision set below MUST join that set's registration here - grep
//! OWEAKHANDLE_LEAFMETHOD2 / _LEAFMETHOD3. (A name on a single leaf stays
//! OWEAKHANDLE_LEAFMETHOD; a virtual override of a base method - TextEntry's
//! setPosition etc. - stays a single OWEAKHANDLE_BASEMETHOD, the vtable dispatches.)
#define OWEAKHANDLE_LEAFMETHOD2(Method, Leaf1, Leaf2)		wh_class[#Method] = Orkige::MetaLuaDetail::forwardLeaf2<WeakHandleBase, Leaf1, Leaf2>(whDeadNoun, whLeafNoun, #Method, #Leaf1 " or " #Leaf2, &Leaf1::Method, &Leaf2::Method);

#define OWEAKHANDLE_LEAFMETHOD3(Method, Leaf1, Leaf2, Leaf3)	wh_class[#Method] = Orkige::MetaLuaDetail::forwardLeaf3<WeakHandleBase, Leaf1, Leaf2, Leaf3>(whDeadNoun, whLeafNoun, #Method, #Leaf1 ", " #Leaf2 " or " #Leaf3, &Leaf1::Method, &Leaf2::Method, &Leaf3::Method);

//! bind a base method that itself RETURNS a woptr (a FactoryHandle's create*):
//! locks self, calls, and hands Lua the result as its OWN weak handle-or-nil
#define OWEAKHANDLE_HANDLEMETHOD(Method)					wh_class[#Method] = Orkige::MetaLuaDetail::forwardHandle<WeakHandleBase>(whDeadNoun, &WeakHandleBase::Method);

//! bind a method taking ANOTHER family's handle narrowed to a widget leaf as its
//! parameter (GuiToggleGroup::addMember takes a checkbox WidgetHandle): the
//! parameter is locked + narrowed to ParamLeaf inside the wrapper
#define OWEAKHANDLE_WIDGETPARAM(Method, ParamLeaf)			wh_class[#Method] = Orkige::MetaLuaDetail::forwardHandleParam<WeakHandleBase, ParamLeaf>(whDeadNoun, "widget handle", "widget", &WeakHandleBase::Method);

#define OWEAKHANDLE_END										}

//! declare this component KIND's SCRIPT access in ONE line at its
//! OrkigeMetaExport site (beside its OWEAKHANDLE block): the script vocabulary
//! name (self.<ScriptName> + self:getComponent("<ScriptName>") +
//! world.getComponent(id,"<ScriptName>")), whether populateSelfTable injects
//! self.<ScriptName>, and the optional legacy world.<WorldAccessor>(id)
//! convenience accessor ("" for none). It registers a type-erased weak-handle
//! thunk built HERE where OSelf is the complete component type, so the
//! ScriptComponent registry drives ALL script-facing component access with no
//! hand-wired per-type block. @see core_script/ScriptRuntime.h - the call site
//! must include it (for ScriptComponentAccess/ScriptRuntime) and
//! core_game/GameObject.h (for the owner's hasComponent/getComponent).
#define OSCRIPT_HANDLE(ScriptName, InjectSelf, WorldAccessor)						\
	{																				\
		Orkige::ScriptComponentAccess orkigeScriptAccess;							\
		orkigeScriptAccess.name = ScriptName;										\
		orkigeScriptAccess.injectSelf = (InjectSelf);								\
		orkigeScriptAccess.worldAccessor = WorldAccessor;							\
		orkigeScriptAccess.type = &OSelf::getClassTypeInfo();						\
		orkigeScriptAccess.injectHandle = [](Orkige::GameObject & orkigeOwner,		\
			Orkige::ScriptInstance & orkigeInstance, char const * orkigeKey)		\
		{																			\
			if (orkigeOwner.hasComponent<OSelf>())									\
				orkigeInstance.setSelfHandle(orkigeKey,								\
					orkigeOwner.getComponent<OSelf>());								\
		};																			\
		orkigeScriptAccess.makeHandleFor = [](sol::state_view orkigeLua,			\
			Orkige::GameObject & orkigeOwner) -> sol::object						\
		{																			\
			/* the typed getComponent<OSelf>() asserts on an absent component, */	\
			/* so the has-component guard IS the absent/present decision (nil) */	\
			if (!orkigeOwner.hasComponent<OSelf>())									\
				return sol::object(orkigeLua, sol::in_place, sol::lua_nil);		\
			return sol::make_object(orkigeLua,										\
				Orkige::MetaLuaDetail::makeHandle(									\
					orkigeOwner.getComponent<OSelf>()));							\
		};																			\
		Orkige::ScriptRuntime::registerComponentAccess(								\
			std::move(orkigeScriptAccess));											\
	}

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
