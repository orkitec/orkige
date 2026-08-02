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
		EditorExportPlan plan;
		plan.platform = inputs.platform;
		plan.projectRoot = inputs.projectRoot;
		plan.defaultIcon = inputs.defaultIcon;
		if(inputs.engineTree)
		{
			// the developer shape: package a preset build tree, with the source
			// tree beside it supplying the engine media and the build scripts
			plan.source = EditorExportSource::Tree;
			plan.engineBuild = treeFor(inputs);
			plan.repoRoot = inputs.engineRoot;
			plan.enginePayload = plan.engineBuild;
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
		// a DEVICE target is the same shape one step further out: its player is
		// not carried but FETCHED, so a copied app packages for it as soon as
		// that download is installed. The caller resolved both the directory
		// and - when there is none - the sentence to show.
		const bool deviceFromPayload = !inputs.devicePayload.empty();
		if(!deviceFromPayload && !inputs.devicePayloadProblem.empty())
		{
			return refuse(inputs.devicePayloadProblem);
		}
		if(!webFromBundle && !deviceFromPayload && inputs.hostPlatform.empty())
		{
			return refuse("this Orkige runs on " + inputs.hostName + ", where "
				"project export has no packaging target yet - export from an "
				"Orkige running on " + platformLabel("macos") + ", or build "
				"the game's player from the engine source tree");
		}
		if(!webFromBundle && !deviceFromPayload &&
			inputs.platform != inputs.hostPlatform)
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
			// compiled game code needs an engine to build against, which a
			// downloaded Orkige has as an INSTALLED SDK PACK rather than as a
			// checkout (Docs/sdk-pack.md). The resolution and the two refusals
			// it can produce are one seam, so the sentence arrives ready-made.
			if(!inputs.nativeProblem.empty())
			{
				return refuse(inputs.nativeProblem);
			}
			plan.sdkPack = inputs.sdkPack;
			plan.moduleCmake = inputs.moduleCmake;
			plan.moduleMakeProgram = inputs.moduleMakeProgram;
		}
		// the two roots the resource locator answered with: the staged Media/
		// tree and the executables beside the editor. `repoRoot` stays EMPTY -
		// this app IS the engine source, and a second one would only supply the
		// same files from a directory that exists on nobody's machine.
		plan.bundleResources = inputs.bundleResources;
		plan.bundleTools = inputs.bundleTools;
		plan.devicePayload = inputs.devicePayload;
		plan.enginePayload = deviceFromPayload
			? inputs.devicePayload : inputs.bundleResources;
		plan.ok = true;
		return plan;
	}
}
