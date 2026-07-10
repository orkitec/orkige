/**************************************************************
	created:	2026/07/09 at 12:00
	filename: 	PrefabSerializer.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __PrefabSerializer_h__9_7_2026__12_00_00__
#define __PrefabSerializer_h__9_7_2026__12_00_00__

#include "core_game/GameObjectManager.h"

namespace Orkige
{
	//! @brief saves a GameObject SUBTREE as a reusable .oprefab asset and
	//! instantiates such assets back into a world (the prefab pattern,
	//! v1: STRUCTURAL variants + root overrides only).
	//! @remarks The .oprefab file is a thin sibling of the .oscene format
	//! (same XMLArchive per-object block, shared through the SceneSerializer
	//! helpers): magic, version, the ROOT's prefab-local id, then every
	//! subtree object under a prefab-LOCAL id ("Frame", "WallLeft", ...) with
	//! local parent links. Instantiating remaps every non-root object to the
	//! DETERMINISTIC instance id "<instanceRootId>/<localId>" - that
	//! determinism is the contract that makes a scene's structural overrides
	//! (suppressed children) re-match across save/load, and the
	//! "<instanceRootId>/" id namespace is how prefab-provided objects are
	//! told apart from scene-side extra children.
	//! Nested prefabs are NOT supported in v1: savePrefab refuses a subtree
	//! containing an instance root below the saved root, and instantiation
	//! hard-errors on a prefabRef inside a prefab file.
	class ORKIGE_CORE_DLL PrefabSerializer
	{
		//--- Types -------------------------------------------
	public:
		//! outcome of instantiatePrefab (the caller decides the policy: a
		//! missing FILE becomes a placeholder instance, everything else is a
		//! hard error - see SceneSerializer::loadScene)
		enum InstantiateResult
		{
			INSTANTIATE_OK,				//!< the instance subtree is live
			INSTANTIATE_FILE_MISSING,	//!< the prefab file does not exist
			INSTANTIATE_ERROR			//!< corrupt file, nested prefab, id clash
		};
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
		static const int PREFAB_FORMAT_VERSION;		//!< version written into every saved prefab
		static const String PREFAB_FORMAT_MAGIC;	//!< magic marker written as the first prefab value
		//! separator between an instance root id and a prefab-local id
		static const String INSTANCE_ID_SEPARATOR;	//!< "/"
	protected:
	private:
		//--- Methods -----------------------------------------
	public:
		//! @brief save the subtree rooted at rootId as a .oprefab file.
		//! @remarks Prefab-local ids are derived deterministically from the
		//! live object ids (see localIdForChild); the root's own prefabRef is
		//! IGNORED (re-making an instance's prefab is the v1 edit loop) and
		//! never written, but a prefab instance BELOW the root refuses the
		//! save - nested prefabs are v2.
		static bool savePrefab(String const & fileName,
			GameObjectManager & gameObjectManager, String const & rootId);
		//! @brief instantiate a .oprefab under the given instance root id:
		//! the root object is reused when it already exists (the scene loader
		//! creates it before it knows the object is an instance) or created,
		//! the prefab's root components become the root's defaults, every
		//! other prefab object lands as "<instanceRootId>/<localId>" and the
		//! suppressedChildren (prefab-local ids) are dropped subtree-deep.
		//! On INSTANTIATE_ERROR the partially created children are removed
		//! again (the root object is left for the caller to decide about).
		static InstantiateResult instantiatePrefab(String const & fileName,
			GameObjectManager & gameObjectManager,
			String const & instanceRootId,
			StringVector const & suppressedChildren);
		//! @brief read-only probe of a .oprefab file: fills outLocalIds with
		//! every prefab-local id (root first, in file order) and
		//! outRootComponentTypes with the component type names on the prefab
		//! ROOT. Nothing is instantiated - the archive is walked and each
		//! component's serialized state is stepped over unread. false (and the
		//! outputs cleared) on a missing/wrong-magic/too-new file. The palette
		//! uses it to know a prefab's edge-wall children and whether its root
		//! already carries a component the paint tool would stamp.
		static bool listPrefabInfo(String const & fileName,
			StringVector & outLocalIds, StringVector & outRootComponentTypes);
		//! the instance-namespace id of one prefab-local id
		static String instanceChildId(String const & instanceRootId,
			String const & localId);
		//! does the id lie inside the given instance root's id namespace
		//! (i.e. it starts with "<instanceRootId>/")
		static bool isInstanceChildId(String const & instanceRootId,
			String const & id);
		//! @brief prefab-local id savePrefab uses for a subtree object: the
		//! object id with the root id prefix (plus one separator character)
		//! stripped when present ("TileA_Frame"/"TileA/Frame" under root
		//! "TileA" -> "Frame"), the full object id otherwise - deterministic,
		//! so locals stay stable across the make/instantiate/re-make loop
		static String localIdForChild(String const & rootId,
			String const & childId);
		//! @brief is the object a prefab-PROVIDED part of an instance: some
		//! ancestor is an instance root and the object id lies inside that
		//! ancestor's "<rootId>/" namespace. Scene-side extra children under
		//! an instance root (their ids are outside the namespace) are NOT
		//! prefab-provided - the scene serializes them normally.
		static bool isPrefabProvided(GameObjectManager & gameObjectManager,
			GameObject const & gameObject);
		//! @brief id of the nearest ancestor prefab-instance ROOT whose
		//! "<rootId>/" id namespace contains the given object, or "" when the
		//! object is not prefab-provided (a plain object or a scene-side extra
		//! child under an instance). isPrefabProvided is this returning non-"".
		static String instanceRootIdOf(GameObjectManager & gameObjectManager,
			GameObject const & gameObject);
	protected:
	private:
	};
	//---------------------------------------------------------
}

#endif //__PrefabSerializer_h__9_7_2026__12_00_00__
