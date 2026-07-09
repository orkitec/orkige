/********************************************************************
	created:	Monday 2010/08/09 at 18:48
	filename: 	TypeManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __TypeManager_h__9_8_2010__18_48_23__
#define __TypeManager_h__9_8_2010__18_48_23__

#include "core_debug/MemoryManager.h"
#include "core_util/Singleton.h"
#include "core_util/ObjectFactory.h"
#include "core_base/TypeInfo.h"
#include "core_base/PropertySchema.h"
#include "core_util/String.h"
#include "core_module/OrkigePrerequisites.h"

#include <map>

namespace Orkige
{
	class Interface;

	class TypeManager;

	//! factory for creating Objects derived from Interface
	typedef ObjectFactory<Interface * (), String> InterfaceTypeFactory;

	//! @brief can register and create all types Derived from Interface with default constructor
	//! @see InterfaceTypeFactory
	//! @remarks TypeManager is
	//! ALSO the owner of the neutral property registry: a per-type
	//! PropertySchema keyed by TypeId plus an enum value<->label registry keyed
	//! by enum-type name. Both are populated by the OPROPERTY* / OENUM_REGISTER
	//! macros in EVERY scripting config (they are sol2-independent), so a
	//! component's declared schema is queryable even in ORKIGE_SCRIPTING=OFF.
	class ORKIGE_CORE_DLL TypeManager : public InterfaceTypeFactory, public Singleton<TypeManager>
	{
		DECL_OSINGLETON(TypeManager);
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		//! per-type declared property schemas (the static reflection half)
		std::map<TypeInfo::TypeId, PropertySchema>	mSchemas;
		//! enum value<->label tables keyed by enum-type name
		std::map<String, EnumInfo>					mEnums;
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		TypeManager();
		//! destructor
		virtual ~TypeManager();

		//--- property registry (reflection substrate) --------------
		//! @brief register (or, by name, replace) a reflected property on the
		//! type identified by typeId. Idempotent per (type,name), so re-running a
		//! type's OrkigeMetaExport does not duplicate. Called by the OPROPERTY*
		//! macros in every scripting config.
		void registerProperty(TypeInfo::TypeId typeId, PropertyDesc const & desc);
		//! the declared property schema of a type, or NULL when the type declared
		//! none
		PropertySchema const * getPropertySchema(TypeInfo::TypeId typeId) const;

		//--- enum registry -----------------------------------------
		//! @brief get (creating on first touch) the EnumInfo for an enum-type
		//! name so the OENUM_REGISTER macros can add its values
		EnumInfo & registerEnum(String const & enumTypeName);
		//! the value<->label table for an enum-type name, or NULL when unknown
		EnumInfo const * findEnum(String const & enumTypeName) const;
	protected:
	private:
	};
	//---------------------------------------------------------------
}

#endif //__TypeManager_h__9_8_2010__18_48_23__
