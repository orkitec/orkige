/********************************************************************
	created:	Friday 2026/07/31 at 18:00
	filename: 	ExportPayload.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportPayload_h__31_7_2026__18_00_00__
#define __ExportPayload_h__31_7_2026__18_00_00__

#include "ExportBuildTree.h"
#include "ExportProcess.h"
#include "ExportProject.h"

#include <core_util/String.h>

#include <functional>
#include <vector>

//! @file ExportPayload.h
//! @brief what rides inside every packaged app: the shippable slice of the
//! project, and the engine media the runtime registers at boot.
//!
//! The PROJECT payload is the manifest plus scenes/assets/scripts - native/
//! and builds/ stay home, because compiled code ships as the packaged binary.
//! Project-CONFIG assets ride along too: files a manifest Setting names rather
//! than assets/ residents (the config-asset convention), copied verbatim at
//! their project-relative paths.
//!
//! The ENGINE media is laid out as `<resources>/Media` in the exact shape a
//! runtime resolves at boot (`PlayerBundle::resolveMediaDirectory`), so the
//! packaged app carries no vcpkg or source-tree path. A distributed editor
//! stages exactly this layout inside itself, which is why packaging from one
//! is a single copy of its `Media/`.
//!
//! The MARKER is the no-args default-project mechanism: the runtimes read
//! `orkige_project.txt` from `SDL_GetBasePath()` and boot the project it
//! names, with no command line at all.
//!
//! A payload carries NO `.orkmeta` sidecars: they are editor bookkeeping, and
//! the one thing a runtime reads out of them - how a texture is sampled - is
//! RESOLVED here, once, for the platform being packaged, into the payload
//! manifest's baked `<TextureSamplers>` block (@see Orkige::Project). Nothing
//! in a frozen payload can be renamed, so asset ids have nothing left to do.

namespace OrkigeExport
{
	//! @brief the log sink an export writes its progress to
	typedef std::function<void(Orkige::String const &)> ExportLog;

	//! @brief where an export takes its engine pieces from. A build tree is
	//! the developer case; a STAGED payload is what a distributed editor
	//! carries inside itself. Everything after the sourcing is the same code.
	struct EngineSource
	{
		//! the preset build tree ("" when packaging from a staged payload)
		Orkige::String	buildDirectory;
		//! the staged payload's resource root, holding Media/ ("" for a tree)
		Orkige::String	bundleResources;
		//! where the staged payload's executables live (defaults to the
		//! resource root; a macOS app keeps them in Contents/MacOS)
		Orkige::String	bundleTools;
		//! the installed SDK pack a project's compiled C++ game code is built
		//! against when there is no build tree ("" = none). A staged payload
		//! carries a PLAYER, which is all a Lua project needs; a project with
		//! compiled game code needs an engine to compile and link against, and
		//! a relocatable pack is that engine (Docs/sdk-pack.md).
		Orkige::String	sdkPack;
		//! the prebuilt DEVICE player payload for the platform being packaged
		//! ("" = none). A staged payload carries the HOST's player; a phone
		//! runs another architecture's, so that one is fetched separately and
		//! kept outside the (read-only, signed) application - the exporter is
		//! handed the directory and never learns how it got there.
		//! Its layout is a player bundle plus the `Media/` tree that player
		//! renders through, so packaging from it needs no source tree at all
		//! (Docs/device-payloads.md).
		Orkige::String	devicePayload;

		bool fromBundle() const { return !this->bundleResources.empty(); }
	};

	//! @brief everything one export run needs that is not the project itself
	struct ExportEnvironment
	{
		Orkige::String	repoRoot;			//!< the engine source tree ("" if none)
		Orkige::String	defaultIconPath;	//!< the neutral engine icon
		Orkige::String	cmake = "cmake";	//!< for native-module builds
		Orkige::String	ninja;				//!< optional generator program
		ExportLog		log;
		ProcessRunner	runner;
	};

	//! @brief copy the manifest-referenced project-config assets (the
	//! `configSettingKeys()` values) into @p destination, preserving each
	//! project-relative path.
	//! @remarks a setting may name a single FILE (input.oactions and its
	//! siblings) or a DIRECTORY (localisation names a tree of one .xlf per
	//! language) - both bundle. A setting pointing at nothing WARNS and is
	//! skipped: a stale key in a manifest is not a reason to refuse an export.
	//! @param outStaged receives the number of files staged
	bool stageConfigSettings(ExportProject const & project,
		Orkige::String const & destination, ExportLog const & log,
		int * outStaged, Orkige::String * error);

	//! @brief resolve the project's texture samplers for @p texturePlatform
	//! into @p manifestPath's baked `<TextureSamplers>` block, then drop every
	//! `.orkmeta` sidecar under @p payloadDirectory.
	//! @remarks the ONE place a shipped build's sampler question is answered.
	//! The fill is the SAME code an authoring project runs on load
	//! (`TextureSamplerTable::fillFromAssets` over a read-only scan of the
	//! SOURCE project), so a baked answer and a live one agree by construction.
	//! @param outStripped receives the number of sidecars removed
	bool bakeTextureSamplers(Orkige::String const & projectRoot,
		Orkige::String const & payloadDirectory,
		Orkige::String const & manifestPath,
		Orkige::String const & texturePlatform, ExportLog const & log,
		int * outStripped, Orkige::String * error);

	//! @brief drop every `*.editor.lua` under @p payloadDirectory.
	//! @remarks an editor TOOL is dev tooling - a command in the editor's Tools
	//! menu - and is structurally inert in a player: nothing in a runtime
	//! discovers it, and the `editor.*` verb table it is written against does
	//! not exist there. It rides inside `scripts/`, which is a payload
	//! subdirectory, so it needs its own strip the way `.orkmeta` does. Test
	//! files need no strip: they live in `tests/`, which is not a payload
	//! subdirectory at all - out by construction rather than by removal.
	//! @param outStripped receives the number of files removed
	bool stripEditorScripts(Orkige::String const & payloadDirectory,
		ExportLog const & log, int * outStripped, Orkige::String * error);

	//! @brief copy the shippable project subset into @p destination and run
	//! the export-time texture cook over it for @p platform and @p flavor.
	//! @param texturePlatform the cook's platform token (@see
	//!        cookPlatformToken)
	//! @param outStaged receives the number of files staged
	bool stageProjectPayload(ExportProject const & project,
		Orkige::String const & destination,
		Orkige::String const & texturePlatform, Orkige::String const & flavor,
		ExportLog const & log, int * outStaged, Orkige::String * error);

	//! @brief lay the engine media a runtime registers at boot into
	//! `<resources>/Media`, sourced from the build's vcpkg + the source tree.
	//! @remarks per flavor: the classic RTSS shader library (Main +
	//! RTShaderLib, with the engine's own metal-rough library merged INTO
	//! RTShaderLib - the ONE location the runtime registers) or the Ogre-Next
	//! Hlms templates + the Atmosphere sky media. Then the content media each
	//! runtime resolves by name: fonts, water, decals and the per-flavor
	//! bloom/grade compositor media.
	bool stageEngineMediaFromTree(Orkige::String const & resources,
		Orkige::String const & backendMediaDirectory,
		Orkige::String const & flavor, EngineSourceMedia const & sourceMedia,
		Orkige::String * error);

	//! @brief lay the CONTENT media a runtime resolves by name - fonts,
	//! water, decals and the per-flavor bloom/grade compositor media - into
	//! `<resources>/Media`.
	//! @remarks the half of the media a prebuilt mobile player bundle does
	//! NOT already carry: its build embedded the backend's shader tree, but
	//! the engine's own content directories are committed to the source tree
	//! and are added at packaging time. An absent source directory is not an
	//! error (the runtime degrades honestly).
	bool stageEngineContentMedia(Orkige::String const & resources,
		Orkige::String const & flavor, EngineSourceMedia const & sourceMedia,
		Orkige::String * error);

	//! @brief write the default-project marker naming the payload directory
	bool writeProjectMarker(Orkige::String const & directory,
		Orkige::String * error);

	//! the third-party license notices every packaged artifact carries, at its
	//! resource root - beside the project marker, in the directory
	//! `SDL_GetBasePath()` answers
	extern const char * const THIRD_PARTY_NOTICES_FILE_NAME;

	//! @brief the ordered places the notices file is looked for: the file name
	//! under each non-empty root, empty roots dropped (PURE - no filesystem is
	//! touched). The caller copies the first entry that exists.
	std::vector<Orkige::String> thirdPartyNoticesCandidates(
		std::vector<Orkige::String> const & roots);

	//! @see thirdPartyNoticesCandidates - the roots an @ref EngineSource
	//! carries, in the same precedence every other packaged piece follows: a
	//! distributed editor answers from INSIDE ITSELF first, then the fetched
	//! device payload, then an installed SDK pack, and an engine source tree
	//! last.
	std::vector<Orkige::String> thirdPartyNoticesCandidates(
		EngineSource const & source, Orkige::String const & repoRoot);

	//! @brief copy the third-party license notices to @p destination.
	//! @remarks the licenses of the statically linked closure require their
	//! text to travel with a BINARY distribution, so the file rides in the
	//! package rather than only in the engine repository. A source that carries
	//! none WARNS rather than refusing: an export that cannot find a text file
	//! must still produce a runnable app, and the export suite asserts the file
	//! is there so a packaging path cannot lose it silently.
	//! @param outStaged receives 1 when the file was copied, 0 when none was
	//!        found
	bool stageThirdPartyNotices(Orkige::String const & destination,
		EngineSource const & source, ExportEnvironment const & environment,
		int * outStaged, Orkige::String * error);

	//! @see stageThirdPartyNotices - the same copy driven by an explicit
	//! candidate list, for a packaging step that carries its own roots rather
	//! than an @ref EngineSource
	bool stageThirdPartyNoticesFrom(Orkige::String const & destination,
		std::vector<Orkige::String> const & candidates, ExportLog const & log,
		int * outStaged, Orkige::String * error);

	//! the manifest a fetched device payload describes itself with, at its
	//! root: `key: value` lines carrying the platform, the render flavor and
	//! the build it was published for
	extern const char * const DEVICE_PAYLOAD_MANIFEST_FILE_NAME;

	//! @brief the value of @p key in @p payloadDirectory's manifest, or ""
	//! when the payload or the key is absent
	Orkige::String devicePayloadSetting(Orkige::String const & payloadDirectory,
		Orkige::String const & key);

	//! @brief the render flavor a fetched device payload records
	//! ("next"/"classic"); classic when it records none, matching the way a
	//! build tree's own cache is read (@see renderBackend)
	Orkige::String payloadFlavor(Orkige::String const & payloadDirectory);
}

#endif //__ExportPayload_h__31_7_2026__18_00_00__
