/********************************************************************
	created:	Friday 2026/07/31 at 09:00
	filename: 	EditorExportPlan.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "EditorExportPlan.h"

namespace OrkigeEditor
{
	namespace
	{
		//! the desktop package the exporter writes on this host (the ONE place
		//! the host/platform pairing lives). Empty where the exporter has no
		//! packaging target for the host yet - the honest answer, not a guess.
#ifdef __APPLE__
		const char* const HOST_PLATFORM = "macos";
		const char* const HOST_NAME = "macOS";
#elif defined(_WIN32)
		const char* const HOST_PLATFORM = "";
		const char* const HOST_NAME = "Windows";
#else
		const char* const HOST_PLATFORM = "";
		const char* const HOST_NAME = "Linux";
#endif

		//! a platform's name as a person says it, for the refusal messages
		Orkige::String platformLabel(Orkige::String const & platform)
		{
			if(platform == "ios" || platform == "ios-simulator")
			{
				return "iOS";
			}
			if(platform == "android")
			{
				return "Android";
			}
			if(platform == "web")
			{
				return "the browser";
			}
			if(platform == "macos")
			{
				return "macOS";
			}
			return platform;
		}
		//---------------------------------------------------------
		//! @brief the preset build tree a Tree-source export packages from:
		//! this editor's own for the desktop app, the platform's preset tree
		//! for a device or browser target (the exporter reports honestly when
		//! one of those was never built).
		//! @remarks The device trees are THIS editor's render flavor - a
		//! next-flavored editor packages the Ogre-Next player, a classic one
		//! the classic player - which is what the flavored tree name carries.
		Orkige::String treeFor(EditorExportInputs const & inputs)
		{
			const Orkige::String build = inputs.engineRoot + "/build/";
			if(inputs.platform == "ios-simulator")
			{
				return build + "ios-simulator-debug";
			}
			if(inputs.platform == "ios")
			{
				return build + inputs.iosDeviceTree;
			}
			if(inputs.platform == "android")
			{
				return build + "android-debug";
			}
			if(inputs.platform == "web")
			{
				return build + "web-release";
			}
			return inputs.engineBuildDir;
		}
		//---------------------------------------------------------
		EditorExportPlan refuse(Orkige::String const & reason)
		{
			EditorExportPlan plan;
			plan.error = reason;
			return plan;
		}
	}
	//---------------------------------------------------------
	bool isExportPlatform(Orkige::String const & platform)
	{
		return platform == "macos" || platform == "ios-simulator" ||
			platform == "ios" || platform == "android" || platform == "web";
	}
	//---------------------------------------------------------
	Orkige::String hostExportPlatform()
	{
		return HOST_PLATFORM;
	}
	//---------------------------------------------------------
	Orkige::String hostExportName()
	{
		return HOST_NAME;
	}
	//---------------------------------------------------------
	EditorExportPlan planProjectExport(EditorExportInputs const & inputs)
	{
		if(!isExportPlatform(inputs.platform))
		{
			return refuse("'" + inputs.platform + "' is not a platform this "
				"editor exports for (macos, ios-simulator, ios, android, web)");
		}
		if(!inputs.exporter.found())
		{
			// neither staged nor reachable in a source tree: nothing to run,
			// and a path that is not there would only fail opaquely later
			return refuse("this copy of Orkige carries no project exporter "
				"(Util/orkige_export.py) and no engine source tree is "
				"reachable from it - reinstall Orkige, or run an editor built "
				"from the source tree");
		}
		EditorExportPlan plan;
		plan.arguments = { inputs.exporter.path, "--project",
			inputs.projectRoot, "--platform", inputs.platform };
		if(inputs.engineTree)
		{
			// the developer shape: package a preset build tree, exactly as the
			// exporter is driven from a shell
			plan.source = EditorExportSource::Tree;
			plan.engineBuild = treeFor(inputs);
			plan.enginePayload = plan.engineBuild;
			plan.arguments.push_back("--engine-build");
			plan.arguments.push_back(plan.engineBuild);
			plan.ok = true;
			return plan;
		}
		// the distributed shape: the app itself is the engine payload
		plan.source = EditorExportSource::Bundle;
		if(!inputs.bundlePlayer || !inputs.bundleMedia ||
			inputs.bundleResources.empty() || inputs.bundleTools.empty())
		{
			return refuse("this copy of Orkige carries no engine payload to "
				"package (its bundled player and engine media are missing) and "
				"no engine build tree is reachable from it - reinstall Orkige");
		}
		// the browser is the one target a copied app can package for on ANY
		// host: nothing is compiled, the wasm player is a prebuilt artifact
		// staged inside the app, and everything else is bytes the exporter
		// arranges. Without that player staged it falls through to the
		// platform refusal below, which says what is missing.
		const bool webFromBundle =
			inputs.platform == "web" && inputs.bundleWebPlayer;
		if(!webFromBundle && inputs.hostPlatform.empty())
		{
			return refuse("this Orkige runs on " + inputs.hostName + ", where "
				"project export has no packaging target yet - export from an "
				"Orkige running on " + platformLabel("macos") + ", or build "
				"the game's player from the engine source tree");
		}
		if(!webFromBundle && inputs.platform != inputs.hostPlatform)
		{
			const Orkige::String label = platformLabel(inputs.platform);
			const Orkige::String host = platformLabel(inputs.hostPlatform);
			return refuse("packaging for " + label + " needs the " + label +
				" player, which only an Orkige built from the engine source "
				"tree produces - this app carries the " + host + " player "
				"alone. Export for " + host + ", or build Orkige from source "
				"to package for " + label);
		}
		if(inputs.nativeModule)
		{
			return refuse("this project builds compiled C++ game code (its "
				"native.target setting), which needs the engine source tree "
				"and a C++ toolchain to build - a downloaded Orkige carries "
				"neither. Projects whose behaviour is Lua scripts export as "
				"they are");
		}
		// the two roots the resource locator answered with: the staged Media/
		// tree and the executables beside the editor
		plan.arguments.push_back("--engine-bundle");
		plan.arguments.push_back(inputs.bundleResources);
		plan.arguments.push_back("--engine-tools");
		plan.arguments.push_back(inputs.bundleTools);
		plan.enginePayload = inputs.bundleResources;
		plan.ok = true;
		return plan;
	}
}
