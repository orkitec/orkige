/********************************************************************
	created:	Monday 2010/08/09 at 18:48
	filename: 	TypeManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/
#ifndef __TypeManager_h__9_8_2010__18_48_23__
#define __TypeManager_h__9_8_2010__18_48_23__

#include "core_debug/MemoryManager.h"
#include "core_util/Singleton.h"
#include "core_util/ObjectFactory.h"
#include "core_base/TypeInfo.h"
#include "core_util/String.h"
#include "core_module/OrkigePrerequisites.h"

namespace Orkige
{
	class Interface;

	class TypeManager;

	//! factory for creating Objects derived from Interface
	typedef ObjectFactory<Interface * (), String> InterfaceTypeFactory;

	//! @brief can register and create all types Derived from Interface with default constructor
	//! @see InterfaceTypeFactory
	class ORKIGE_DLL TypeManager : public InterfaceTypeFactory, public Singleton<TypeManager>
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
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		TypeManager();
		//! destructor
		virtual ~TypeManager();
	protected:
	private:
	};
	//---------------------------------------------------------------
}

#endif //__TypeManager_h__9_8_2010__18_48_23__
