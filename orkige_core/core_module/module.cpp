/**************************************************************
	created:	2010/09/08 at 10:21
	filename: 	module.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/

#include "core_base/Interface.h"
#include "core_base/Object.h"
#include "core_base/Value.h"
#include "core_serialization/ISerializeable.h"
#include "core_serialization/XMLArchive.h"
#include "core_event/Event.h"
#include "core_event/EventType.h"

#include "core_event/EventManager.h"
#include "core_event/EventListener.h"
#include "core_event/GlobalEventManager.h"
#ifndef ORKIGE_NOSCRIPT
#include "core_python/PythonFunction.hpp"
#endif

#include "core_game/GameObject.h"
#include "core_game/GameObjectComponent.h"
#include "core_game/GameObjectManager.h"
#include "core_game/GameState.h"
#include "core_game/GameStateManager.h"

using namespace Orkige;

ORKIGE_MODULE(orkige_core)
#ifndef ORKIGE_LUA
#ifndef ORKIGE_NOSCRIPT
VLDDisable ();
#endif
#endif
	OEXPORT(TypeInfo)
	OEXPORT(Interface)
	OEXPORT(ISerializeable)
	OEXPORT(IArchive)
	OEXPORT(ObjectAttributeHolder)
	OEXPORT(Object)
	OEXPORT(ObjectAttributeHolder::AttributeWrapper<int>)
	OEXPORT(ObjectAttributeHolder::AttributeWrapper<long>)
	OEXPORT(ObjectAttributeHolder::AttributeWrapper<uint>)
	OEXPORT(ObjectAttributeHolder::AttributeWrapper<float>)
	OEXPORT(ObjectAttributeHolder::AttributeWrapper<double>)
	OEXPORT(ObjectAttributeHolder::AttributeWrapper<bool>)
	OEXPORT(ObjectAttributeHolder::AttributeWrapper<String>)
	OEXPORT(Value<int>)
	OEXPORT(Value<unsigned int>)
	OEXPORT(Value<unsigned long>)
	OEXPORT(Value<long>)
	OEXPORT(Value<bool>)
	OEXPORT(Value<float>)
	OEXPORT(Value<double>)
	OEXPORT(Value<String>)
	OEXPORT(XMLArchive)
	OEXPORTMAPTYPE(ObjectMap)
	OEXPORTLISTTYPE(StringList)
	OEXPORTLIST(ObjectPtrList,Object*)
#ifndef ORKIGE_LUA
#ifndef ORKIGE_NOSCRIPT
	//event
	Orkige::function::export_function< Orkige::EventHandlerFunction >( "Function" ) ;
#endif
#endif
	OEXPORT(Event)
	OEXPORT(EventType)
	OEXPORT(EventManager)
	OEXPORT(GlobalEventManager)
#ifndef ORKIGE_LUA
#ifndef ORKIGE_NOSCRIPT
	Orkige::function::register_pyobject_to_function< Orkige::EventHandlerFunction >() ;
#endif
#endif

	OEXPORT(Component<Orkige::GameObject>)
	OEXPORT(GameObjectComponent)
	OEXPORT(ComponentHolder<Orkige::GameObjectComponent>)
	OEXPORTMAP(GameObjectComponentMap,String,optr<ComponentHolder<Orkige::GameObjectComponent>::OwnedComponentType>)
	OEXPORT(GameObject)
	OEXPORT(GameObjectManager)
	OEXPORT(GameState)
	OEXPORT(GameStateManager)
#ifndef ORKIGE_LUA
#ifndef ORKIGE_NOSCRIPT
VLDEnable ();
#endif
#endif
ORKIGE_MODULE_END
