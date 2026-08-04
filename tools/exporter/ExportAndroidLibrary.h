/********************************************************************
	created:	Tuesday 2026/08/04 at 10:00
	filename: 	ExportAndroidLibrary.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ExportAndroidLibrary_h__4_8_2026__10_00_00__
#define __ExportAndroidLibrary_h__4_8_2026__10_00_00__

#include "ExportPayload.h"

#include <core_util/String.h>

#include <utility>
#include <vector>

//! @file ExportAndroidLibrary.h
//! @brief the ANDROID LIBRARY ARCHIVE tier: what a project's `.aar`
//! dependencies contribute to the package being assembled, and what is refused
//! by name instead.
//!
//! An Android library archive is a zip carrying compiled Java (`classes.jar`
//! plus any `libs/*.jar`), a manifest fragment, and optionally resources,
//! assets and per-ABI native libraries. A project lists the ones it depends on
//! in the manifest Setting `export.android.libraries` (project-relative paths
//! to files it already has - nothing is downloaded and no dependency graph is
//! resolved), and each part is routed into the assembly that was already
//! there: the jars onto the compile classpath and the dex inputs, `jni/<abi>`
//! into the package's `lib/`, `assets/` and `res/` into the staged trees, and
//! the manifest fragment into the app manifest.
//!
//! @par The refusal rule
//! A silently dropped permission, activity or native library produces an app
//! that builds cleanly and then fails on a player's phone. So the merge
//! supports a NAMED SUBSET and refuses everything else BY NAME - the archive
//! and the element - rather than dropping it. @ref androidMergeManifest lists
//! the subset; anything outside it stops the export with a message that says
//! which archive and which element.
//!
//! @par Resource ids
//! A library whose code reads its own resources resolves them through a
//! generated `R` class that only the APP can produce, because the ids are
//! assigned when the whole resource table is linked. The assembly therefore
//! asks the resource linker for the library packages' `R` sources and compiles
//! them alongside its own Java - which is why the resource steps run BEFORE
//! the Java ones.
//!
//! Everything here that decides is PURE: the routing of an archive entry, the
//! manifest merge, the classpath composition. Only @ref unpackAndroidLibrary
//! touches a file.

namespace OrkigeExport
{
	//--- routing one archive entry -----------------------------

	//! @brief where one entry of a library archive belongs
	struct AndroidLibraryEntry
	{
		//! what the entry IS - the parts an assembly consumes, plus the two
		//! answers that are not a refusal: nothing to do, or not ours
		enum Kind
		{
			MANIFEST,	//!< `AndroidManifest.xml` - the fragment to merge
			JAR,		//!< `classes.jar` / `libs/*.jar` - compiled Java
			RESOURCE,	//!< `res/...` - compiled by the resource linker
			ASSET,		//!< `assets/...` - packaged verbatim
			NATIVE,		//!< `jni/<abi>/...` - the package's `lib/<abi>/`
			SYMBOLS,	//!< `R.txt` - the resource symbols the library needs
			IGNORED		//!< carries nothing this assembly consumes
		};

		Kind			kind = IGNORED;
		//! the path under the tree the entry is routed into ("values/values.xml"
		//! for a RESOURCE, "libfoo.so" for a NATIVE entry)
		Orkige::String	relative;
		//! the ABI a NATIVE entry is built for ("" for every other kind)
		Orkige::String	abi;
	};

	//! @brief route one archive entry name. PURE.
	//! @remarks a directory entry, and anything this assembly has no consumer
	//! for (the lint jar, the shrinker rules, the annotation archive, the
	//! signature block), comes back IGNORED - it is not dropped from something
	//! that would otherwise have used it.
	AndroidLibraryEntry androidLibraryEntry(Orkige::String const & entryName);

	//! @brief @p paths joined with this host's Java classpath separator. PURE.
	//! @remarks `;` on Windows and `:` everywhere else - a separator the JDK
	//! defines per platform, so composing one by hand is the only way to hand
	//! javac and the dexer a list they both read the same way.
	Orkige::String androidClasspath(std::vector<Orkige::String> const & paths);

	//--- merging the manifests ---------------------------------

	//! @brief one library's manifest, on its way into the app's
	struct AndroidManifestFragment
	{
		//! the archive it came from - what a refusal names
		Orkige::String	source;
		Orkige::String	text;
	};

	//! @brief merge every fragment into @p appManifest. PURE.
	//!
	//! With NO fragments @p out is @p appManifest byte for byte, so a project
	//! that depends on no library packages exactly the manifest it always did.
	//!
	//! The supported subset, merged and de-duplicated by `android:name`:
	//!  - `<uses-permission>`, `<uses-permission-sdk-23>`, `<permission>`
	//!  - `<uses-feature>` (a feature two libraries disagree about becomes
	//!    required, which is the platform's own rule)
	//!  - `<queries>` children (package visibility - dropping one silently
	//!    breaks intent resolution on newer platforms)
	//!  - inside `<application>`: `<activity>`, `<activity-alias>`,
	//!    `<service>`, `<receiver>`, `<provider>`, `<meta-data>`,
	//!    `<uses-library>`, `<uses-native-library>`, `<property>`
	//!  - `<uses-sdk>` is READ, not copied: a library that needs a newer
	//!    minimum than the app declares is refused rather than packaged into an
	//!    app the platform will let install and then crash.
	//!  - `${applicationId}` is substituted with @p applicationId, the one
	//!    placeholder the merge resolves.
	//!
	//! Everything else is refused BY NAME. Two declarations of the same thing
	//! are the same declaration only when they are IDENTICAL; where they differ
	//! the export stops rather than pick one.
	//!
	//! @param applicationId the app's own package - what `${applicationId}`
	//!        resolves to
	//! @param minimumApi the app's `minSdkVersion`, which no library may exceed
	//! @param notes receives one line per merged contribution, for the log
	bool androidMergeManifest(Orkige::String const & appManifest,
		std::vector<AndroidManifestFragment> const & fragments,
		Orkige::String const & applicationId, int minimumApi,
		Orkige::String & out, std::vector<Orkige::String> * notes,
		Orkige::String * error);

	//! @brief the `package` an Android manifest declares, or "". PURE.
	Orkige::String androidManifestPackage(Orkige::String const & manifestText);

	//--- unpacking ---------------------------------------------

	//! @brief what one library archive contributes, once unpacked
	struct AndroidLibrary
	{
		//! the archive's file name - what every message about it says
		Orkige::String	name;
		Orkige::String	path;			//!< where it was read from
		Orkige::String	packageName;	//!< the package its manifest declares
		Orkige::String	manifestText;	//!< the fragment to merge
		//! the unpacked `classes.jar` and `libs/*.jar`, in archive order
		std::vector<Orkige::String>	jars;
		Orkige::String	resDirectory;		//!< "" when it carries no resources
		Orkige::String	assetsDirectory;	//!< "" when it carries no assets
		//! the `jni/<abi>` libraries for the ABI being packaged, as
		//! (relative name, unpacked path) - staged into the package's `lib/`
		std::vector<std::pair<Orkige::String, Orkige::String> > nativeLibraries;
		//! does its code resolve resource ids through a generated `R` class?
		//! (it carries resource symbols, so the linker must generate one)
		bool			generatesResourceIds = false;
	};

	//! @brief unpack @p archivePath into @p workDirectory, keeping the parts
	//! this assembly consumes.
	//! @param abi the ABI the package is being built for - a library that
	//!        carries native code for other ABIs and not this one is refused
	//!        rather than packaged without the library it will try to load
	//! @param log receives one line per archive, naming what it contributed
	bool unpackAndroidLibrary(Orkige::String const & archivePath,
		Orkige::String const & workDirectory, Orkige::String const & abi,
		AndroidLibrary & out, ExportLog const & log, Orkige::String * error);
}

#endif //__ExportAndroidLibrary_h__4_8_2026__10_00_00__
