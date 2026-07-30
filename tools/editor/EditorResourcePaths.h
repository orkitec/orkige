/********************************************************************
	created:	Thursday 2026/07/30 at 09:00
	filename: 	EditorResourcePaths.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __EditorResourcePaths_h__30_7_2026__09_00_00__
#define __EditorResourcePaths_h__30_7_2026__09_00_00__

#include <core_util/String.h>

#include <functional>

//! @file EditorResourcePaths.h
//! @brief the ONE seam answering "where is resource X" for the editor:
//! BUNDLE first (the app the editor was copied as), developer TREE second.
//!
//! The editor ships as a self-contained app - a copy of it on a machine with
//! no repository, no engine build tree and no Python must render, open a
//! project and play. Everything it needs at runtime therefore rides inside the
//! app, and every consumer asks this locator instead of reading a baked
//! developer path directly. The tree paths remain as the SECOND choice so a
//! build-tree run keeps working with no staging.
//!
//! @par Layout
//! The bundle layout mirrors what a project export produces (@see
//! PlayerBundle::resolveMediaDirectory), so one convention covers the editor,
//! the player and every exported game:
//! - macOS: `Orkige.app/Contents/Resources/Media/...` for content,
//!   `Orkige.app/Contents/MacOS/` for the sibling executables (the player and
//!   the texture cook tool - nested executables belong beside the main one so
//!   one signature covers the bundle).
//! - elsewhere: `<executable dir>/share/orkige/Media/...` for content and
//!   `<executable dir>/` for the sibling executables.
//! `SDL_GetBasePath()` is the base in both cases (macOS resolves it to the
//! bundle's Resources directory, every other platform to the executable's
//! directory), so the two roots derive from that ONE probe.
//!
//! @par Purity
//! The locator asks the filesystem exactly one kind of question ("does this
//! path exist?") through an injectable predicate, so its whole decision table
//! is unit-tested headlessly with no staged bundle
//! (EditorResourcePathsTests). The developer-tree fallbacks arrive as data:
//! CMake bakes them into the editor executable, which binds them in
//! EditorResourceBinding.cpp - editor_core itself carries no absolute path.

namespace OrkigeEditor
{
	//! which root answered a resource query
	enum class EditorResourceRoot
	{
		Bundle,		//!< the app the editor was copied as carries it
		Tree,		//!< the developer source/build tree it was built in
		Missing		//!< neither has it - the caller degrades honestly
	};

	//! one resolved resource: the path plus the root that answered
	struct EditorResourcePath
	{
		Orkige::String		path;
		EditorResourceRoot	root = EditorResourceRoot::Missing;

		//! did any root have it? (a Missing answer carries an empty path)
		bool found() const { return this->root != EditorResourceRoot::Missing; }
		//! did the BUNDLE answer? (the distributed-app case)
		bool fromBundle() const
		{
			return this->root == EditorResourceRoot::Bundle;
		}
	};

	//! @brief the developer-tree paths CMake bakes into the editor executable:
	//! the SECOND choice behind the bundled copies. Every field may be empty
	//! (a build that ships no such media resolves Missing and the consumer
	//! logs its own honest line).
	struct EditorResourceFallbacks
	{
		//! the render flavor's shader media root (classic: the RTSS library
		//! holding Main/ + RTShaderLib/; next: the Hlms shader templates)
		Orkige::String	engineMedia;
		//! the subdirectory that MARKS a real engine-media root ("Main" on
		//! classic, "Hlms" on next): a Media/ without it cannot render, so
		//! probing for it is what distinguishes a staged bundle from an empty
		//! directory. @see PlayerBundle::resolveMediaDirectory
		Orkige::String	engineMediaMarker;
		//! the render flavor this editor was built with ("next"/"classic"):
		//! the compositor media is staged per flavor (Media/bloom/<flavor>),
		//! exactly as an export stages it
		Orkige::String	flavor;
		Orkige::String	fonts;		//!< engine-default font (Nunito)
		Orkige::String	water;		//!< water plane mesh + normal map
		Orkige::String	decals;		//!< default decal textures
		Orkige::String	bloom;		//!< bloom compositor media (per flavor)
		Orkige::String	grade;		//!< output-grade media (per flavor)
		//! the editor's own UI fonts (icon font, mono symbols) source dir
		Orkige::String	uiFonts;
		Orkige::String	player;		//!< the play-mode player executable
		Orkige::String	texcook;	//!< the texture cook tool executable
	};

	//! @brief bundle-first / tree-second resolution of everything a
	//! distributed editor carries. @see EditorResourcePaths.h
	class EditorResourceLocator
	{
		//--- Types -------------------------------------------
	public:
		//! the filesystem question the locator asks (injected so the decision
		//! table is testable with no files on disk)
		using ExistsFn = std::function<bool(Orkige::String const &)>;

		//--- Variables ---------------------------------------
	private:
		Orkige::String				mResourceRoot;	//!< holds Media/ (may be "")
		Orkige::String				mToolRoot;		//!< holds the executables
		EditorResourceFallbacks		mFallbacks;
		ExistsFn					mExists;

		//--- Methods -----------------------------------------
	public:
		//! @brief @p bundleBase is SDL_GetBasePath() (empty when the platform
		//! cannot provide one - every query then falls back to the tree).
		//! @p exists defaults to a std::filesystem probe.
		EditorResourceLocator(Orkige::String const & bundleBase,
			EditorResourceFallbacks const & fallbacks,
			ExistsFn exists = ExistsFn());

		//! the bundle directory that holds `Media/` ("" without a base path)
		Orkige::String const & bundleResourceRoot() const
		{
			return this->mResourceRoot;
		}
		//! the bundle directory that holds the sibling executables
		Orkige::String const & bundleToolRoot() const
		{
			return this->mToolRoot;
		}

		//! @brief the render flavor's shader media root - the resource the
		//! editor cannot render without (Hlms templates on next, the RTSS
		//! library on classic). Bundled `Media/` wins when it carries the
		//! flavor's marker subdirectory.
		EditorResourcePath engineMedia() const;
		//! engine-default font dir (a project's .ogui references it by name)
		EditorResourcePath engineFonts() const;
		//! water plane mesh + tiling normal map (WaterComponent previews)
		EditorResourcePath engineWater() const;
		//! default decal textures (DecalComponent previews)
		EditorResourcePath engineDecals() const;
		//! bloom compositor media of this flavor (a Play session's setBloom)
		EditorResourcePath engineBloom() const;
		//! output-grade compositor media of this flavor (setGrade)
		EditorResourcePath engineGrade() const;
		//! one of the editor's own UI font files by name (icon/mono symbols);
		//! bundled copies sit at the resource root, not under Media/
		EditorResourcePath uiFont(Orkige::String const & fileName) const;
		//! the player executable Play spawns
		EditorResourcePath player() const;
		//! the texture cook tool the export path drives
		EditorResourcePath texcook() const;

		//! @brief the ONE boot line: which root this editor runs out of and
		//! where its engine media came from
		Orkige::String describe() const;

	private:
		//! bundle `<resourceRoot><relative>` when it exists, else @p fallback
		//! when THAT exists, else Missing
		EditorResourcePath resolveResource(Orkige::String const & relative,
			Orkige::String const & fallback) const;
		//! bundle `<toolRoot><fileName>` when it exists, else @p fallback
		EditorResourcePath resolveTool(Orkige::String const & fileName,
			Orkige::String const & fallback) const;
	};

	//! @brief the process-wide locator, built from the developer-tree
	//! fallbacks CMake bakes into the editor executable.
	//! @remarks DEFINED in the editor executable (EditorResourceBinding.cpp),
	//! declared here so every consumer shares the one instance. editor_core
	//! itself never calls it - the pure locator above is what its unit tests
	//! exercise.
	EditorResourceLocator const & editorResources();

	//! @brief the directory the editor WRITES its own state into (settings
	//! inis, the engine log): the platform's per-user application-support
	//! directory, created on demand.
	//! @remarks A distributed app bundle is read-only (and self-writes
	//! invalidate its signature), so nothing the editor persists may live next
	//! to the executable. `ORKIGE_EDITOR_STATE_DIR` redirects the whole set
	//! (the test-isolation seam - the support directory follows the user
	//! account, not HOME, so a scripted run has no other way to stay out of the
	//! real user's editor state). Returns "" when the platform cannot provide
	//! one; the caller then keeps its historical relative path.
	Orkige::String editorWritableStateDirectory();

	//! @brief the path the editor persists @p fileName at (inside
	//! editorWritableStateDirectory()), migrating a single legacy copy from
	//! @p legacyDirectory once.
	//! @remarks Earlier builds wrote the settings inis NEXT TO the executable.
	//! A user upgrading in place keeps their layout/recents: the legacy file is
	//! MOVED to the writable directory the first time (copy then remove; a
	//! failed remove leaves the original alone and the move still counts, so
	//! the state travels either way). Falls back to @p legacyDirectory (the
	//! historical location) on a platform with no writable app directory, so
	//! the answer is always a usable path.
	Orkige::String editorStateFilePath(Orkige::String const & fileName,
		Orkige::String const & legacyDirectory);
}

#endif //__EditorResourcePaths_h__30_7_2026__09_00_00__
