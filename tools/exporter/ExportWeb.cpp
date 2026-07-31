/********************************************************************
	created:	Saturday 2026/08/01 at 14:00
	filename: 	ExportWeb.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportWeb.h"

#include "ExportBuildTree.h"
#include "ExportFiles.h"
#include "ExportIcons.h"
#include "ExportImage.h"
#include "ExportSettings.h"
#include "ExportZip.h"

#include <string>
#include <vector>

namespace OrkigeExport
{
	const char * const WEB_PAK_FILE_NAME = "game.pak";
	const char * const WEB_LOADER_FILE_NAME = "game.js";
	const char * const WEB_SHELL_FILE_NAME = "index.html";
	const char * const WEB_PLAYER_SCRIPT_FILE_NAME = "orkige_player.js";
	const char * const WEB_PLAYER_WASM_FILE_NAME = "orkige_player.wasm";
	const char * const WEB_PAYLOAD_DIR_NAME = "web";

	namespace
	{
		//! the shell template + the data loader live beside the player's web
		//! sources in a source tree, and beside the wasm player in a staged
		//! engine payload - the SAME two file names either way
		const char * const WEB_SHELL_TEMPLATE_FILE_NAME = "index.html.in";
		const char * const WEB_LOADER_TEMPLATE_FILE_NAME = "pak_loader.js";
		//! the browser player is the classic flavor (GLES2/GLES3 over WebGL)
		const char * const WEB_FLAVOR = "classic";

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
		void replaceAll(Orkige::String & text, Orkige::String const & needle,
			Orkige::String const & value)
		{
			std::size_t at = text.find(needle);
			while(at != Orkige::String::npos)
			{
				text.replace(at, needle.size(), value);
				at = text.find(needle, at + value.size());
			}
		}
		//---------------------------------------------------------
		//! where the browser pieces come from: a web-release build tree, or the
		//! `web/` subdirectory of a staged engine payload
		struct WebSource
		{
			Orkige::String	playerScript;	//!< orkige_player.js
			Orkige::String	playerWasm;		//!< orkige_player.wasm
			Orkige::String	shellTemplate;	//!< index.html.in
			Orkige::String	loaderTemplate;	//!< pak_loader.js
			//! a staged payload's ready-made classic `Media/` tree ("" for a
			//! build tree, which is staged from its own vcpkg instead)
			Orkige::String	stagedMedia;
		};
		//---------------------------------------------------------
		bool resolveWebSource(EngineSource const & source,
			Orkige::String const & repoRoot, WebSource & out,
			Orkige::String * error)
		{
			if(source.fromBundle())
			{
				const Orkige::String web = ExportFiles::join(
					source.bundleResources, WEB_PAYLOAD_DIR_NAME);
				out.playerScript =
					ExportFiles::join(web, WEB_PLAYER_SCRIPT_FILE_NAME);
				out.playerWasm =
					ExportFiles::join(web, WEB_PLAYER_WASM_FILE_NAME);
				out.shellTemplate =
					ExportFiles::join(web, WEB_SHELL_TEMPLATE_FILE_NAME);
				out.loaderTemplate =
					ExportFiles::join(web, WEB_LOADER_TEMPLATE_FILE_NAME);
				out.stagedMedia = ExportFiles::join(web, "Media");
				if(!ExportFiles::isRegularFile(out.playerScript) ||
					!ExportFiles::isRegularFile(out.playerWasm))
				{
					return report(error, "this copy of Orkige carries no "
						"browser player (no " +
						Orkige::String(WEB_PLAYER_WASM_FILE_NAME) + " under '" +
						web + "') - reinstall Orkige, or build the browser "
						"player from the engine source tree (preset "
						"web-release)");
				}
				if(!ExportFiles::isDirectory(out.stagedMedia))
				{
					return report(error, "this copy of Orkige carries a browser "
						"player but no browser engine media (no 'Media' under '" +
						web + "') - the browser player is the classic flavor "
						"and needs its own shader library staged beside it");
				}
			}
			else
			{
				const Orkige::String tools = ExportFiles::join(
					source.buildDirectory, "tools/player");
				out.playerScript =
					ExportFiles::join(tools, WEB_PLAYER_SCRIPT_FILE_NAME);
				out.playerWasm =
					ExportFiles::join(tools, WEB_PLAYER_WASM_FILE_NAME);
				if(!ExportFiles::isRegularFile(out.playerScript) ||
					!ExportFiles::isRegularFile(out.playerWasm))
				{
					return report(error, "no wasm player at '" +
						out.playerScript + "' - build the web-release preset "
						"first");
				}
				if(renderBackend(source.buildDirectory) != WEB_FLAVOR)
				{
					return report(error, "the browser player is the classic "
						"(GLES2/WebGL) flavor - '" + source.buildDirectory +
						"' is not a classic tree");
				}
				if(repoRoot.empty())
				{
					return report(error, "the shell page and its data loader "
						"live beside the browser player's sources "
						"(tools/player/web) - this export has no engine source "
						"tree to take them from");
				}
				const Orkige::String web =
					ExportFiles::join(repoRoot, "tools/player/web");
				out.shellTemplate =
					ExportFiles::join(web, WEB_SHELL_TEMPLATE_FILE_NAME);
				out.loaderTemplate =
					ExportFiles::join(web, WEB_LOADER_TEMPLATE_FILE_NAME);
			}
			if(!ExportFiles::isRegularFile(out.shellTemplate) ||
				!ExportFiles::isRegularFile(out.loaderTemplate))
			{
				return report(error, "the browser shell page ('" +
					out.shellTemplate + "') or its data loader ('" +
					out.loaderTemplate + "') is missing");
			}
			return true;
		}
		//---------------------------------------------------------
		//! pack the staged tree into ONE pak, deflated: entries are added in
		//! the walk's sorted order, so packaging the same tree twice yields
		//! the same bytes (@see ExportZip.h)
		bool writeGamePak(Orkige::String const & staging,
			Orkige::String const & pakPath, ExportLog const & log,
			Orkige::String * error)
		{
			ExportZip zip;
			const std::vector<Orkige::String> files =
				ExportFiles::listFilesRecursive(staging);
			for(Orkige::String const & relative : files)
			{
				if(!zip.addFile(webArchiveName(relative),
					ExportFiles::join(staging, relative),
					ExportZip::METHOD_DEFLATE, error))
				{
					return false;
				}
			}
			if(!zip.write(pakPath, error))
			{
				return false;
			}
			emit(log, "game pak: " + std::to_string(zip.entryCount()) +
				" entries, " +
				humanSize(ExportFiles::treeSize(pakPath)));
			return true;
		}
	}
	//---------------------------------------------------------
	Orkige::String webShellPage(Orkige::String const & templateText,
		Orkige::String const & title, Orkige::String const & background,
		Orkige::String const & dataLoader, Orkige::String const & playerScript)
	{
		Orkige::String page = templateText;
		replaceAll(page, "@TITLE@", title);
		replaceAll(page, "@BACKGROUND@", background);
		replaceAll(page, "@DATA_LOADER@", dataLoader);
		replaceAll(page, "@PLAYER_JS@", playerScript);
		return page;
	}
	//---------------------------------------------------------
	Orkige::String webArchiveName(Orkige::String const & path)
	{
		Orkige::String name = path;
		for(char & character : name)
		{
			if(character == '\\')
			{
				character = '/';
			}
		}
		return name;
	}
	//---------------------------------------------------------
	bool exportWeb(ExportProject const & project, EngineSource const & source,
		Orkige::String const & outputDirectory,
		ExportEnvironment const & environment, Orkige::String & outArtifact,
		Orkige::String * error)
	{
		if(!project.nativeTarget().empty())
		{
			return report(error, "project '" + project.name + "' has a native "
				"module ('" + project.nativeTarget() + "') - native modules are "
				"desktop-only, the browser player runs Lua/scene projects");
		}
		WebSource web;
		if(!resolveWebSource(source, environment.repoRoot, web, error))
		{
			return false;
		}
		// the output IS the artifact (a directory a web server hosts), so it
		// starts clean - a stale file from an earlier export would be served
		if(!ExportFiles::removeTree(outputDirectory, error) ||
			!ExportFiles::makeDirectories(outputDirectory, error))
		{
			return false;
		}

		// stage the payload tree exactly as a desktop bundle's Resources/ looks
		// (the module filesystem root is what SDL_GetBasePath() answers in the
		// browser), then seal it into the pak
		const Orkige::String staging =
			ExportFiles::join(outputDirectory, "pak-staging");
		if(!ExportFiles::removeTree(staging, error))
		{
			return false;
		}
		if(web.stagedMedia.empty())
		{
			const Orkige::String backendMedia =
				ogreMediaDirectory(source.buildDirectory);
			if(backendMedia.empty())
			{
				return report(error, "no shader media under the build tree's "
					"vcpkg - broken tree?");
			}
			if(!stageEngineMediaFromTree(staging, backendMedia, WEB_FLAVOR,
				engineSourceMedia(environment.repoRoot, WEB_FLAVOR), error))
			{
				return false;
			}
		}
		else if(!ExportFiles::copyTree(web.stagedMedia,
			ExportFiles::join(staging, "Media"), error, 0))
		{
			return false;
		}
		int staged = 0;
		if(!stageProjectPayload(project,
			ExportFiles::join(staging, PAYLOAD_DIR_NAME),
			cookPlatformToken("web"), WEB_FLAVOR, environment.log, &staged,
			error))
		{
			return false;
		}
		emit(environment.log,
			"project payload: " + std::to_string(staged) + " files");
		if(!writeProjectMarker(staging, error))
		{
			return false;
		}
		if(!writeGamePak(staging,
			ExportFiles::join(outputDirectory, WEB_PAK_FILE_NAME),
			environment.log, error))
		{
			return false;
		}
		if(!ExportFiles::removeTree(staging, error))
		{
			return false;
		}

		// the player pair and the loader ride along verbatim
		if(!ExportFiles::copyFile(web.playerScript,
				ExportFiles::join(outputDirectory,
					WEB_PLAYER_SCRIPT_FILE_NAME), error) ||
			!ExportFiles::copyFile(web.playerWasm,
				ExportFiles::join(outputDirectory, WEB_PLAYER_WASM_FILE_NAME),
				error) ||
			!ExportFiles::copyFile(web.loaderTemplate,
				ExportFiles::join(outputDirectory, WEB_LOADER_FILE_NAME),
				error))
		{
			return false;
		}

		// the browser's app-icon slot
		ExportImage icon;
		if(!loadSquareIconSource(resolveIconSource(project,
				environment.defaultIconPath, environment.log), icon, error) ||
			!writeIconSizes(icon, outputDirectory,
				std::vector<IconEntry>{ IconEntry{ "icon.png", 256 } }, 0,
				error))
		{
			return false;
		}

		// the shell page: the project's identity baked into the template
		Orkige::String shellTemplate;
		if(!ExportFiles::readTextFile(web.shellTemplate, shellTemplate, error))
		{
			return false;
		}
		if(!ExportFiles::writeTextFile(
			ExportFiles::join(outputDirectory, WEB_SHELL_FILE_NAME),
			webShellPage(shellTemplate, project.name,
				launchBackground(project.settings), WEB_LOADER_FILE_NAME,
				WEB_PLAYER_SCRIPT_FILE_NAME), error))
		{
			return false;
		}
		emit(environment.log, "serve: python3 -m http.server -d '" +
			outputDirectory + "'");
		outArtifact = outputDirectory;
		return true;
	}
}
