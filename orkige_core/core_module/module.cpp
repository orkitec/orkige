/**************************************************************
	created:	2010/09/08 at 10:21
	filename: 	module.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
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

#include "core_game/GameObject.h"
#include "core_game/GameObjectComponent.h"
#include "core_game/GameObjectManager.h"
#include "core_game/GameState.h"
#include "core_game/GameStateManager.h"

using namespace Orkige;

ORKIGE_MODULE(orkige_core)
	OEXPORT(TypeInfo)
	OEXPORT(Interface)
	OEXPORT(ISerializeable)
	OEXPORT(IArchive)
	OEXPORT(ObjectAttributeHolder)
	OEXPORT(Object)
	OEXPORT(ObjectAttributeHolder::AttributeWrapper<int>)
	OEXPORT(ObjectAttributeHolder::AttributeWrapper<long>)
OEXPORT(ObjectAttributeHolder::AttributeWrapper< ::Orkige::uint >)
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
	OEXPORT(Event)
	OEXPORT(EventType)
	OEXPORT(EventManager)
	OEXPORT(GlobalEventManager)

	OEXPORT(Component<Orkige::GameObject>)
	OEXPORT(GameObjectComponent)
	OEXPORT(ComponentHolder<Orkige::GameObjectComponent>)
	OEXPORTMAP(GameObjectComponentMap,String,optr<ComponentHolder<Orkige::GameObjectComponent>::OwnedComponentType>)
	OEXPORT(GameObject)
	OEXPORT(GameObjectManager)
	OEXPORT(GameState)
	OEXPORT(GameStateManager)
ORKIGE_MODULE_END
