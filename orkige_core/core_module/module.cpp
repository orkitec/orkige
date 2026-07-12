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
#include "core_game/LevelComponent.h"
#include "core_game/TileComponent.h"
#include "core_game/LevelManager.h"

#include "core_tween/TweenManager.h"

#include "core_script/ScriptEventBus.h"

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

	// the tile-slide level tier: the data-only LevelComponent (grid
	// geometry the game snaps tiles into, its LevelGrid math re-exposed to
	// Lua) and the thin TileComponent marker; both are plain GameObject
	// components registered like the rest.
	OEXPORT(LevelComponent)
	OEXPORT(TileComponent)

	// the runtime level director: sequence + current index + the
	// deferred scene-load request + progression save, reached from Lua as
	// LevelManager.getSingleton(). Like InputActions it is a plain Singleton
	// (not an OOBJECT), so its Lua face is spelled out here. Honest no-op in
	// the editor, where no LevelManager exists.
	OSIMPLEEXPORT(Orkige::LevelManager,LevelManager)
		OSINGLETON()
		OFUNC(count)			// count()            - number of levels
		OFUNC(currentIndex)		// currentIndex()     - the live level index
		OFUNC(levelName)		// levelName(i)       - display name
		OFUNC(levelPar)			// levelPar(i)        - par slide count
		OFUNC(levelScene)		// levelScene(i)      - project-relative scene path
		OFUNC(hasNext)			// hasNext()          - is there a level after the current
		OFUNC(loadLevel)		// loadLevel(i)       - request the deferred load of level i
		OFUNC(loadScenePath)	// loadScenePath(path)- deferred load of any scene path
		OFUNC(resumeLevel)		// resumeLevel()      - persisted resume index
		OFUNC(setResumeLevel)	// setResumeLevel(i)
		OFUNC(bestMoves)		// bestMoves(i)       - fewest recorded slides (-1 = none)
		OFUNC(recordBestMoves)	// recordBestMoves(i, moves)
		OFUNC(saveProgress)		// saveProgress()     - write the save file
	OSIMPLEEXPORT_END

	//the value handle the tween Lua API returns (core_tween/TweenManager.h);
	//the tween functions themselves are registered through the ScriptRuntime
	//seam in engine_gocomponent/ScriptComponent.cpp (ensureScriptApi)
	OSIMPLEEXPORT(Orkige::TweenHandle,TweenHandle)
		OFUNC(cancel)
		OFUNC(isActive)
		// make the tween loop: setLoops(count, pingpong) - count total plays
		// (<0 = forever), pingpong true runs it back and forth
		OFUNC(setLoops)
	OSIMPLEEXPORT_END

	//the value handle the events.subscribe Lua API returns
	//(core_script/ScriptEventBus.h); events.subscribe/emit themselves are
	//registered through the ScriptRuntime seam in
	//engine_gocomponent/ScriptComponent.cpp (ensureScriptApi)
	OSIMPLEEXPORT(Orkige::EventSubscription,EventSubscription)
		// drop the subscription: cancel() -> true when it was still live
		OFUNC(cancel)
		// is the subscription still live: isActive()
		OFUNC(isActive)
	OSIMPLEEXPORT_END
ORKIGE_MODULE_END
