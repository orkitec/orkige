/********************************************************************
	created:	Sunday 2026/08/03 at 10:00
	filename: 	ExportAndroidAssemble.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportAndroidAssemble_h__3_8_2026__10_00_00__
#define __ExportAndroidAssemble_h__3_8_2026__10_00_00__

#include "ExportPayload.h"
#include "ExportProcess.h"
#include "ExportSettings.h"

#include <core_util/String.h>

#include <vector>

//! @file ExportAndroidAssemble.h
//! @brief assembling an Android package - the APK and the App Bundle - out of
//! the Android SDK's own programs, spawned DIRECTLY as argv.
//!
//! An Android package is a zip with a compiled manifest, a resource table, a
//! dex image, the native library and the assets. Producing one is a fixed
//! choreography over five SDK programs (`aapt2`, `zipalign`, `d8`,
//! `apksigner`, plus a JDK's `javac`/`keytool`), and everything BETWEEN them -
//! staging trees, substituting the manifest, writing the resource XML, listing
//! the bundled files, building the zip - is file work this library does itself.
//!
//! @par No shell, ever
//! Every program is spawned as its own argv through the ONE `ProcessRunner`
//! seam: `argv[0]` is an absolute path to the program, and the arguments reach
//! it verbatim. Nothing is handed to a command interpreter, so there is no
//! quoting layer to get wrong, no PATH lookup to differ per machine, and no
//! host that needs an interpreter installed before it can package a game. The
//! whole command set one run spawns is decided UP FRONT by the pure
//! @ref androidAssemblyPlan, which is what lets that property be asserted.
//!
//! @par Two engine sources, one assembly
//! The player, the engine media, the manifest template and the Java glue come
//! either from a preset BUILD TREE (the developer case) or from a fetched
//! device PAYLOAD, which is what an editor with no repository packages from
//! (Docs/device-payloads.md). Everything after the sourcing is the same code.
//!
//! @par What stays the machine's
//! The Android SDK build tools and a JDK. They are a TOOLCHAIN, and we ship the
//! engine, never a toolchain - so each program is resolved by name and each
//! missing one is reported with what installs it (@ref androidToolchainGaps).

namespace OrkigeExport
{
	struct AndroidToolchain;

	//--- the commands ------------------------------------------

	//! @brief one program the assembly spawns.
	//! @remarks `arguments[0]` is the PROGRAM - an absolute path to an SDK
	//! tool or a JDK program, never a command interpreter and never a bare
	//! name resolved through PATH.
	struct AndroidCommand
	{
		//! the progress line this step reports itself as ("aapt2 link")
		Orkige::String				label;
		//! argv, argument 0 being the program
		std::vector<Orkige::String>	arguments;

		//! is this step absent from the plan (a strip that is not needed, a
		//! signing step a module-only bundle never reaches)?
		bool empty() const { return this->arguments.empty(); }
		//! the program, or "" for an absent step
		Orkige::String program() const
		{
			return this->arguments.empty() ? Orkige::String()
				: this->arguments[0];
		}
	};

	//! @brief `<directory>/<name>`, with the host's program suffix - the way
	//! an SDK tool is named on this machine. PURE.
	Orkige::String androidProgramPath(Orkige::String const & directory,
		Orkige::String const & name);

	//--- the plan ----------------------------------------------

	//! @brief every path and choice one assembly is laid out with. Everything
	//! here is already RESOLVED, which is what makes the planner pure: the
	//! probing happens once, in the assembler, and the plan is a function of
	//! its answers.
	struct AndroidAssemblyLayout
	{
		//--- the machine's toolchain
		Orkige::String	buildTools;		//!< `<sdk>/build-tools/<version>`
		Orkige::String	platformJar;	//!< `<sdk>/platforms/<api>/android.jar`
		Orkige::String	javaHome;		//!< the JDK javac/java/keytool come from
		//! the NDK strip for a build tree's unstripped library ("" when the
		//! library already ships stripped, which is what a payload carries)
		Orkige::String	strip;
		//! the oldest API the dex is built for
		int				minimumApi = 28;

		//--- what is being packaged
		Orkige::String	nativeLibrary;		//!< the `libmain.so` to package
		Orkige::String	stagedLibrary;		//!< where it lands under the stage
		std::vector<Orkige::String>	javaSources;	//!< the `.java` files to compile

		//--- the work layout
		Orkige::String	classesDirectory;	//!< javac output
		Orkige::String	classListFile;		//!< the `@file` d8 reads
		Orkige::String	dexDirectory;		//!< d8 output
		Orkige::String	resDirectory;		//!< the composed `res/` tree
		Orkige::String	compiledResources;	//!< the aapt2 compile output zip
		Orkige::String	manifestPath;		//!< the substituted manifest
		Orkige::String	linkedDirectory;	//!< the aapt2 link output directory
		Orkige::String	unalignedPackage;	//!< the APK before zipalign

		//--- the artifact
		Orkige::String	outputPath;			//!< the .apk / .aab / module .zip
		bool			bundle = false;		//!< an App Bundle, not an APK
		bool			moduleOnly = false;	//!< stop at the unsigned module zip

		//--- APK signing (the shared debug key, created on demand)
		Orkige::String	debugKeystore;

		//--- App Bundle signing (the release upload key)
		Orkige::String	moduleZip;			//!< the bundletool module input
		Orkige::String	bundleConfig;		//!< "" = bundletool's own defaults
		Orkige::String	bundlePath;			//!< the .aab before signing
		Orkige::String	bundletool;			//!< the bundletool jar
		Orkige::String	releaseKeystore;
		Orkige::String	releaseKeyAlias;
		//! the environment variables jarsigner reads the two passwords from -
		//! NAMES, never values, so no secret ever reaches a command line
		Orkige::String	storePasswordEnv;
		Orkige::String	keyPasswordEnv;
	};

	//! @brief every program one assembly spawns, in the order it spawns them.
	//! @remarks a step that this package shape does not need carries no
	//! arguments (@ref AndroidCommand::empty).
	struct AndroidAssemblyPlan
	{
		AndroidCommand	strip;				//!< NDK llvm-strip
		AndroidCommand	compileJava;		//!< javac
		AndroidCommand	dex;				//!< d8
		AndroidCommand	compileResources;	//!< aapt2 compile
		AndroidCommand	linkResources;		//!< aapt2 link
		AndroidCommand	align;				//!< zipalign (APK)
		AndroidCommand	createDebugKey;		//!< keytool (APK, on demand)
		AndroidCommand	sign;				//!< apksigner (APK)
		AndroidCommand	buildBundle;		//!< bundletool (signed .aab)
		AndroidCommand	signBundle;			//!< jarsigner (signed .aab)
		AndroidCommand	verifyBundle;		//!< jarsigner -verify

		//! @brief the non-empty steps, in run order - what a caller sweeps to
		//! assert a property of the WHOLE run
		std::vector<AndroidCommand> all() const;
	};

	//! @brief lay out every program one assembly runs. PURE.
	AndroidAssemblyPlan androidAssemblyPlan(
		AndroidAssemblyLayout const & layout);

	//! @brief extract SDL's Java glue out of the verified source archive vcpkg
	//! downloaded - the BUILD-TREE fallback for a tree whose vcpkg buildtrees
	//! were cleaned. PURE.
	//! @remarks a fetched device payload carries the glue outright, so this
	//! never runs in a distributed editor; it belongs to a machine that has a
	//! vcpkg root, and `tar` is a program on every host that does.
	AndroidCommand androidExtractJavaGlueCommand(
		Orkige::String const & archive, Orkige::String const & destination);

	//--- the generated text ------------------------------------

	//! @brief is @p colour a `#RRGGBB` literal? PURE.
	bool isAndroidLaunchColour(Orkige::String const & colour);

	//! @brief the `res/values/colors.xml` carrying the cold-start window
	//! background. PURE.
	Orkige::String androidLaunchColoursXml(Orkige::String const & colour);

	//! @brief the `res/values/styles.xml` naming the launch theme. PURE.
	Orkige::String androidLaunchStylesXml();

	//! @brief the bundletool `BundleConfig.json` that keeps the asset entries
	//! UNCOMPRESSED in the APKs Play generates, so the installed app mounts and
	//! reads them in place. PURE.
	Orkige::String androidBundleConfigJson();

	//! @brief the `orkige_assets.txt` extraction manifest - one bundled file
	//! per line, in the order given. PURE.
	Orkige::String androidAssetManifest(
		std::vector<Orkige::String> const & relativePaths);

	//! @brief Google Play's current minimum `targetSdkVersion` for uploads. A
	//! build below it packages, but Play rejects it.
	int androidPlayTargetSdkFloor();

	//! @brief the `android:targetSdkVersion` a manifest declares, or 0. PURE.
	int androidManifestTargetSdk(Orkige::String const & manifestText);

	//! @brief what a packaged manifest says that the checked-in template does
	//! not. An empty field leaves the template's own value alone, so a bare
	//! player run keeps the template byte-identical.
	struct AndroidManifestEdits
	{
		Orkige::String	package;			//!< the application id
		Orkige::String	label;				//!< the app label
		//! `android:screenOrientation`; "" leaves the activity unconstrained
		Orkige::String	screenOrientation;
		//! link the launcher icon + launch theme (only resolvable once a
		//! staged `res/` tree is linked in)
		bool			launcherResources = false;
		//! a release artifact is not debuggable
		bool			release = false;
		int				versionCode = 0;	//!< 0 leaves the template's
		Orkige::String	versionName;		//!< "" leaves the template's
	};

	//! @brief @p templateText with @p edits applied. PURE - the substitution
	//! the packaged manifest is, with no text processor in the loop.
	Orkige::String androidManifestText(Orkige::String const & templateText,
		AndroidManifestEdits const & edits);

	//--- the assembly ------------------------------------------

	//! @brief what one package assembly packages
	struct AndroidPackageRequest
	{
		//--- engine source (exactly one of the two)
		//! the preset build tree the player was built in
		Orkige::String	buildDirectory;
		//! the fetched device payload the player travelled in
		Orkige::String	devicePayload;
		//! the engine source tree (a build tree's samples + engine media)
		Orkige::String	repoRoot;
		//! where vcpkg keeps its buildtrees, for a build tree's SDL Java glue
		Orkige::String	vcpkgRoot;

		//--- what goes in
		//! the staged project payload ("" = the bare player, whose own demo
		//! content ships instead)
		Orkige::String	projectPayload;
		//! a staged `res/` tree of launcher mipmaps ("" = no launcher icon,
		//! and the manifest keeps the framework theme)
		Orkige::String	launcherResources;
		Orkige::String	package;		//!< "" keeps the template's
		Orkige::String	label;			//!< "" keeps the template's
		Orkige::String	launchColour = "#12161f";
		//! "stored" (assets uncompressed, mounted in place) or "compressed"
		Orkige::String	assetsMode = "stored";
		//! the activity's `android:screenOrientation` ("" = unconstrained)
		Orkige::String	screenOrientation;

		//--- the artifact
		Orkige::String	outputPath;		//!< the .apk / .aab / module .zip
		//! the intermediates directory ("" = `<output dir>/apk-work`)
		Orkige::String	workDirectory;
		bool			bundle = false;
		bool			moduleOnly = false;
		int				versionCode = 0;
		Orkige::String	versionName;
		Orkige::String	bundletool;
		AndroidKeystore	keystore;

		//--- the machine
		EnvironmentMap	environment;
		ExportLog		log;
		ProcessRunner	runner;
	};

	//! @brief assemble the package @p request describes.
	//! @param tools the resolved machine toolchain, handed DOWN rather than
	//!        probed again: the programs the export checked for and reported on
	//!        are then exactly the ones that run.
	//! @param outArtifact receives the packaged file on success
	bool assembleAndroidPackage(AndroidPackageRequest const & request,
		AndroidToolchain const & tools, Orkige::String & outArtifact,
		Orkige::String * error);
}

#endif //__ExportAndroidAssemble_h__3_8_2026__10_00_00__
