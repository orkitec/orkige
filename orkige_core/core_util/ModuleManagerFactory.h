/**************************************************************
	created:	2010/08/19 at 23:24
	filename: 	ModuleManagerFactory.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __ModuleManagerFactory_h__19_8_2010__23_24_25__
#define __ModuleManagerFactory_h__19_8_2010__23_24_25__

#include "core_event/Event.h"

namespace Orkige
{
	//! tiny helper class for creating and destroying Managers in Modules(Plugins etc.) if specific Events occour
	//! @note creatible Managers must at least Derive from Interface, should probably be Singletons of some type and should have a Default Constructor
	class ORKIGE_CORE_DLL ModuleManagerFactory
	{
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		optr<Interface> manager;
		const String managerTypeName;
		const String currentModuleName;
		//--- Methods -----------------------------------------
	public:
		//! constructor
		explicit ModuleManagerFactory(EventType const & initEvent, EventType const & deInitEvent, String const & _managerTypeName, String const & moduleName);
		//! destructor
		~ModuleManagerFactory();
	protected:
	private:
		bool onInitEvent(Event const & event);
		bool onDeInitEvent(Event const & event);
	};
	//---------------------------------------------------------
}

#define OMODULEMANAGER(InitEventType, DeinitEventType)	static ::Orkige::ModuleManagerFactory moduleManagerFactory(InitEventType, DeinitEventType, ExposedClassType::getClassTypeInfo().getName(), currentOrkigeModuleName);

//! call this in the OOBJECT_IMPL block of the manager that should get created on engine initialization
//! and get destroyed on engine de-initialization Events
#define OENGINEMANAGER()							OMODULEMANAGER(EventType("InitEngine"), EventType("DeInitEngine"))

#endif //__ModuleManagerFactory_h__19_8_2010__23_24_25__
