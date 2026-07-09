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

#include <filesystem>
#include <map>
#include <vector>

namespace Orkige
{
	class IArchive;

	//! @brief one enumerated asset the database knows about (id + where it
	//! lives), the shape listAssets() returns. Plain strings so callers above
	//! the project layer (the editor's MCP control server, an asset browser)
	//! stay ignorant of the internal lookup maps.
	struct ORKIGE_CORE_DLL AssetEntry
	{
		String id;				//!< stable 32-hex asset id
		String relativePath;	//!< project-relative path ("assets/ball.png")
		String fileName;		//!< bare file name ("ball.png")
	};

	//! @brief a texture asset's import settings for ONE platform: how the
	//! runtime samples it (filter/wrap, honored LIVE at sprite material
	//! creation) and how the EXPORT-TIME cook conditions the shipped pixels
	//! (maxSize downscale + premultiply; generateMips is a forward-looking
	//! flag the cook does not act on yet).
	//! @remarks filter/wrap are kept as strings ("point"/"bilinear",
	//! "clamp"/"wrap") because this struct lives in core, BELOW the render
	//! facade that owns the SpriteQuad sampler enums - the engine layer maps
	//! the strings onto those enums. GPU-compressed formats are deliberately
	//! ABSENT: the runtime registers only the PNG/JPG image codec AND the
	//! stdlib-Python cook has no block-compression encoder, so compression is
	//! double-blocked and stays out of v1 (its own separate future effort).
	struct ORKIGE_CORE_DLL TextureImportSettings
	{
		String	filter = "bilinear";	//!< "point" | "bilinear"
		String	wrap = "clamp";			//!< "clamp" | "wrap"
		int		maxSize = 0;			//!< texel cap (longest side); 0 = uncapped
		bool	premultiply = false;	//!< cook premultiplies alpha into RGB
		bool	generateMips = false;	//!< reserved (the cook does not build mips yet)
	};

	//! @brief the full <texture> import block of a sidecar: the default
	//! settings plus optional per-platform override sub-blocks. Each platform
	//! sub-block is stored ALREADY RESOLVED against the default (the reader
	//! starts every override from the default and overlays the attributes the
	//! sub-block actually spells), so resolvedFor() is a plain lookup.
	struct ORKIGE_CORE_DLL TextureImport
	{
		TextureImportSettings	base;				//!< the default (desktop) settings
		bool					hasAndroid = false;	//!< an <android> override exists
		TextureImportSettings	android;			//!< resolved Android settings
		bool					hasIos = false;		//!< an <ios> override exists
		TextureImportSettings	ios;				//!< resolved iOS settings

		//! @brief the effective settings for a platform token ("android"/"ios"
		//! use the override when present; anything else - "" = desktop - the
		//! default)
		TextureImportSettings const & resolvedFor(String const & platform) const;
	};

	//! @brief stable asset ids for a project's assets - the
	//! foundation that lets scene references survive renames and moves.
	//! @remarks Every asset file under a project's assets/ and scripts/
	//! directories gets a sibling sidecar file "<name>.<ext>.orkmeta" holding
	//! its stable 128-bit random id as a tiny hand-readable XML document:
	//! @code
	//! <orkmeta id="5f2c..."/>
	//! @endcode
	//! Sidecars are the per-asset sidecar shape: per-asset files merge cleanly in
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
		static const String META_TEXTURE_ELEMENT_NAME;	//!< "texture" (the import block)
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
		//! @brief every id-carrying asset (id + project-relative path + bare
		//! file name), sorted by project-relative path for a stable listing.
		//! The clean enumeration accessor over the otherwise-private lookup
		//! maps (the MCP control server / an asset browser build on it).
		std::vector<AssetEntry> listAssets() const;

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

		//! @brief move/rename one asset file AND its sidecar in one step; the id
		//! survives (the pair travels together, the documented keep-the-id
		//! rule). Both paths are project-relative; parent directories of the
		//! destination are created. False when the source is missing, the
		//! destination exists, either path escapes the root, or the rename
		//! fails. A file the database does not index (sidecar-less, or under a
		//! non-tracked tree) moves fine - there is simply no map entry to carry.
		bool moveAsset(String const & relativePath, String const & newRelativePath);

		//! @brief copy one asset next to itself as "<stem> Copy[ N]<ext>",
		//! minting a FRESH id for the copy while carrying the source sidecar's
		//! <texture> import block over (settings are per-asset intent, ids are
		//! per-file identity). Returns the copy's project-relative path ("" on
		//! failure - a missing source or a path outside the root).
		String duplicateAsset(String const & relativePath);

		//! a fresh 128-bit random id as 32 lower-case hex characters
		static String generateId();
		//! read a sidecar file; false (id untouched) when missing/invalid.
		//! @remarks reads ONLY the root id and ignores any children, so a v2
		//! sidecar carrying a <texture> import block reads exactly like a v1
		//! id-only one - back-compat is automatic.
		static bool readMetaFile(String const & metaFilePath, String & assetId);
		//! (over)write a plain id-only sidecar; false on an IO error
		static bool writeMetaFile(String const & metaFilePath, String const & assetId);
		//! @brief (over)write a sidecar carrying its id AND a <texture> import
		//! block (per-platform overrides included); false on an IO error. The
		//! id is preserved by the caller passing it - read it with readMetaFile
		//! first when updating an existing sidecar's settings.
		static bool writeMetaFile(String const & metaFilePath,
			String const & assetId, TextureImport const & texture);
		//! @brief read the <texture> import block of a sidecar into settings;
		//! false (settings left at defaults) when the file is missing/invalid
		//! or carries no <texture> block (an id-only v1 sidecar).
		static bool readImportSettings(String const & metaFilePath,
			TextureImport & texture);

		//! @brief absolute .orkmeta path of an asset id, or "" when the id is
		//! unknown (used to reach a texture's import settings from its id)
		String metaFilePathForId(String const & assetId) const;

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
		//! otherwise the stored id (kept even when unresolved,
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
		//! @brief normalize a project-relative-or-absolute asset path to an
		//! absolute, lexically-normal path INSIDE the root; false when the root
		//! is unset, the input is empty, or it escapes the root (a "../" head).
		//! The shared containment check behind importAsset/moveAsset/duplicate.
		bool resolveInsideRoot(String const & relativeOrAbsolute,
			std::filesystem::path & absoluteOut) const;
		//! scan one directory tree (sorted, deterministic)
		void scanDirectory(String const & directory, bool createSidecars);
		//! register one asset (relative path + id) into the lookup maps
		void registerAsset(String const & relativePath, String const & assetId);
	};
	//---------------------------------------------------------
}

#endif //__AssetDatabase_h__9_7_2026__10_00_00__
