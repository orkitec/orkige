# ---------------------------------------------------------
cmake_minimum_required(VERSION 2.6)

# ---------------------------------------------------------
# core_base Files
# ---------------------------------------------------------

set(core_base_SOURCE
    # ----- Source -----
    core_base/Interface.cpp
    core_base/Object.cpp
    core_base/TypeInfo.cpp
    core_base/TypeManager.cpp
    core_base/Value.cpp
)

set(core_base_HEADER
    # ----- Headers -----
    core_base/Interface.h
    core_base/Meta.h
    core_base/Meta_Lua.h
    core_base/Meta_None.h
    core_base/Meta_Python.h
    core_base/Object.h
    core_base/TypeInfo.h
    core_base/TypeManager.h
    core_base/Value.h
)

source_group(core_base FILES ${core_base_HEADER} ${core_base_SOURCE})

# ---------------------------------------------------------

set(core_debug_SOURCE
    # ----- Source -----
    core_debug/LogChannel.cpp
    core_debug/LogConfig.cpp
    core_debug/LogManager.cpp
    core_debug/MemoryManager.cpp
    core_debug/ProfileManager.cpp
    core_debug/ProfileNode.cpp
)

set(core_debug_HEADER
    # ----- Headers -----
    core_debug/DebugMacros.h
    core_debug/DisableMemoryManager.h
    core_debug/EnableMemoryManager.h
    core_debug/LogChannel.h
    core_debug/LogConfig.h
    core_debug/LogManager.h
    core_debug/MemoryManager.h
    core_debug/Profile.h
    core_debug/ProfileManager.h
    core_debug/ProfileNode.h
)

source_group(core_debug FILES ${core_debug_HEADER} ${core_debug_SOURCE})

# ---------------------------------------------------------

set(core_event_SOURCE
    # ----- Source -----
    core_event/Event.cpp
    core_event/EventHandler.cpp
    core_event/EventListener.cpp
    core_event/EventManager.cpp
    core_event/EventType.cpp
    core_event/GlobalEventManager.cpp
)

set(core_event_HEADER
    # ----- Headers -----
    core_event/Event.h
    core_event/EventHandler.h
    core_event/EventListener.h
    core_event/EventManager.h
    core_event/EventType.h
    core_event/GlobalEventManager.h
)

source_group(core_event FILES ${core_event_HEADER} ${core_event_SOURCE})

# ---------------------------------------------------------

set(core_game_SOURCE
    # ----- Source -----
    core_game/Application.cpp
    core_game/GameObject.cpp
    core_game/GameObjectComponent.cpp
    core_game/GameObjectManager.cpp
    core_game/GameState.cpp
    core_game/GameStateManager.cpp
)

set(core_game_HEADER
    # ----- Headers -----
    core_game/Application.h
    core_game/GameObject.h
    core_game/GameObjectComponent.h
    core_game/GameObjectManager.h
    core_game/GameState.h
    core_game/GameStateManager.h
)

source_group(core_game FILES ${core_game_HEADER} ${core_game_SOURCE})

# ---------------------------------------------------------

set(core_module_SOURCE
    # ----- Source -----
    core_module/module.cpp
)

set(core_module_HEADER
    # ----- Headers -----
    core_module/OrkigePrerequisites.h
)

source_group(core_module FILES ${core_module_HEADER} ${core_module_SOURCE})

# ---------------------------------------------------------

set(core_python_SOURCE
    # ----- Source -----
    core_python/PythonInterpreter.cpp
)

set(core_python_HEADER
    # ----- Headers -----
    core_python/PythonErrorHelper.h
    core_python/PythonFunction.h
    core_python/PythonInterpreter.h
)

source_group(core_python FILES ${core_python_HEADER} ${core_python_SOURCE})

# ---------------------------------------------------------

set(core_serialization_SOURCE
    # ----- Source -----
    core_serialization/IArchive.cpp
    core_serialization/ISerializeable.cpp
    core_serialization/XMLArchive.cpp
)

set(core_serialization_HEADER
    # ----- Headers -----
    core_serialization/IArchive.h
    core_serialization/ISerializeable.h
    core_serialization/StreamOperators.h
    core_serialization/XMLArchive.h
)

source_group(core_serialization FILES ${core_serialization_HEADER} ${core_serialization_SOURCE})

# ---------------------------------------------------------

set(core_tinyxml_SOURCE
    # ----- Source -----
    core_tinyxml/tinystr.cpp
    core_tinyxml/tinyxml.cpp
    core_tinyxml/tinyxmlerror.cpp
    core_tinyxml/tinyxmlparser.cpp
)

set(core_tinyxml_HEADER
    # ----- Headers -----
    core_tinyxml/foreach.h
    core_tinyxml/tinystr.h
    core_tinyxml/tinyxml.h
)

source_group(core_tinyxml FILES ${core_tinyxml_HEADER} ${core_tinyxml_SOURCE})

# ---------------------------------------------------------

set(core_util_SOURCE
    # ----- Source -----
    core_util/ModuleManagerFactory.cpp
    core_util/SimpleState.cpp
    core_util/SimpleStateMachine.cpp
    core_util/StringUtil.cpp
    core_util/Timer.cpp
    core_util/PlatformUtil.mm
    core_util/PlatformUtil.cpp
)

set(core_util_HEADER
    # ----- Headers -----
    core_util/AttributeHolder.h
    core_util/Component.h
    core_util/ComponentHolder.h
    core_util/FastDelegate.h
    core_util/foreach.h
    core_util/Hash.h
    core_util/PlatformUtil.h
    core_util/MacroRepeat.h
    core_util/ModuleManagerFactory.h
    core_util/ObjectFactory.h
    core_util/optr.h
    core_util/SimpleState.h
    core_util/SimpleStateMachine.h
    core_util/Singleton.h
    core_util/String.h
    core_util/StringUtil.h
    core_util/Timer.h
)

source_group(core_util FILES ${core_util_HEADER} ${core_util_SOURCE})

# ---------------------------------------------------------
# Build setup
# ---------------------------------------------------------

add_definitions(-DSTATIC_LINKED)

include_directories(
	${CMAKE_SOURCE_DIR}/Dependencies/Source/boost
	./
	)

ogre_add_library(orkige_core STATIC
	${core_base_SOURCE}			${core_base_HEADER}
	${core_debug_SOURCE}		${core_debug_HEADER}
	${core_event_SOURCE}		${core_event_HEADER}
	${core_game_SOURCE}			${core_game_HEADER}
	${core_module_SOURCE}		${core_module_HEADER}
	${core_python_SOURCE}		${core_python_HEADER}
	${core_serialization_SOURCE}	${core_serialization_HEADER}
	${core_tinyxml_SOURCE}		${core_tinyxml_HEADER}
	${core_util_SOURCE}			${core_util_HEADER}
    )
