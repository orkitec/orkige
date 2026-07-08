/**************************************************************
	created:	2026/07/09 at 10:00
	filename: 	AssetDatabase.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __AssetDatabase_h__9_7_2026__10_00_00__
#define __AssetDatabase_h__9_7_2026__10_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"
#include "core_util/optr.h"

#include <map>

namespace Orkige
{
	class IArchive;

	//! @brief Unity-style stable asset ids for a project's assets - the
	//! foundation that lets scene references survive renames and moves.
	//! @remarks Every asset file under a project's assets/ and scripts/
	//! directories gets a sibling sidecar file "<name>.<ext>.orkmeta" holding
	//! its stable 128-bit random id as a tiny hand-readable XML document:
	//! @code
	//! <orkmeta id="5f2c..."/>
	//! @endcode
	//! Sidecars are the Unity-proven shape: per-asset files merge cleanly in
	//! VCS and travel with the asset when both are moved together. refresh()
	//! scans the project; with createSidecars (the EDITOR's import mode)
	//! missing sidecars are minted and orphaned ones deleted - a runtime
	//! (player, exported app; possibly on a read-only mobile bundle) refreshes
	//! read-only and simply has no ids for sidecar-less assets.
	//!
	//! Rename/move detection is deliberately simple and honest: moving an
	//! asset TOGETHER with its sidecar keeps the id; moving the asset alone
	//! mints a fresh id for it and drops the orphaned sidecar (logged) - no
	//! silent re-linking. Content-hash re-linking is future work.
	//!
	//! One database instance belongs to the open Project (Project::load
	//! creates and activates it); the process-wide "active" database is what
	//! the serialization helpers below resolve against, so components stay
	//! ignorant of project plumbing. Scene files carry the id as an XML
	//! attribute ("assetId") NEXT TO the legacy name/path value (see
	//! IArchive::writeAttributed) - old scenes without ids keep loading via
	//! the path, old engine builds ignore the extra attribute.
	class ORKIGE_CORE_DLL AssetDatabase
	{
		//--- Types -------------------------------------------
	public:
		//! what a serialized asset reference value names (see the
		//! serialization helpers below)
		enum ReferenceKind
		{
			REF_FILE_NAME,		//!< a bare resource file name ("ball.png" - Model/Sprite)
			REF_PROJECT_PATH	//!< a project-relative path ("scripts/player.lua")
		};
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
		static const String META_FILE_EXTENSION;		//!< ".orkmeta"
		static const String META_ELEMENT_NAME;			//!< "orkmeta"
		static const String META_ID_ATTRIBUTE;			//!< "id"
		//! the scene-file attribute carrying an asset id next to a value
		static const String REFERENCE_ID_ATTRIBUTE;	//!< "assetId"
	protected:
	private:
		String						mRootDirectory;	//!< absolute project root ("" until refreshed)
		std::map<String, String>	mIdToPath;			//!< id -> project-relative path
		std::map<String, String>	mPathToId;			//!< project-relative path -> id
		std::map<String, String>	mFileNameToId;		//!< bare file name -> id (first wins on clashes)
		static optr<AssetDatabase>	sActive;			//!< the open project's database (may be NULL)
		//--- Methods -----------------------------------------
	public:
		//! @brief (re)scan the project's assets/ and scripts/ directories
		//! recursively, loading sidecar ids.
		//! @param projectRootDirectory the project root (missing scan
		//! directories are simply empty)
		//! @param createSidecars true = the editor's IMPORT mode: mint a
		//! sidecar for every sidecar-less asset, delete orphaned sidecars and
		//! re-mint on duplicated ids (a copied asset+sidecar pair); false =
		//! read-only (runtimes never write into a project)
		void refresh(String const & projectRootDirectory, bool createSidecars);
		//! back to the empty state
		void clear();

		//! the scanned project root ("" before the first refresh)
		String const & getRootDirectory() const { return mRootDirectory; }
		//! number of assets that carry an id
		size_t getAssetCount() const { return mIdToPath.size(); }

		//! project-relative path of an asset id ("" when unknown)
		String pathForId(String const & assetId) const;
		//! id of a project-relative path ("" when unknown/sidecar-less)
		String idForPath(String const & relativePath) const;
		//! @brief id of a bare asset file name ("ball.png"); resource names
		//! are basenames in the engine's resource groups, so they must be
		//! unique per project anyway - on a clash the first (sorted) asset
		//! wins and the clash is logged. "" when unknown.
		String idForFileName(String const & fileName) const;
		//! bare file name of an asset id ("" when unknown)
		String fileNameForId(String const & assetId) const;

		//! @brief register one just-created asset file (editor-side asset
		//! creation, e.g. mesh import): mints and writes its sidecar when
		//! missing, reuses an existing one otherwise.
		//! @param assetPath absolute or project-relative asset file path
		//! @return the asset's id ("" when the path lies outside the root)
		String importAsset(String const & assetPath);

		//! a fresh 128-bit random id as 32 lower-case hex characters
		static String generateId();
		//! read a sidecar file; false (id untouched) when missing/invalid
		static bool readMetaFile(String const & metaFilePath, String & assetId);
		//! (over)write a sidecar file; false on an IO error
		static bool writeMetaFile(String const & metaFilePath, String const & assetId);

		//--- the active database (the open project's) --------
		//! @brief make the given database the process-wide one asset
		//! references resolve against (NULL = none - standalone scene loads
		//! fall back to their legacy paths)
		static void setActive(optr<AssetDatabase> const & database);
		//! the active database or NULL
		static optr<AssetDatabase> const & getActive();

		//--- serialization helpers (active-database based) ----
		//! @brief (save side) the id to serialize next to a reference value:
		//! the active database's current id for the value when it knows it
		//! (covers references set before/after the sidecar was minted),
		//! otherwise the stored id (kept even when unresolved - Unity-style,
		//! a temporarily missing asset must not lose its reference)
		static String referenceIdForValue(String const & value,
			String const & storedId, ReferenceKind kind);
		//! @brief (load side) resolve a serialized reference: when the id
		//! resolves in the active database the CURRENT file name/path REPLACES
		//! the serialized value (rename survival; re-saving upgrades the
		//! scene). Otherwise the legacy value carries, and when the value
		//! itself names a known asset the id self-heals to that asset's.
		//! Without an active database both stay exactly as read.
		static void resolveReference(String & valueInOut,
			String & assetIdInOut, ReferenceKind kind);
	protected:
	private:
		//! scan one directory tree (sorted, deterministic)
		void scanDirectory(String const & directory, bool createSidecars);
		//! register one asset (relative path + id) into the lookup maps
		void registerAsset(String const & relativePath, String const & assetId);
	};
	//---------------------------------------------------------
}

#endif //__AssetDatabase_h__9_7_2026__10_00_00__
