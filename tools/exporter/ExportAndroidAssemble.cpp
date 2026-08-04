/********************************************************************
	created:	Sunday 2026/08/03 at 10:00
	filename: 	ExportAndroidAssemble.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportAndroidAssemble.h"

#include "ExportAndroid.h"
#include "ExportBuildTree.h"
#include "ExportFiles.h"
#include "ExportPayload.h"
#include "ExportSettings.h"
#include "ExportZip.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace OrkigeExport
{
	namespace
	{
		//! the packaged player's own identity in the checked-in template - the
		//! literals a project export substitutes away
		const char * const TEMPLATE_PACKAGE = "com.orkitec.orkigeplayer";
		const char * const TEMPLATE_LABEL = "Orkige Player";
		//! the template's framework theme, replaced when a res/ tree is linked
		const char * const TEMPLATE_THEME =
			"android:theme=\"@android:style/Theme.NoTitleBar.Fullscreen\"";
		//! Google Play's current minimum targetSdkVersion for uploads
		const int PLAY_TARGET_SDK_FLOOR = 35;

		bool report(Orkige::String * error, Orkige::String const & message)
		{
			if(error != 0)
			{
				*error = message;
			}
			return false;
		}
		//---------------------------------------------------------
		void emit(ExportLog const & log, Orkige::String const & message)
		{
			if(log)
			{
				log(message);
			}
		}
		//---------------------------------------------------------
		//! @p text with every occurrence of @p from replaced by @p to
		Orkige::String replaceAll(Orkige::String const & text,
			Orkige::String const & from, Orkige::String const & to)
		{
			if(from.empty())
			{
				return text;
			}
			Orkige::String out;
			std::size_t begin = 0;
			for(;;)
			{
				const std::size_t found = text.find(from, begin);
				if(found == Orkige::String::npos)
				{
					out += text.substr(begin);
					return out;
				}
				out += text.substr(begin, found - begin);
				out += to;
				begin = found + from.size();
			}
		}
		//---------------------------------------------------------
		//! an XML attribute's value in @p text (`name="value"`), or ""
		Orkige::String attributeValue(Orkige::String const & text,
			Orkige::String const & name)
		{
			const Orkige::String needle = name + "=\"";
			const std::size_t found = text.find(needle);
			if(found == Orkige::String::npos)
			{
				return Orkige::String();
			}
			const std::size_t begin = found + needle.size();
			const std::size_t end = text.find('"', begin);
			if(end == Orkige::String::npos)
			{
				return Orkige::String();
			}
			return text.substr(begin, end - begin);
		}
		//---------------------------------------------------------
		//! @p name in @p environment, or "" (whitespace-only reads as absent)
		Orkige::String lookupEnvironment(EnvironmentMap const & environment,
			char const * name)
		{
			EnvironmentMap::const_iterator found = environment.find(name);
			if(found == environment.end())
			{
				return Orkige::String();
			}
			Orkige::String const & value = found->second;
			for(std::size_t index = 0; index < value.size(); ++index)
			{
				if(std::isspace(static_cast<unsigned char>(value[index])) == 0)
				{
					return value;
				}
			}
			return Orkige::String();
		}
		//---------------------------------------------------------
		//! the immediate entry names of @p path, sorted ("" when absent - a
		//! missing directory is an answer here, not an error)
		std::vector<Orkige::String> entryNames(Orkige::String const & path,
			bool directoriesOnly)
		{
			std::vector<Orkige::String> names;
			std::error_code ignored;
			const std::filesystem::directory_iterator end;
			std::filesystem::directory_iterator entry(
				std::filesystem::path(path), ignored);
			if(ignored)
			{
				return names;
			}
			for(; entry != end; entry.increment(ignored))
			{
				if(ignored)
				{
					break;
				}
				if(directoriesOnly && !entry->is_directory(ignored))
				{
					continue;
				}
				names.push_back(entry->path().filename().string());
			}
			std::sort(names.begin(), names.end());
			return names;
		}
		//---------------------------------------------------------
		bool endsWith(Orkige::String const & text, Orkige::String const & tail)
		{
			return text.size() >= tail.size() &&
				text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
		}
		//---------------------------------------------------------
		bool beginsWith(Orkige::String const & text,
			Orkige::String const & head)
		{
			return text.size() >= head.size() &&
				text.compare(0, head.size(), head) == 0;
		}
		//---------------------------------------------------------
		//! every `.java` file under @p root, as absolute paths, sorted - the
		//! deterministic compile order (javac's own output does not depend on
		//! it, but a reproducible command line does)
		std::vector<Orkige::String> javaSourcesUnder(Orkige::String const & root)
		{
			std::vector<Orkige::String> sources;
			for(Orkige::String const & relative :
				ExportFiles::listFilesRecursive(root))
			{
				if(endsWith(relative, ".java"))
				{
					sources.push_back(ExportFiles::join(root, relative));
				}
			}
			return sources;
		}
		//---------------------------------------------------------
		//! add every regular file under @p root to @p zip beneath @p prefix.
		//! @param stored the entry names (after the prefix) that must go in
		//!        UNCOMPRESSED; an empty list deflates everything
		bool addTree(ExportZip & zip, Orkige::String const & root,
			Orkige::String const & prefix, bool storeAll,
			std::vector<Orkige::String> const & storedNames,
			Orkige::String * error)
		{
			for(Orkige::String const & relative :
				ExportFiles::listFilesRecursive(root))
			{
				const bool store = storeAll ||
					std::find(storedNames.begin(), storedNames.end(),
						relative) != storedNames.end();
				if(!zip.addFile(prefix + relative,
					ExportFiles::join(root, relative),
					store ? ExportZip::METHOD_STORE : ExportZip::METHOD_DEFLATE,
					error))
				{
					return false;
				}
			}
			return true;
		}
	}
	//---------------------------------------------------------
	Orkige::String androidProgramPath(Orkige::String const & directory,
		Orkige::String const & name)
	{
#if defined(_WIN32)
		// the SDK ships its native programs as .exe on Windows; a JDK's are
		// named the same way. Naming the file rather than trusting a PATH
		// lookup is the whole point of resolving a toolchain first.
		return ExportFiles::join(directory, name + ".exe");
#else
		return ExportFiles::join(directory, name);
#endif
	}
	//---------------------------------------------------------
	std::vector<AndroidCommand> AndroidAssemblyPlan::all() const
	{
		const AndroidCommand steps[] = {
			this->strip, this->compileJava, this->dex, this->compileResources,
			this->linkResources, this->align, this->createDebugKey, this->sign,
			this->buildBundle, this->signBundle, this->verifyBundle
		};
		std::vector<AndroidCommand> ordered;
		for(AndroidCommand const & step : steps)
		{
			if(!step.empty())
			{
				ordered.push_back(step);
			}
		}
		return ordered;
	}
	//---------------------------------------------------------
	AndroidAssemblyPlan androidAssemblyPlan(
		AndroidAssemblyLayout const & layout)
	{
		AndroidAssemblyPlan plan;
		const Orkige::String javaBin = ExportFiles::join(layout.javaHome, "bin");
		const Orkige::String java = androidProgramPath(javaBin, "java");

		if(!layout.strip.empty())
		{
			// a build tree's debug library carries hundreds of MB of DWARF;
			// the symbols stay in the tree for ndk-stack
			plan.strip.label = "stripping libmain.so";
			plan.strip.arguments = { layout.strip, "--strip-unneeded", "-o",
				layout.stagedLibrary, layout.nativeLibrary };
		}

		// -source/-target 8 + -bootclasspath is the only combination javac
		// still accepts a custom bootclasspath for, which is what keeps java.*
		// resolving against android.jar instead of the host JDK. d8 consumes
		// Java 8 bytecode happily.
		plan.compileJava.label = "compiling the Java glue";
		plan.compileJava.arguments = { androidProgramPath(javaBin, "javac"),
			"-source", "8", "-target", "8", "-encoding", "UTF-8",
			"-bootclasspath", layout.platformJar,
			"-d", layout.classesDirectory, "-nowarn" };
		plan.compileJava.arguments.insert(plan.compileJava.arguments.end(),
			layout.javaSources.begin(), layout.javaSources.end());

		plan.dex.label = "dexing";
		plan.dex.arguments = { java, "-cp",
			ExportFiles::join(layout.buildTools, "lib/d8.jar"),
			"com.android.tools.r8.D8", "--release",
			"--min-api", std::to_string(layout.minimumApi),
			"--lib", layout.platformJar,
			"--output", layout.dexDirectory,
			"@" + layout.classListFile };

		plan.compileResources.label = "aapt2 compile (res)";
		plan.compileResources.arguments = {
			androidProgramPath(layout.buildTools, "aapt2"), "compile",
			"--dir", layout.resDirectory, "-o", layout.compiledResources };

		// the linked output goes to a DIRECTORY rather than a zip: the package
		// is assembled here, from those files plus the staged tree, so every
		// entry's compression is this library's decision (a mounted APK reads
		// its assets in place, which a deflated entry cannot offer)
		plan.linkResources.label = layout.bundle
			? "aapt2 link (protobuf)" : "aapt2 link";
		plan.linkResources.arguments = {
			androidProgramPath(layout.buildTools, "aapt2"), "link" };
		if(layout.bundle)
		{
			plan.linkResources.arguments.push_back("--proto-format");
		}
		plan.linkResources.arguments.push_back("--manifest");
		plan.linkResources.arguments.push_back(layout.manifestPath);
		plan.linkResources.arguments.push_back("-I");
		plan.linkResources.arguments.push_back(layout.platformJar);
		plan.linkResources.arguments.push_back("-o");
		plan.linkResources.arguments.push_back(layout.linkedDirectory);
		plan.linkResources.arguments.push_back("--output-to-dir");
		plan.linkResources.arguments.push_back(layout.compiledResources);

		if(!layout.bundle)
		{
			plan.align.label = "zipalign";
			plan.align.arguments = {
				androidProgramPath(layout.buildTools, "zipalign"), "-f", "4",
				layout.unalignedPackage, layout.outputPath };

			// the shared Android debug key, created on demand: Android installs
			// no unsigned package, so signing was never optional
			plan.createDebugKey.label = "creating the debug keystore";
			plan.createDebugKey.arguments = {
				androidProgramPath(javaBin, "keytool"), "-genkeypair",
				"-keystore", layout.debugKeystore,
				"-storepass", "android", "-keypass", "android",
				"-alias", "androiddebugkey",
				"-dname", "CN=Android Debug,O=Android,C=US",
				"-keyalg", "RSA", "-keysize", "2048", "-validity", "10000" };

			plan.sign.label = "signing";
			plan.sign.arguments = { java, "-jar",
				ExportFiles::join(layout.buildTools, "lib/apksigner.jar"),
				"sign", "--ks", layout.debugKeystore,
				"--ks-pass", "pass:android", "--key-pass", "pass:android",
				layout.outputPath };
		}
		else if(!layout.moduleOnly)
		{
			plan.buildBundle.label = "bundletool build-bundle";
			plan.buildBundle.arguments = { java, "-jar", layout.bundletool,
				"build-bundle", "--modules=" + layout.moduleZip,
				"--output=" + layout.bundlePath };
			if(!layout.bundleConfig.empty())
			{
				plan.buildBundle.arguments.push_back(
					"--config=" + layout.bundleConfig);
			}

			// the passwords travel as environment variable NAMES: jarsigner
			// reads them itself, so no secret ever reaches a command line
			plan.signBundle.label = "jarsigner (release)";
			plan.signBundle.arguments = {
				androidProgramPath(javaBin, "jarsigner"),
				"-sigalg", "SHA256withRSA", "-digestalg", "SHA-256",
				"-keystore", layout.releaseKeystore,
				"-storepass:env", layout.storePasswordEnv,
				"-keypass:env", layout.keyPasswordEnv,
				layout.bundlePath, layout.releaseKeyAlias };

			plan.verifyBundle.label = "jarsigner -verify";
			plan.verifyBundle.arguments = {
				androidProgramPath(javaBin, "jarsigner"), "-verify",
				layout.bundlePath };
		}
		return plan;
	}
	//---------------------------------------------------------
	AndroidCommand androidExtractJavaGlueCommand(Orkige::String const & archive,
		Orkige::String const & destination)
	{
		AndroidCommand command;
		command.label = "extracting the SDL Java glue";
		command.arguments = { "tar", "-xzf", archive, "-C", destination,
			"--strip-components=1",
			"*/android-project/app/src/main/java/org/libsdl/app" };
		return command;
	}
	//---------------------------------------------------------
	bool isAndroidLaunchColour(Orkige::String const & colour)
	{
		if(colour.size() != 7 || colour[0] != '#')
		{
			return false;
		}
		for(std::size_t index = 1; index < colour.size(); ++index)
		{
			if(std::isxdigit(static_cast<unsigned char>(colour[index])) == 0)
			{
				return false;
			}
		}
		return true;
	}
	//---------------------------------------------------------
	Orkige::String androidLaunchColoursXml(Orkige::String const & colour)
	{
		return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
			"<resources>\n"
			"    <color name=\"launch_bg\">" + colour + "</color>\n"
			"</resources>\n";
	}
	//---------------------------------------------------------
	Orkige::String androidLaunchStylesXml()
	{
		return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
			"<resources>\n"
			"    <style name=\"OrkigeLaunch\" "
			"parent=\"@android:style/Theme.NoTitleBar.Fullscreen\">\n"
			"        <item name=\"android:windowBackground\">@color/launch_bg"
			"</item>\n"
			"    </style>\n"
			"</resources>\n";
	}
	//---------------------------------------------------------
	Orkige::String androidBundleConfigJson()
	{
		// the glob syntax is bundletool's own (assets/** under the module root)
		return "{\n"
			"  \"compression\": {\n"
			"    \"uncompressedGlob\": [\"assets/**\"]\n"
			"  }\n"
			"}\n";
	}
	//---------------------------------------------------------
	Orkige::String androidAssetManifest(
		std::vector<Orkige::String> const & relativePaths)
	{
		Orkige::String text;
		for(Orkige::String const & path : relativePaths)
		{
			text += path + "\n";
		}
		return text;
	}
	//---------------------------------------------------------
	int androidPlayTargetSdkFloor()
	{
		return PLAY_TARGET_SDK_FLOOR;
	}
	//---------------------------------------------------------
	int androidManifestTargetSdk(Orkige::String const & manifestText)
	{
		const Orkige::String value =
			attributeValue(manifestText, "android:targetSdkVersion");
		int api = 0;
		for(std::size_t index = 0; index < value.size(); ++index)
		{
			if(std::isdigit(static_cast<unsigned char>(value[index])) == 0)
			{
				return 0;
			}
			api = api * 10 + static_cast<int>(value[index] - '0');
		}
		return api;
	}
	//---------------------------------------------------------
	Orkige::String androidManifestText(Orkige::String const & templateText,
		AndroidManifestEdits const & edits)
	{
		Orkige::String text = templateText;
		if(!edits.package.empty())
		{
			text = replaceAll(text,
				Orkige::String("package=\"") + TEMPLATE_PACKAGE + "\"",
				"package=\"" + edits.package + "\"");
		}
		if(!edits.label.empty())
		{
			text = replaceAll(text,
				Orkige::String("android:label=\"") + TEMPLATE_LABEL + "\"",
				"android:label=\"" + edits.label + "\"");
		}
		if(!edits.screenOrientation.empty())
		{
			// injected in front of the configChanges the activity already
			// declares; `auto` passes none, so an unconstrained project leaves
			// the template's own (unspecified) value alone
			text = replaceAll(text, "android:configChanges=",
				"android:screenOrientation=\"" + edits.screenOrientation +
					"\" android:configChanges=");
		}
		if(edits.launcherResources)
		{
			text = replaceAll(text, TEMPLATE_THEME,
				Orkige::String("android:icon=\"@mipmap/ic_launcher\"\n"
					"        android:theme=\"@style/OrkigeLaunch\""));
		}
		if(edits.release)
		{
			text = replaceAll(text, "android:debuggable=\"true\"",
				"android:debuggable=\"false\"");
		}
		if(edits.versionCode > 0)
		{
			text = replaceAll(text, "android:versionCode=\"1\"",
				"android:versionCode=\"" + std::to_string(edits.versionCode) +
					"\"");
		}
		if(!edits.versionName.empty())
		{
			const Orkige::String current =
				attributeValue(templateText, "android:versionName");
			if(!current.empty())
			{
				text = replaceAll(text,
					"android:versionName=\"" + current + "\"",
					"android:versionName=\"" + edits.versionName + "\"");
			}
		}
		return text;
	}

	//--- the assembly ------------------------------------------

	namespace
	{
		//! what one run resolved about its engine source before planning
		struct AndroidEngineFacts
		{
			Orkige::String	abi;
			Orkige::String	flavor;
			Orkige::String	nativeLibrary;
			Orkige::String	strip;			//!< "" = already stripped
			Orkige::String	assemblyDirectory;	//!< manifest + res + java
		};
		//---------------------------------------------------------
		//! run one planned command through the injected runner, reporting the
		//! program by name when it cannot be spawned
		bool runStep(AndroidCommand const & command, ExportLog const & log,
			ProcessRunner const & runner, Orkige::String * error)
		{
			if(command.empty())
			{
				return true;
			}
			emit(log, command.label);
			const ProcessResult result = runner(command.arguments);
			if(!result.launched)
			{
				return report(error, "could not run '" + command.program() +
					"'");
			}
			if(result.exitCode != 0)
			{
				return report(error, ExportFiles::fileName(command.program()) +
					" failed (exit " + std::to_string(result.exitCode) + ")" +
					(result.output.empty() ? "" : ": " + result.output));
			}
			return true;
		}
		//---------------------------------------------------------
		//! the NDK strip a build tree recorded, else the one under the NDK the
		//! environment names ("" when neither is there, which is honest: the
		//! packaged library is then the unstripped one)
		Orkige::String resolveStrip(Orkige::String const & buildDirectory,
			EnvironmentMap const & environment)
		{
			const Orkige::String recorded =
				readCMakeCache(buildDirectory, "CMAKE_STRIP");
			if(ExportFiles::isRegularFile(recorded))
			{
				return recorded;
			}
			const Orkige::String ndk =
				lookupEnvironment(environment, "ANDROID_NDK_HOME");
			if(ndk.empty())
			{
				return Orkige::String();
			}
			const Orkige::String prebuilt = ExportFiles::join(ndk,
				"toolchains/llvm/prebuilt");
			for(Orkige::String const & host : entryNames(prebuilt, true))
			{
				const Orkige::String candidate = androidProgramPath(
					ExportFiles::join(ExportFiles::join(prebuilt, host), "bin"),
					"llvm-strip");
				if(ExportFiles::isRegularFile(candidate))
				{
					return candidate;
				}
			}
			return Orkige::String();
		}
		//---------------------------------------------------------
		//! SDL3's Java glue, from the exact source vcpkg built the player's
		//! native library from. A glue and a library that disagree is a crash
		//! at the JNI boundary, so this never falls back to "some" SDL.
		bool resolveSdlJavaGlue(AndroidPackageRequest const & request,
			Orkige::String const & workDirectory, Orkige::String & out,
			Orkige::String * error)
		{
			const Orkige::String relative =
				"android-project/app/src/main/java/org/libsdl/app";
			const Orkige::String buildtrees = ExportFiles::join(
				request.vcpkgRoot, "buildtrees/sdl3/src");
			for(Orkige::String const & name : entryNames(buildtrees, true))
			{
				if(!endsWith(name, ".clean"))
				{
					continue;
				}
				const Orkige::String candidate = ExportFiles::join(
					ExportFiles::join(buildtrees, name), relative);
				if(ExportFiles::isDirectory(candidate))
				{
					out = candidate;
					return true;
				}
			}
			// ...else the verified source archive in the downloads cache, which
			// is what a tree whose buildtrees were cleaned still has
			const Orkige::String downloads =
				ExportFiles::join(request.vcpkgRoot, "downloads");
			Orkige::String archive;
			for(Orkige::String const & name : entryNames(downloads, false))
			{
				if(beginsWith(name, "libsdl-org-SDL-release-3.") &&
					endsWith(name, ".tar.gz"))
				{
					archive = ExportFiles::join(downloads, name);
				}
			}
			if(archive.empty())
			{
				return report(error, "SDL3's Java sources are not under '" +
					request.vcpkgRoot + "' (neither buildtrees nor the "
					"downloads cache) - configure the android-debug preset "
					"once, or package from an installed Android player");
			}
			const Orkige::String tar = findOnPath("tar");
			if(tar.empty())
			{
				return report(error, "tar - it unpacks the SDL source archive "
					"the Java glue comes out of (install it, or configure the "
					"android-debug preset once so vcpkg leaves the extracted "
					"sources in place)");
			}
			const Orkige::String extract =
				ExportFiles::join(workDirectory, "sdl3-java-src");
			if(!ExportFiles::removeTree(extract, error) ||
				!ExportFiles::makeDirectories(extract, error))
			{
				return false;
			}
			AndroidCommand command =
				androidExtractJavaGlueCommand(archive, extract);
			command.arguments[0] = tar;
			emit(request.log, command.label);
			const ProcessResult result = request.runner(command.arguments);
			if(!result.launched)
			{
				return report(error, "could not run '" + tar + "'");
			}
			out = ExportFiles::join(extract, relative);
			if(!ExportFiles::isDirectory(out))
			{
				return report(error, "the SDL source archive '" + archive +
					"' carries no Java glue at " + relative);
			}
			return true;
		}
		//---------------------------------------------------------
		//! the `.java` files this package compiles, from whichever engine
		//! source answered
		bool resolveJavaSources(AndroidPackageRequest const & request,
			AndroidEngineFacts const & facts,
			Orkige::String const & workDirectory,
			std::vector<Orkige::String> & out, Orkige::String * error)
		{
			if(!request.devicePayload.empty())
			{
				// a payload carries the glue outright: it was composed on the
				// machine that had vcpkg, matched to the library beside it
				const Orkige::String java =
					ExportFiles::join(facts.assemblyDirectory, "java");
				if(!ExportFiles::isRegularFile(ExportFiles::join(java,
					"org/libsdl/app/SDLActivity.java")))
				{
					return report(error, "the installed Android player carries "
						"no SDL Java glue at '" + java + "' - fetch it again "
						"under Settings > Build Targets");
				}
				out = javaSourcesUnder(java);
				if(out.empty())
				{
					return report(error, "no Java sources under '" + java +
						"'");
				}
				return true;
			}
			Orkige::String glue;
			if(!resolveSdlJavaGlue(request, workDirectory, glue, error))
			{
				return false;
			}
			out = javaSourcesUnder(glue);
			const Orkige::String activity = ExportFiles::join(
				facts.assemblyDirectory,
				"java/com/orkitec/orkigeplayer/OrkigeActivity.java");
			const Orkige::String http = ExportFiles::join(request.repoRoot,
				"orkige_core/core_http/OrkigeHttp.java");
			out.push_back(activity);
			out.push_back(http);
			for(Orkige::String const & source : out)
			{
				if(!ExportFiles::isRegularFile(source))
				{
					return report(error, "no Java source at '" + source + "'");
				}
			}
			return true;
		}
		//---------------------------------------------------------
		//! what the engine source says about itself: the ABI its library is
		//! for, the render flavor its shader media belongs to, and where the
		//! assembly pieces (manifest template, res policy, Java) live
		bool resolveEngineFacts(AndroidPackageRequest const & request,
			AndroidEngineFacts & out, Orkige::String * error)
		{
			if(!request.devicePayload.empty())
			{
				out.abi = devicePayloadSetting(request.devicePayload, "abi");
				out.flavor = payloadFlavor(request.devicePayload);
				out.nativeLibrary =
					ExportFiles::join(request.devicePayload, "libmain.so");
				out.assemblyDirectory =
					ExportFiles::join(request.devicePayload, "android");
				if(out.abi.empty())
				{
					return report(error, "the installed Android player names "
						"no ABI - fetch it again under Settings > Build "
						"Targets");
				}
				// a payload's library was stripped when the payload was
				// composed, on the machine that had the NDK
				return true;
			}
			out.abi = readCMakeCache(request.buildDirectory, "ANDROID_ABI");
			if(out.abi.empty())
			{
				out.abi = "arm64-v8a";
			}
			if(out.abi != "arm64-v8a" && out.abi != "x86_64")
			{
				return report(error, "unsupported Android ABI '" + out.abi +
					"'");
			}
			out.flavor = renderBackend(request.buildDirectory);
			out.nativeLibrary = ExportFiles::join(ExportFiles::join(
				request.buildDirectory, "tools/player"), "libmain.so");
			out.strip = resolveStrip(request.buildDirectory,
				request.environment);
			out.assemblyDirectory =
				ExportFiles::join(request.repoRoot, "tools/player/android");
			return true;
		}
		//---------------------------------------------------------
		//! the assets tree every packaged app carries: the backend's shader
		//! media, the engine content media, the player's own demo content (a
		//! build tree only) and the staged project payload
		bool stageAssets(AndroidPackageRequest const & request,
			AndroidEngineFacts const & facts, Orkige::String const & assets,
			Orkige::String * error)
		{
			const Orkige::String media = ExportFiles::join(assets, "Media");
			if(!ExportFiles::makeDirectories(media, error))
			{
				return false;
			}
			if(!request.devicePayload.empty())
			{
				// a payload's Media/ IS this layout already: it was staged from
				// the same build tree the player came out of, so the shaders
				// beside the library are the ones it was built against
				if(!ExportFiles::copyTree(
					ExportFiles::join(request.devicePayload, "Media"), media,
					error, 0))
				{
					return false;
				}
			}
			else
			{
				const Orkige::String backendMedia = facts.flavor == "next"
					? ogreNextMediaDirectory(request.buildDirectory)
					: ogreMediaDirectory(request.buildDirectory);
				if(backendMedia.empty())
				{
					return report(error, "the build tree '" +
						request.buildDirectory + "' carries no " +
						facts.flavor + " shader media - configure and build "
						"the Android preset first");
				}
				if(!stageEngineMediaFromTree(assets, backendMedia, facts.flavor,
					engineSourceMedia(request.repoRoot, facts.flavor), error))
				{
					return false;
				}
				// the player's own demo content, which a packaged game never
				// boots into but a bare player run is entirely about
				const Orkige::String samples =
					ExportFiles::join(request.repoRoot, "samples");
				struct Demo { char const * from; char const * to; };
				const Demo demos[] = {
					{ "hello_orkige/media", "assets" },
					{ "jumper/media", "jumper_media" }
				};
				for(Demo const & demo : demos)
				{
					const Orkige::String from =
						ExportFiles::join(samples, demo.from);
					if(ExportFiles::isDirectory(from) &&
						!ExportFiles::copyTree(from,
							ExportFiles::join(assets, demo.to), error, 0))
					{
						return false;
					}
				}
				const Orkige::String scene =
					ExportFiles::join(samples, "scenes/example.oscene");
				if(ExportFiles::isRegularFile(scene) &&
					!ExportFiles::copyFile(scene,
						ExportFiles::join(assets, "example.oscene"), error))
				{
					return false;
				}
			}
			if(!request.projectPayload.empty())
			{
				// the project payload plus the default-project marker: the
				// runtime finds the marker at the bundle root and boots the
				// project with no arguments at all
				if(!ExportFiles::copyTree(request.projectPayload,
					ExportFiles::join(assets, "project"), error, 0) ||
					!ExportFiles::writeTextFile(
						ExportFiles::join(assets, "orkige_project.txt"),
						"project\n", error))
				{
					return false;
				}
			}
			// the third-party license notices, at the assets root the marker
			// sits at - the resource root an Android runtime resolves. An
			// Android package is sourced from a fetched payload or a build
			// tree; there is no bundle-resources form of it.
			if(!stageThirdPartyNoticesFrom(assets,
				thirdPartyNoticesCandidates(
					{ request.devicePayload, request.repoRoot }),
				request.log, 0, error))
			{
				return false;
			}
			if(request.assetsMode == "stored")
			{
				// the mount marker: the runtime MOUNTS the package and reads
				// its assets in place rather than extracting them
				if(!ExportFiles::writeTextFile(
					ExportFiles::join(assets, "orkige_mount.txt"), "stored\n",
					error))
				{
					return false;
				}
			}
			// the extraction manifest, listing every bundled file relative to
			// the assets root. Unused on the mount path, kept so a compressed
			// package still lists.
			std::vector<Orkige::String> bundled;
			for(Orkige::String const & relative :
				ExportFiles::listFilesRecursive(assets))
			{
				if(relative != "orkige_assets.txt")
				{
					bundled.push_back(relative);
				}
			}
			return ExportFiles::writeTextFile(
				ExportFiles::join(assets, "orkige_assets.txt"),
				androidAssetManifest(bundled), error);
		}
		//---------------------------------------------------------
		//! the `res/` tree aapt2 compiles: the network-security policy every
		//! package carries, plus the launcher mipmaps and launch theme when the
		//! caller staged one
		bool stageResources(AndroidPackageRequest const & request,
			AndroidEngineFacts const & facts, Orkige::String const & resDirectory,
			Orkige::String * error)
		{
			if(!ExportFiles::removeTree(resDirectory, error))
			{
				return false;
			}
			if(!request.launcherResources.empty())
			{
				if(!ExportFiles::isDirectory(request.launcherResources))
				{
					return report(error, "no res directory at '" +
						request.launcherResources + "'");
				}
				if(!ExportFiles::copyTree(request.launcherResources,
					resDirectory, error, 0))
				{
					return false;
				}
			}
			// every package names res/xml/orkige_network_security.xml in its
			// manifest: the platform's cleartext and trust-anchor policy has to
			// agree with the engine's own HTTP policy rather than silently
			// overrule it (see that file and Docs/http.md)
			const Orkige::String policy = ExportFiles::join(
				facts.assemblyDirectory,
				"res/xml/orkige_network_security.xml");
			if(!ExportFiles::isRegularFile(policy))
			{
				return report(error, "no network-security policy at '" + policy +
					"'");
			}
			if(!ExportFiles::copyFile(policy, ExportFiles::join(resDirectory,
				"xml/orkige_network_security.xml"), error))
			{
				return false;
			}
			if(request.launcherResources.empty())
			{
				return true;
			}
			if(!isAndroidLaunchColour(request.launchColour))
			{
				return report(error, "launch background '" +
					request.launchColour + "' is not #RRGGBB");
			}
			return ExportFiles::writeTextFile(
				ExportFiles::join(resDirectory, "values/colors.xml"),
				androidLaunchColoursXml(request.launchColour), error) &&
				ExportFiles::writeTextFile(
					ExportFiles::join(resDirectory, "values/styles.xml"),
					androidLaunchStylesXml(), error);
		}
		//---------------------------------------------------------
		//! write the APK: the linked resources, the dex, the native library and
		//! the assets, each with the compression its reader needs
		bool writeApkArchive(Orkige::String const & linked,
			Orkige::String const & stage, bool storedAssets,
			Orkige::String const & path, Orkige::String * error)
		{
			ExportZip zip;
			// resources.arsc must be STORED (the platform maps it in place, and
			// refuses a compressed one from API 30 on); everything else the
			// linker produced is read after inflation
			const std::vector<Orkige::String> storedLinked = { "resources.arsc" };
			if(!addTree(zip, linked, "", false, storedLinked, error))
			{
				return false;
			}
			for(Orkige::String const & relative :
				ExportFiles::listFilesRecursive(stage))
			{
				const bool asset = beginsWith(relative, "assets/");
				if(!zip.addFile(relative, ExportFiles::join(stage, relative),
					(asset && storedAssets) ? ExportZip::METHOD_STORE
						: ExportZip::METHOD_DEFLATE, error))
				{
					return false;
				}
			}
			return zip.write(path, error);
		}
		//---------------------------------------------------------
		//! write the bundle MODULE zip bundletool consumes: the module content
		//! at the zip root (manifest/, dex/, res/, resources.pb, assets/, lib/)
		bool writeBundleModule(Orkige::String const & linked,
			Orkige::String const & stage, Orkige::String const & path,
			Orkige::String * error)
		{
			ExportZip zip;
			const std::vector<Orkige::String> none;
			const Orkige::String manifest =
				ExportFiles::join(linked, "AndroidManifest.xml");
			if(!zip.addFile("manifest/AndroidManifest.xml", manifest,
				ExportZip::METHOD_DEFLATE, error))
			{
				return false;
			}
			const Orkige::String resources =
				ExportFiles::join(linked, "resources.pb");
			if(ExportFiles::isRegularFile(resources) &&
				!zip.addFile("resources.pb", resources,
					ExportZip::METHOD_DEFLATE, error))
			{
				return false;
			}
			if(!addTree(zip, ExportFiles::join(linked, "res"), "res/", false,
				none, error))
			{
				return false;
			}
			if(!zip.addFile("dex/classes.dex",
				ExportFiles::join(stage, "classes.dex"),
				ExportZip::METHOD_DEFLATE, error))
			{
				return false;
			}
			return addTree(zip, ExportFiles::join(stage, "assets"), "assets/",
				false, none, error) &&
				addTree(zip, ExportFiles::join(stage, "lib"), "lib/", false,
					none, error) &&
				zip.write(path, error);
		}
	}
	//---------------------------------------------------------
	bool assembleAndroidPackage(AndroidPackageRequest const & request,
		AndroidToolchain const & tools, Orkige::String & outArtifact,
		Orkige::String * error)
	{
		if(request.outputPath.empty())
		{
			return report(error, "an Android package needs an output path");
		}
		if(request.buildDirectory.empty() == request.devicePayload.empty())
		{
			return report(error, "a build tree and an installed player are two "
				"engine sources - package from one");
		}
		if(request.assetsMode != "stored" && request.assetsMode != "compressed")
		{
			return report(error, "the asset packaging mode must be 'stored' or "
				"'compressed' (got '" + request.assetsMode + "')");
		}
		AndroidEngineFacts facts;
		if(!resolveEngineFacts(request, facts, error))
		{
			return false;
		}
		if(!ExportFiles::isRegularFile(facts.nativeLibrary))
		{
			return report(error, "no libmain.so at '" + facts.nativeLibrary +
				"'");
		}
		const Orkige::String outputDirectory = std::filesystem::path(
			request.outputPath).parent_path().string();
		const Orkige::String work = request.workDirectory.empty()
			? ExportFiles::join(outputDirectory, "apk-work")
			: request.workDirectory;
		const Orkige::String stage = ExportFiles::join(work, "stage");
		const Orkige::String assets = ExportFiles::join(stage, "assets");
		if(!ExportFiles::removeTree(work, error) ||
			!ExportFiles::makeDirectories(
				ExportFiles::join(stage, "lib/" + facts.abi), error) ||
			!ExportFiles::makeDirectories(assets, error))
		{
			return false;
		}

		std::vector<Orkige::String> javaSources;
		if(!resolveJavaSources(request, facts, work, javaSources, error))
		{
			return false;
		}
		emit(request.log, "Java: " + std::to_string(javaSources.size()) +
			" sources");

		AndroidAssemblyLayout layout;
		layout.buildTools = tools.buildTools;
		layout.platformJar = tools.platformJar;
		layout.javaHome = tools.javaHome;
		layout.strip = facts.strip;
		layout.minimumApi = androidMinimumApi();
		layout.nativeLibrary = facts.nativeLibrary;
		layout.stagedLibrary = ExportFiles::join(
			ExportFiles::join(stage, "lib/" + facts.abi), "libmain.so");
		layout.javaSources = javaSources;
		layout.classesDirectory = ExportFiles::join(work, "classes");
		layout.classListFile = ExportFiles::join(work, "classlist.txt");
		layout.dexDirectory = ExportFiles::join(work, "dex");
		layout.resDirectory = ExportFiles::join(work, "res");
		layout.compiledResources = ExportFiles::join(work, "res.zip");
		layout.manifestPath = ExportFiles::join(work, "AndroidManifest.xml");
		layout.linkedDirectory = ExportFiles::join(work, "linked");
		layout.unalignedPackage = ExportFiles::join(work, "unaligned.apk");
		layout.outputPath = request.outputPath;
		layout.bundle = request.bundle;
		layout.moduleOnly = request.moduleOnly;
		layout.moduleZip = ExportFiles::join(work, "base.zip");
		layout.bundlePath = ExportFiles::join(work, "app.aab");
		layout.bundletool = request.bundletool;
		layout.releaseKeystore = request.keystore.keystore;
		layout.releaseKeyAlias = request.keystore.alias;
		layout.storePasswordEnv = ANDROID_KEYSTORE_PASS_ENV;
		// the key password defaults to the store password when the environment
		// names none - the same variable, so no secret changes hands
		layout.keyPasswordEnv =
			lookupEnvironment(request.environment, ANDROID_KEY_PASS_ENV).empty()
				? ANDROID_KEYSTORE_PASS_ENV : ANDROID_KEY_PASS_ENV;
		if(request.bundle && request.assetsMode == "stored" &&
			!request.moduleOnly)
		{
			layout.bundleConfig = ExportFiles::join(work, "BundleConfig.json");
		}
		// the SHARED Android debug key, so every package this machine builds
		// installs over the last one. Without a home directory to keep it in
		// the key is per-run instead of shared, which is the honest degrade -
		// never a relative path written into whatever the current directory is.
		Orkige::String home = lookupEnvironment(request.environment, "HOME");
		if(home.empty())
		{
			home = lookupEnvironment(request.environment, "USERPROFILE");
		}
		layout.debugKeystore = home.empty()
			? ExportFiles::join(work, "debug.keystore")
			: ExportFiles::join(home, ".android/debug.keystore");

		const AndroidAssemblyPlan plan = androidAssemblyPlan(layout);

		//--- the native library
		if(plan.strip.empty())
		{
			emit(request.log, "staging libmain.so (" + facts.abi + ")");
			if(!ExportFiles::copyFile(facts.nativeLibrary,
				layout.stagedLibrary, error))
			{
				return false;
			}
		}
		else if(!runStep(plan.strip, request.log, request.runner, error))
		{
			return false;
		}

		//--- the Java side
		if(!ExportFiles::makeDirectories(layout.classesDirectory, error) ||
			!ExportFiles::makeDirectories(layout.dexDirectory, error) ||
			!runStep(plan.compileJava, request.log, request.runner, error))
		{
			return false;
		}
		std::vector<Orkige::String> classes;
		for(Orkige::String const & relative :
			ExportFiles::listFilesRecursive(layout.classesDirectory))
		{
			if(endsWith(relative, ".class"))
			{
				classes.push_back(
					ExportFiles::join(layout.classesDirectory, relative));
			}
		}
		if(classes.empty())
		{
			return report(error, "javac produced no classes under '" +
				layout.classesDirectory + "'");
		}
		Orkige::String classList;
		for(Orkige::String const & path : classes)
		{
			classList += path + "\n";
		}
		if(!ExportFiles::writeTextFile(layout.classListFile, classList,
			error) ||
			!runStep(plan.dex, request.log, request.runner, error) ||
			!ExportFiles::copyFile(
				ExportFiles::join(layout.dexDirectory, "classes.dex"),
				ExportFiles::join(stage, "classes.dex"), error))
		{
			return false;
		}

		//--- the assets
		emit(request.log, "staging assets (" + facts.flavor + " flavor)");
		if(!stageAssets(request, facts, assets, error))
		{
			return false;
		}

		//--- the resources and the manifest
		if(!stageResources(request, facts, layout.resDirectory, error))
		{
			return false;
		}
		Orkige::String templateText;
		if(!ExportFiles::readTextFile(ExportFiles::join(
			facts.assemblyDirectory, "AndroidManifest.xml"), templateText,
			error))
		{
			return false;
		}
		AndroidManifestEdits edits;
		edits.package = request.package;
		edits.label = request.label;
		edits.screenOrientation = request.screenOrientation;
		edits.launcherResources = !request.launcherResources.empty();
		edits.release = request.bundle;
		edits.versionCode = request.bundle ? request.versionCode : 0;
		edits.versionName = request.bundle ? request.versionName
			: Orkige::String();
		const Orkige::String manifestText =
			androidManifestText(templateText, edits);
		if(!ExportFiles::writeTextFile(layout.manifestPath, manifestText,
			error))
		{
			return false;
		}
		if(request.bundle)
		{
			const int target = androidManifestTargetSdk(manifestText);
			if(target > 0 && target < androidPlayTargetSdkFloor())
			{
				emit(request.log, "WARNING: targetSdkVersion " +
					std::to_string(target) + " is below Google Play's current "
					"floor (" + std::to_string(androidPlayTargetSdkFloor()) +
					") - Play will reject the upload (see "
					"Docs/store-release.md)");
			}
		}
		if(!runStep(plan.compileResources, request.log, request.runner,
			error) ||
			!ExportFiles::makeDirectories(layout.linkedDirectory, error) ||
			!runStep(plan.linkResources, request.log, request.runner, error))
		{
			return false;
		}

		//--- the artifact
		if(!request.bundle)
		{
			emit(request.log, "packing (assets: " + request.assetsMode + ")");
			if(!writeApkArchive(layout.linkedDirectory, stage,
				request.assetsMode == "stored", layout.unalignedPackage, error))
			{
				return false;
			}
			if(!ExportFiles::makeDirectories(outputDirectory, error) ||
				!runStep(plan.align, request.log, request.runner, error))
			{
				return false;
			}
			if(!ExportFiles::isRegularFile(layout.debugKeystore))
			{
				// keytool writes the keystore but does NOT create the directory
				// holding it, and on a machine that has never run an Android
				// tool `~/.android` does not exist yet - a fresh CI runner is
				// exactly that machine. Make the parent first, the way the
				// shell this replaced did, or keytool fails with a bare
				// FileNotFoundException naming a path it could have created.
				const Orkige::String keystoreDirectory =
					std::filesystem::path(layout.debugKeystore)
						.parent_path().string();
				if(!keystoreDirectory.empty() &&
					!ExportFiles::makeDirectories(keystoreDirectory, error))
				{
					return false;
				}
				if(!runStep(plan.createDebugKey, request.log, request.runner,
					error))
				{
					return false;
				}
			}
			if(!runStep(plan.sign, request.log, request.runner, error))
			{
				return false;
			}
		}
		else
		{
			emit(request.log, "assembling the bundle module");
			const Orkige::String modulePath = request.moduleOnly
				? request.outputPath : layout.moduleZip;
			if(!ExportFiles::makeDirectories(outputDirectory, error) ||
				!writeBundleModule(layout.linkedDirectory, stage, modulePath,
					error))
			{
				return false;
			}
			if(!request.moduleOnly)
			{
				if(!layout.bundleConfig.empty() &&
					!ExportFiles::writeTextFile(layout.bundleConfig,
						androidBundleConfigJson(), error))
				{
					return false;
				}
				if(!runStep(plan.buildBundle, request.log, request.runner,
					error) ||
					!runStep(plan.signBundle, request.log, request.runner,
						error) ||
					!runStep(plan.verifyBundle, request.log, request.runner,
						error) ||
					!ExportFiles::copyFile(layout.bundlePath,
						request.outputPath, error))
				{
					return false;
				}
			}
		}
		if(!ExportFiles::isRegularFile(request.outputPath))
		{
			return report(error, "the assembly produced no '" +
				request.outputPath + "'");
		}
		ExportFiles::removeTree(work, 0);
		emit(request.log, "done: " + request.outputPath + " (" +
			humanSize(ExportFiles::treeSize(request.outputPath)) + ")");
		outArtifact = request.outputPath;
		return true;
	}
}
