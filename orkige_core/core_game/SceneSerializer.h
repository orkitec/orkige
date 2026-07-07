/**************************************************************
	created:	2026/07/07 at 22:10
	filename: 	SceneSerializer.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __SceneSerializer_h__7_7_2026__22_10_00__
#define __SceneSerializer_h__7_7_2026__22_10_00__

#include "core_game/GameObjectManager.h"

namespace Orkige
{
	//! @brief saves and loads a whole GameObjectManager world to/from a .oscene file
	//! @remarks The scene file is an XMLArchive: a scene format version followed
	//! by every GameObject as id + component type list + each component's
	//! serialized state (ISerializeable::save/load). Components rebuild their
	//! renderer-side state themselves (onAdd creates the scene nodes, load
	//! re-applies transforms/meshes), so loading needs no direct renderer access
	//! and the serializer can live in the platform-independent core.
	class ORKIGE_CORE_DLL SceneSerializer
	{
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
		static const int SCENE_FORMAT_VERSION;		//!< version written into every saved scene
		static const String SCENE_FORMAT_MAGIC;	//!< magic marker written as the first scene value
	protected:
	private:
		//--- Methods -----------------------------------------
	public:
		//! save all GameObjects managed by the given GameObjectManager to fileName
		static bool saveScene(String const & fileName, GameObjectManager & gameObjectManager);
		//! @brief load a scene from fileName into the given GameObjectManager
		//! @remarks replaces the current world: all managed GameObjects are
		//! removed (their components tear down their scene-side state) before
		//! the saved GameObjects are recreated through the component factories
		static bool loadScene(String const & fileName, GameObjectManager & gameObjectManager);
	protected:
	private:
	};
	//---------------------------------------------------------
}

#endif //__SceneSerializer_h__7_7_2026__22_10_00__
