/**************************************************************
	created:	2010/08/19 at 23:26
	filename: 	ModuleManagerFactory.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_util/ModuleManagerFactory.h"
#include "core_event/GlobalEventManager.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	ModuleManagerFactory::ModuleManagerFactory(EventType const & initEvent, EventType const & deInitEvent, String const & _managerTypeName, String const & moduleName)
		: managerTypeName(_managerTypeName), currentModuleName(moduleName)
	{
		this->manager = oNULL(Interface);
		GlobalEventManager::getSingleton().bind(initEvent,	&ModuleManagerFactory::onInitEvent,	this);
		GlobalEventManager::getSingleton().bind(deInitEvent,&ModuleManagerFactory::onDeInitEvent,	this);
	}
	//---------------------------------------------------------
	ModuleManagerFactory::~ModuleManagerFactory()
	{
		this->manager = oNULL(Interface);
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	bool ModuleManagerFactory::onInitEvent(Event const & event)
	{
		this->manager = onew(TypeManager::getSingleton().create(this->managerTypeName));
		return false;
	}
	//---------------------------------------------------------
	bool ModuleManagerFactory::onDeInitEvent(Event const & event)
	{
		this->manager = oNULL(Interface);
		return false;
	}
}
