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

#include <vector>

//! @file EditorExportPlan.h
//! @brief the ONE decision behind "package this project": which exporter runs,
//! what it packages from, and - when it cannot run - the one sentence that says
//! why in terms the person reading it can act on.
//!
//! Two shapes of editor ask for an export and they carry different things:
//! - built from the SOURCE TREE: the tree's `Util/orkige_export.py` packages a
//!   preset build tree (this editor's own for the desktop app, the mobile/web
//!   preset trees for the device targets).
//! - COPIED as a distributed app: there is no repository and no build tree on
//!   the machine, so the exporter and the engine payload both ride INSIDE the
//!   app - the same staged `Media/` tree, player and texture cook tool the
//!   editor already renders and plays with (@see EditorResourcePaths.h). That
//!   app packages the host desktop app and says so plainly for anything else:
//!   an iOS or Android package needs that platform's player, which only a
//!   source build produces.
//!
//! Both the Build menu and the export verb of the control endpoint run through
//! here, so the command line and the refusals have ONE definition. The decision
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
		//! the resolved exporter script (EditorResourceLocator::pythonTool)
		EditorResourcePath	exporter;
		//! is the source tree this editor was built in still reachable, with a
		//! configured build tree in it?
		bool				engineTree = false;
		Orkige::String		engineRoot;			//!< the source tree root
		Orkige::String		engineBuildDir;		//!< this editor's build tree
		//! the physical-device iOS preset tree NAME (under `<engineRoot>/build`)
		Orkige::String		iosDeviceTree;
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

	//! @brief the answer: an exporter command line, or one honest sentence.
	struct EditorExportPlan
	{
		bool				ok = false;
		EditorExportSource	source = EditorExportSource::Tree;
		//! the exporter arguments, interpreter NOT included (the caller
		//! prepends the python3 its own preflight resolved)
		std::vector<Orkige::String> arguments;
		//! the build tree this packages from (Tree source only) - the caller's
		//! own tree preconditions read it; empty for a Bundle plan
		Orkige::String		engineBuild;
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
