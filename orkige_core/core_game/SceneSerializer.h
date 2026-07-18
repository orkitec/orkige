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
	class XMLArchive;

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
		//! @brief load a scene from an in-memory XML string (no file to fopen)
		//! @remarks the mounted-pak / resource-system path: the scene bytes are
		//! read through the resource system (RenderSystem) from a mounted pak and
		//! parsed in memory. Same replace-the-world semantics as loadScene.
		//! @param sourceLabel names the source for error messages AND for prefab
		//! path resolution (a pak-mounted scene's prefabs resolve against it)
		static bool loadSceneFromString(String const & xml,
			GameObjectManager & gameObjectManager,
			String const & sourceLabel = "memory");

		//--- shared per-object helpers (PrefabSerializer reuses these) ---
		//! write one GameObject's component block (count, then per component
		//! the type name followed by the component's serialized state)
		static void writeComponents(optr<XMLArchive> const & ar, GameObject & gameObject);
		//! @brief read one GameObject's component block onto the (existing)
		//! object; components that dependencies already added are re-read in
		//! place. False on an unregistered component type (a hard error by
		//! design - the archive cursor cannot skip the unknown state element);
		//! fileName only labels the error message.
		static bool readComponents(optr<XMLArchive> const & ar,
			optr<GameObject> const & gameObject, String const & fileName);

		//--- reflected per-component property capture (prefab override / baseline) ---
		//! @brief capture ONE component's reflected properties (the full
		//! static UNION dynamic schema, transient/read-only skipped) as a
		//! name -> {kind, value, reference} map. This is the per-property baseline
		//! a prefab instance diffs its live children against, and the granularity a
		//! prefab override is stored at - reflection lets an override be the subset
		//! of NAMED fields that differ rather than an opaque whole-component block.
		//! @see saveComponentProperties for the identical per-field capture the
		//! scene serializer writes inline.
		static GameObject::ComponentPropertyMap captureComponentProperties(
			GameObjectComponent & component);
		//! @brief apply a captured property map (a per-property OVERRIDE subset)
		//! back onto an (existing) component through the reflected setters,
		//! resolving AssetRef references against the active AssetDatabase. Applied
		//! in schema declaration order (static then dynamic); a property absent
		//! from the map is left untouched (it keeps the prefab default).
		static void applyComponentProperties(
			GameObject::ComponentPropertyMap const & properties,
			GameObjectComponent & component);

		//--- reflection-driven component serialization ---
		//! @brief write a component's declared properties as a NAMED field block:
		//! a count followed by (name, kind, value, ref-id) records driven off the
		//! component type's PropertySchema (TypeManager). Transient/read-only
		//! (setter-less) properties are skipped; AssetRef properties carry their
		//! stable asset id in the trailing record field. This is what replaced the
		//! per-component positional save - a component's save() calls it after
		//! OParent::save (@see loadComponentProperties). Order-independent,
		//! add/remove-field tolerant by design (matched by name on load).
		static void saveComponentProperties(optr<IArchive> const & ar,
			GameObjectComponent & component);
		//! @brief read a NAMED field block written by saveComponentProperties and
		//! assign each present property through its reflected setter (resolving
		//! AssetRef references against the active AssetDatabase). A field missing
		//! from the file keeps the constructed default; a field not in the schema
		//! is ignored - so the named format has no per-version field gates.
		static void loadComponentProperties(optr<IArchive> const & ar,
			GameObjectComponent & component);
	protected:
	private:
		//! @brief the shared scene-read body: magic/version check, object loop
		//! and hierarchy/active application over an ALREADY-started archive
		//! (loadScene opens a file, loadSceneFromString parses a string, both
		//! funnel here). fileName labels errors + roots prefab resolution.
		static bool loadSceneFromArchive(optr<XMLArchive> const & ar,
			GameObjectManager & gameObjectManager, String const & fileName);
	};
	//---------------------------------------------------------
}

#endif //__SceneSerializer_h__7_7_2026__22_10_00__
