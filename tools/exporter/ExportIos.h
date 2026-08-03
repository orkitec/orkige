/********************************************************************
	created:	Saturday 2026/08/01 at 10:00
	filename: 	ExportIos.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportIos_h__1_8_2026__10_00_00__
#define __ExportIos_h__1_8_2026__10_00_00__

#include "ExportPayload.h"
#include "ExportProject.h"
#include "ExportSettings.h"

#include <core_debugnet/Json.h>
#include <core_util/String.h>

#include <vector>

//! @file ExportIos.h
//! @brief packaging a project for iOS - the simulator, a signed device build,
//! and the App Store `.ipa`.
//!
//! All three assemble the SAME bundle. An iOS build already produces a complete
//! `.app` carrying the engine's backend media - the player's, or (for a project
//! whose game code is compiled) the MODULE's, which is the same shape by the
//! same recipe (cmake/OrkigeTargetShape.cmake) - so an export copies that
//! bundle and adds to it: the engine's content media, the project payload and
//! its marker, the per-project icons, the privacy manifest and the identity
//! keys rewritten onto the built bundle's Info.plist. The bundle is FLAT (no
//! Contents/) - `SDL_GetBasePath()` on iOS is the bundle root, which is where
//! the marker and the payload have to sit.
//!
//! A NATIVE-MODULE project has no player to copy: the module is the runtime, so
//! it is BUILT here, against an iOS SDK pack (Docs/sdk-pack.md) - the only form
//! of the engine that exists on a machine with no engine checkout.
//!
//! What separates the three is only what happens after assembly:
//!
//!   ios-simulator  nothing. A simulator runs unsigned code.
//!   ios            codesign inside-out with a DEVELOPMENT identity and an
//!                  embedded provisioning profile - hardware refuses to
//!                  install anything less.
//!   ios-ipa        the same, with a DISTRIBUTION identity (entitlements drop
//!                  get-task-allow, which the App Store rejects), then zipped
//!                  under `Payload/` - the upload container.
//!
//! The signing material is machine-local and never committed (@see
//! ExportSettings.h); only the Team ID travels in the manifest. The codesign
//! ARGUMENT COMPOSITION is exposed as pure builders so it is asserted on a
//! machine that holds no certificate - what actually breaks a signed build is
//! a missing flag, not the cryptography.

namespace OrkigeExport
{
	//! the target an iOS SIMULATOR SDK pack declares itself built for
	//! (ORKIGE_SDK_TARGET_PLATFORM; @see cmake/OrkigeTargetShape.cmake, which
	//! both sides of that name read)
	extern const char * const IOS_SIMULATOR_PLATFORM;

	//! @brief the keys an export rewrites onto the prebuilt player's
	//! Info.plist: the project's identity, its icon list and the fixed
	//! declarations. PURE - the plist edit itself is a separate step, so the
	//! decision is testable without a bundle.
	//! @param iconNames the loose icon FILE names (with `.png`); the plist
	//!        lists them without the extension, the way iOS resolves them
	Orkige::JsonValue iosInfoPlistKeys(ExportProject const & project,
		Orkige::String const & bundleId,
		std::vector<Orkige::String> const & iconNames);

	//! @brief the archive name of @p filePath (inside the bundle @p appDirectory)
	//! within an `.ipa`: `Payload/<App>.app/<relative path>`. An `.ipa` is a zip
	//! whose ONE top-level directory is `Payload/`.
	Orkige::String ipaArchiveName(Orkige::String const & appDirectory,
		Orkige::String const & filePath);

	//! @brief the `codesign` call for one nested binary (a bundled dylib or
	//! framework), which must be signed BEFORE the bundle that contains it
	std::vector<Orkige::String> codesignNestedArguments(
		Orkige::String const & identity, Orkige::String const & path);

	//! @brief the `codesign` call for the app bundle itself: the identity plus
	//! the entitlements this build's signature seals in.
	//! @remarks `--generate-entitlement-der` is not optional - a bundle signed
	//! without the DER representation is rejected by current iOS.
	std::vector<Orkige::String> codesignBundleArguments(
		Orkige::String const & identity,
		Orkige::String const & entitlementsPath,
		Orkige::String const & bundleDirectory);

	//! @brief what an iOS export packages beyond the shared assembly
	struct IosRequest
	{
		//! the resolved identity + profile; both empty = the simulator
		SigningPair	signing;
		//! App Store signing: entitlements drop get-task-allow
		bool		distribution = false;
		//! wrap the signed bundle into an `.ipa` under `Payload/`
		bool		packageIpa = false;
	};

	//! @brief package @p project into a copy of the iOS player bundle the
	//! given build tree carries.
	//! @param outArtifact receives the `.app` (or the `.ipa` when one is
	//!        asked for)
	//! @return false with an honest @p error naming the missing piece
	bool exportIos(ExportProject const & project, EngineSource const & source,
		Orkige::String const & outputDirectory, IosRequest const & request,
		ExportEnvironment const & environment, Orkige::String & outArtifact,
		Orkige::String * error);
}

#endif //__ExportIos_h__1_8_2026__10_00_00__
