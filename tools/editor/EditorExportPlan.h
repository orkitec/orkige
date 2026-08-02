/********************************************************************
	created:	Friday 2026/07/31 at 09:00
	filename: 	EditorExportPlan.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __EditorExportPlan_h__31_7_2026__09_00_00__
#define __EditorExportPlan_h__31_7_2026__09_00_00__

#include "EditorResourcePaths.h"

#include <core_util/String.h>

//! @file EditorExportPlan.h
//! @brief the ONE decision behind "package this project": what the export
//! packages from, and - when it cannot run - the one sentence that says why in
//! terms the person reading it can act on.
//!
//! The exporter itself is code this editor links, so there is nothing to find
//! and nothing to spawn. What still differs between two shapes of editor is
//! what they carry to package FROM:
//! - built from the SOURCE TREE: a preset build tree (this editor's own for the
//!   desktop app, the mobile/web preset trees for the device targets), with the
//!   source tree beside it supplying the engine media and build scripts.
//! - COPIED as a distributed app: there is no repository and no build tree on
//!   the machine, so the engine payload rides INSIDE the app - the same staged
//!   `Media/` tree, player and texture cook tool the editor already renders and
//!   plays with (@see EditorResourcePaths.h). That app packages the host
//!   desktop app and says so plainly for anything else: an iOS or Android
//!   package needs that platform's player, which only a source build produces.
//!
//! The two are mutually exclusive BY CONSTRUCTION: a plan carries either an
//! `engineBuild` + `repoRoot` pair or a `bundleResources` + `bundleTools` pair,
//! never both, so the exporter and the payload can never come from two
//! different places (@see tools/exporter/ExportRun.h, which rejects the
//! contradiction at the other end of the same seam).
//!
//! Both the Build menu and the export verb of the control endpoint run through
//! here, so the sourcing and the refusals have ONE definition. The decision
//! is pure - it asks the filesystem nothing and reads no globals; the caller
//! resolves the paths (through the resource locator, never a baked constant)
//! and passes them in as facts, which is what makes the whole table testable
//! headlessly (EditorExportPlanTests).

namespace OrkigeEditor
{
	//! where an export takes its exporter and engine payload from
	enum class EditorExportSource
	{
		Tree,	//!< the source + build tree this editor was built in
		Bundle	//!< the payload the distributed app carries inside itself
	};

	//! @brief the facts a plan is made from. Every path arrives resolved: the
	//! planner concatenates and chooses, it never probes.
	struct EditorExportInputs
	{
		//! the requested platform ("macos", "ios-simulator", "ios", "android",
		//! "web")
		Orkige::String		platform;
		Orkige::String		projectRoot;
		//! does the project carry compiled C++ game code? (a native module
		//! needs the engine SDK tree and a C++ toolchain to build)
		bool				nativeModule = false;
		//! is the source tree this editor was built in still reachable, with a
		//! configured build tree in it?
		bool				engineTree = false;
		Orkige::String		engineRoot;			//!< the source tree root
		Orkige::String		engineBuildDir;		//!< this editor's build tree
		//! the physical-device iOS preset tree NAME (under `<engineRoot>/build`)
		Orkige::String		iosDeviceTree;
		//! the neutral engine app icon an export falls back to when the project
		//! sets no `export.icon` (EditorResourceLocator::defaultAppIcon)
		Orkige::String		defaultIcon;
		//! the staged payload's resource root (holds `Media/`) and tool root
		//! (holds the player + texture cook executables)
		Orkige::String		bundleResources;
		Orkige::String		bundleTools;
		bool				bundlePlayer = false;	//!< a player rides along
		bool				bundleMedia = false;	//!< ...and the engine media
		//! does the BROWSER player ride along too? A web build compiles
		//! nothing - the wasm player is a prebuilt artifact and the rest is
		//! bytes the exporter arranges - so a copied app that carries it can
		//! package for the browser on any host, unlike the device targets.
		bool				bundleWebPlayer = false;
		//! the desktop package this host produces (hostExportPlatform()), or
		//! "" where the exporter has no packaging target for it yet
		Orkige::String		hostPlatform;
		//! this host's name for the message when it has none ("Linux")
		Orkige::String		hostName;
	};

	//! @brief the answer: what one export run packages, or one honest sentence.
	//! The fields map 1:1 onto `OrkigeExport::ExportRequest`.
	struct EditorExportPlan
	{
		bool				ok = false;
		EditorExportSource	source = EditorExportSource::Tree;
		Orkige::String		platform;		//!< echoed back for the caller
		Orkige::String		projectRoot;
		//! the build tree this packages from (Tree source only) - the caller's
		//! own tree preconditions read it; empty for a Bundle plan
		Orkige::String		engineBuild;
		//! the engine SOURCE TREE the export resolves its beside-itself files
		//! from (media, module build scripts, the browser shell template).
		//! EMPTY for a Bundle plan - the app IS the payload and has no
		//! repository, which is what keeps the two sources from mixing.
		Orkige::String		repoRoot;
		//! the staged payload's roots (Bundle source only): the directory
		//! holding `Media/` and the one holding the sibling executables
		Orkige::String		bundleResources;
		Orkige::String		bundleTools;
		//! the neutral app icon a project with no `export.icon` gets ("" when
		//! this editor carries none - the export then ships no icon and says so)
		Orkige::String		defaultIcon;
		//! the directory the engine pieces come from, whichever source
		//! answered: the build tree, or the app's staged resource root. What a
		//! report shows a person ("packaged from ...").
		Orkige::String		enginePayload;
		//! empty while ok; a complete, actionable sentence otherwise
		Orkige::String		error;
	};

	//! @brief is @p platform one the exporter knows at all?
	bool isExportPlatform(Orkige::String const & platform);

	//! @brief the desktop package platform of the host the editor runs on
	//! ("macos"), or "" where the exporter has no packaging target yet.
	Orkige::String hostExportPlatform();

	//! @brief this host's display name for a refusal ("macOS", "Linux",
	//! "Windows") - one spelling, shared by the messages.
	Orkige::String hostExportName();

	//! @brief plan the export (@see EditorExportPlan.h). Never throws; a
	//! refusal is a plan with `ok == false` and an `error` that names the
	//! missing piece AND what to do about it.
	EditorExportPlan planProjectExport(EditorExportInputs const & inputs);
}

#endif //__EditorExportPlan_h__31_7_2026__09_00_00__
