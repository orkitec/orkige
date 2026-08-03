/********************************************************************
	created:	Friday 2026/07/31 at 18:00
	filename: 	ExportBuildTree.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportBuildTree_h__31_7_2026__18_00_00__
#define __ExportBuildTree_h__31_7_2026__18_00_00__

#include <core_util/String.h>

#include <vector>

//! @file ExportBuildTree.h
//! @brief what an export needs to know about the preset build tree it packages
//! from, and where the engine media it bundles lives.
//!
//! An export never BUILDS the engine - it packages what a preset build tree
//! already produced. That tree answers four questions about itself, all out of
//! its own CMake cache and its vcpkg directory: which render flavor it is,
//! which architecture it targets, whether it is a Release build, and where the
//! backend's shader media sits. Everything here is a lookup; nothing decides
//! policy.
//!
//! The two engine sources an export can package from are the reason this is
//! separate from the packaging itself: a build tree answers through its cache,
//! and a STAGED engine payload (what a distributed editor carries inside
//! itself) answers through its own media tree - @see ExportEngineSource.

namespace OrkigeExport
{
	//! @brief the value of @p variable in @p buildDirectory's CMakeCache.txt,
	//! or "" when the tree or the entry is absent
	Orkige::String readCMakeCache(Orkige::String const & buildDirectory,
		Orkige::String const & variable);

	//! @brief the value part of one `NAME:TYPE=VALUE` cache line matching
	//! @p variable, or "" (PURE - the parse the reader above performs)
	Orkige::String parseCMakeCacheLine(Orkige::String const & line,
		Orkige::String const & variable);

	//! @brief the tree's `vcpkg_installed/<triplet>` (the one with an
	//! include/), or "" - the same detection cmake/OrkigeGameModule.cmake does
	Orkige::String vcpkgTripletDirectory(Orkige::String const & buildDirectory);

	//! @brief the tree's render flavor ("next" or "classic"); classic when the
	//! cache names none (the historical default)
	Orkige::String renderBackend(Orkige::String const & buildDirectory);

	//! @brief the shippable Release tree beside @p buildDirectory, when one
	//! exists: each flavor's release tree is its own (trees are flavor-bound) -
	//! build/macos-release for Ogre-Next, build/macos-release-classic for
	//! classic. A Debug player runs far slower, so an export prefers the
	//! Release sibling of the SAME flavor.
	Orkige::String siblingReleaseTree(Orkige::String const & buildDirectory);

	//! @brief the release-tree NAME for a flavor (PURE)
	Orkige::String releaseTreeName(Orkige::String const & flavor);

	//! @brief the tree's target architecture derived from its vcpkg triplet
	//! ("arm64-osx" -> "arm64"), or "".
	//! @remarks the exporter PINS a native-module build to it: without the
	//! pin, clang targets whatever architecture the spawning process runs as,
	//! and an x86_64 parent can silently turn the module build x86_64 while
	//! the engine libraries are arm64.
	Orkige::String tripletArchitecture(Orkige::String const & triplet);

	//! @see tripletArchitecture - resolved from the tree's own triplet dir
	Orkige::String engineTreeArchitecture(
		Orkige::String const & buildDirectory);

	//! @brief the classic flavor's RTSS shader-library media root
	//! (`share/ogre/Media`, holding Main + RTShaderLib), or ""
	Orkige::String ogreMediaDirectory(Orkige::String const & buildDirectory);

	//! @brief the Ogre-Next flavor's media root (`share/ogre-next/Media`,
	//! holding Hlms - the shader templates the runtime registers - and
	//! Atmosphere, the sky material media), or ""
	Orkige::String ogreNextMediaDirectory(
		Orkige::String const & buildDirectory);

	//! @brief the Ogre-Next Media subdirectories to bundle: Hlms (mandatory -
	//! materials do not work without it) plus Atmosphere when the installed
	//! port ships it. An older port pin may not, and the runtime degrades that
	//! honestly (no sky, flat fog colour), so bundling stays optional here too
	//! rather than a hard failure.
	std::vector<Orkige::String> ogreNextMediaSubdirs(
		Orkige::String const & mediaDirectory);

	//! @brief the engine media directories committed to the SOURCE tree that
	//! every runtime resolves by name at boot. Each is "" when absent.
	struct EngineSourceMedia
	{
		Orkige::String	fonts;	//!< the engine-default font (SIL OFL)
		Orkige::String	water;	//!< the shared water plane mesh + normal map
		Orkige::String	decals;	//!< the default mark + blob-shadow textures
		Orkige::String	rtss;	//!< the engine-owned classic shader library
		Orkige::String	bloom;	//!< the bloom compositor media, PER FLAVOR
		Orkige::String	grade;	//!< the output-grade compositor media, per flavor
	};

	//! @brief the same set located under an engine media ROOT - the source
	//! tree's `orkige_engine/media`, or the `media/` an SDK pack carries,
	//! which is that directory installed verbatim
	EngineSourceMedia engineMediaFromRoot(Orkige::String const & mediaRoot,
		Orkige::String const & flavor);

	//! @see EngineSourceMedia
	EngineSourceMedia engineSourceMedia(Orkige::String const & repoRoot,
		Orkige::String const & flavor);
}

#endif //__ExportBuildTree_h__31_7_2026__18_00_00__
