/********************************************************************
	created:	Friday 2026/07/31 at 18:00
	filename: 	ExportPayload.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportPayload.h"

#include "ExportFiles.h"
#include "ExportSettings.h"
#include "ExportTextureCook.h"

#include <core_project/AssetDatabase.h>
#include <core_project/Project.h>
#include <core_project/TextureSamplerTable.h>

#include <functional>
#include <string>
#include <vector>

namespace OrkigeExport
{
	namespace
	{
		void emit(ExportLog const & log, Orkige::String const & message)
		{
			if(log)
			{
				log(message);
			}
		}
		//---------------------------------------------------------
		Orkige::String trimmed(Orkige::String const & text)
		{
			const std::size_t first = text.find_first_not_of(" \t\r\n");
			if(first == Orkige::String::npos)
			{
				return "";
			}
			const std::size_t last = text.find_last_not_of(" \t\r\n");
			return text.substr(first, last - first + 1);
		}
		//---------------------------------------------------------
		//! copy a source media directory into `<resources>/Media/<name>` when
		//! it exists; an absent one is not an error (the runtime degrades)
		bool stageMedia(Orkige::String const & source,
			Orkige::String const & resources, Orkige::String const & name,
			Orkige::String * error)
		{
			if(source.empty())
			{
				return true;
			}
			return ExportFiles::copyTree(source,
				ExportFiles::join(ExportFiles::join(resources, "Media"), name),
				error, 0);
		}
	}
	//---------------------------------------------------------
	bool stageConfigSettings(ExportProject const & project,
		Orkige::String const & destination, ExportLog const & log,
		int * outStaged, Orkige::String * error)
	{
		int staged = 0;
		for(Orkige::String const & key : configSettingKeys())
		{
			const Orkige::String relative = trimmed(project.setting(key));
			if(relative.empty())
			{
				continue;
			}
			const Orkige::String source =
				ExportFiles::join(project.root, relative);
			const Orkige::String target =
				ExportFiles::join(destination, relative);
			if(ExportFiles::isDirectory(source))
			{
				int count = 0;
				if(!ExportFiles::copyTree(source, target, error, &count))
				{
					return false;
				}
				staged += count;
			}
			else if(ExportFiles::isRegularFile(source))
			{
				if(!ExportFiles::copyFile(source, target, error))
				{
					return false;
				}
				++staged;
			}
			else
			{
				// a stale key in a manifest is not a reason to refuse an
				// export - say so and carry on
				emit(log, "WARNING: manifest setting '" + key +
					"' references '" + relative + "' but no such file or "
					"directory exists - not bundled");
			}
		}
		if(outStaged != 0)
		{
			*outStaged = staged;
		}
		return true;
	}
	//---------------------------------------------------------
	bool bakeTextureSamplers(Orkige::String const & projectRoot,
		Orkige::String const & payloadDirectory,
		Orkige::String const & manifestPath,
		Orkige::String const & texturePlatform, ExportLog const & log,
		int * outStripped, Orkige::String * error)
	{
		// a LOCAL database: refresh() is read-only and never touches the
		// process-wide active one, so an in-process export leaves the editor's
		// open project alone (@see ExportProject.h)
		Orkige::AssetDatabase database;
		database.refresh(projectRoot, false);
		Orkige::TextureSamplerTable samplers;
		samplers.fillFromAssets(database, texturePlatform);
		Orkige::String bakeError;
		if(!Orkige::Project::writeBakedTextureSamplers(manifestPath, samplers,
			&bakeError))
		{
			if(error != 0)
			{
				*error = bakeError;
			}
			return false;
		}
		if(!samplers.empty())
		{
			emit(log, "baked " + std::to_string(samplers.size()) +
				" texture sampler(s) into the payload manifest");
		}
		// the sidecars themselves are editor bookkeeping and never ship
		const Orkige::String metaExtension =
			Orkige::AssetDatabase::META_FILE_EXTENSION;
		int stripped = 0;
		for(Orkige::String const & relative :
			ExportFiles::listFilesRecursive(payloadDirectory))
		{
			if(relative.size() <= metaExtension.size() ||
				relative.compare(relative.size() - metaExtension.size(),
					metaExtension.size(), metaExtension) != 0)
			{
				continue;
			}
			if(!ExportFiles::removeTree(
				ExportFiles::join(payloadDirectory, relative), error))
			{
				return false;
			}
			++stripped;
		}
		if(outStripped != 0)
		{
			*outStripped = stripped;
		}
		return true;
	}
	//---------------------------------------------------------
	bool stripEditorScripts(Orkige::String const & payloadDirectory,
		ExportLog const & log, int * outStripped, Orkige::String * error)
	{
		const Orkige::String suffix = ".editor.lua";
		int stripped = 0;
		for(Orkige::String const & relative :
			ExportFiles::listFilesRecursive(payloadDirectory))
		{
			if(relative.size() <= suffix.size() ||
				relative.compare(relative.size() - suffix.size(),
					suffix.size(), suffix) != 0)
			{
				continue;
			}
			if(!ExportFiles::removeTree(
				ExportFiles::join(payloadDirectory, relative), error))
			{
				return false;
			}
			++stripped;
		}
		if(stripped > 0)
		{
			emit(log, "dropped " + std::to_string(stripped) +
				" editor tool script(s) - dev tooling never ships");
		}
		if(outStripped != 0)
		{
			*outStripped = stripped;
		}
		return true;
	}
	//---------------------------------------------------------
	bool stageProjectPayload(ExportProject const & project,
		Orkige::String const & destination,
		Orkige::String const & texturePlatform, Orkige::String const & flavor,
		ExportLog const & log, int * outStaged, Orkige::String * error)
	{
		if(!ExportFiles::makeDirectories(destination, error))
		{
			return false;
		}
		if(!ExportFiles::copyFile(
			ExportFiles::join(project.root, "project.orkproj"),
			ExportFiles::join(destination, "project.orkproj"), error))
		{
			return false;
		}
		int staged = 1;
		for(Orkige::String const & subdir : payloadSubdirs())
		{
			const Orkige::String source =
				ExportFiles::join(project.root, subdir);
			if(!ExportFiles::isDirectory(source))
			{
				continue;
			}
			int count = 0;
			if(!ExportFiles::copyTree(source,
				ExportFiles::join(destination, subdir), error, &count))
			{
				return false;
			}
			staged += count;
		}
		int configStaged = 0;
		if(!stageConfigSettings(project, destination, log, &configStaged,
			error))
		{
			return false;
		}
		staged += configStaged;

		// the export-time texture cook: resize/premultiply/block-compress the
		// staged textures per their sidecar import settings, resolved for the
		// target platform AND the packaged render flavor (which picks the
		// container and the auto formats).
		TextureCookResult cook;
		if(!cookTexturePayload(destination, texturePlatform, flavor, cook, log,
			error))
		{
			return false;
		}
		if(cook.cooked > 0)
		{
			emit(log, "cooked " + std::to_string(cook.cooked) +
				" texture(s) for platform '" + texturePlatform + "' (" +
				flavor + " flavor)");
		}
		// then the sampler bake, which retires the sidecars: the cook read
		// them, nothing downstream does
		int stripped = 0;
		if(!bakeTextureSamplers(project.root, destination,
			ExportFiles::join(destination, "project.orkproj"), texturePlatform,
			log, &stripped, error))
		{
			return false;
		}
		staged -= stripped;
		// the other dev-only artefact riding inside a payload subdirectory
		int editorScripts = 0;
		if(!stripEditorScripts(destination, log, &editorScripts, error))
		{
			return false;
		}
		staged -= editorScripts;
		if(outStaged != 0)
		{
			*outStaged = staged;
		}
		return true;
	}
	//---------------------------------------------------------
	bool stageEngineMediaFromTree(Orkige::String const & resources,
		Orkige::String const & backendMediaDirectory,
		Orkige::String const & flavor, EngineSourceMedia const & sourceMedia,
		Orkige::String * error)
	{
		const Orkige::String media = ExportFiles::join(resources, "Media");
		std::vector<Orkige::String> subdirs;
		if(flavor == "next")
		{
			subdirs = ogreNextMediaSubdirs(backendMediaDirectory);
		}
		else
		{
			subdirs.push_back("Main");
			subdirs.push_back("RTShaderLib");
		}
		for(Orkige::String const & subdir : subdirs)
		{
			if(!ExportFiles::copyTree(
				ExportFiles::join(backendMediaDirectory, subdir),
				ExportFiles::join(media, subdir), error, 0))
			{
				return false;
			}
		}
		if(flavor != "next" && !sourceMedia.rtss.empty())
		{
			// the engine-owned metal-rough shader library is merged INTO the
			// bundled RTShaderLib: the runtime registers that ONE location
			if(!ExportFiles::copyTree(sourceMedia.rtss,
				ExportFiles::join(media, "RTShaderLib"), error, 0))
			{
				return false;
			}
		}
		return stageEngineContentMedia(resources, flavor, sourceMedia, error);
	}
	//---------------------------------------------------------
	bool stageEngineContentMedia(Orkige::String const & resources,
		Orkige::String const & flavor, EngineSourceMedia const & sourceMedia,
		Orkige::String * error)
	{
		if(!stageMedia(sourceMedia.fonts, resources, "fonts", error) ||
			!stageMedia(sourceMedia.water, resources, "water", error) ||
			!stageMedia(sourceMedia.decals, resources, "decals", error))
		{
			return false;
		}
		// the compositor media engine:setBloom / engine:setGrade need, kept
		// under their flavor like the runtime looks them up
		return stageMedia(sourceMedia.bloom, resources, "bloom/" + flavor,
			error) &&
			stageMedia(sourceMedia.grade, resources, "grade/" + flavor, error);
	}
	//---------------------------------------------------------
	bool writeProjectMarker(Orkige::String const & directory,
		Orkige::String * error)
	{
		return ExportFiles::writeTextFile(
			ExportFiles::join(directory, PROJECT_MARKER_FILE_NAME),
			Orkige::String(PAYLOAD_DIR_NAME) + "\n", error);
	}
	//---------------------------------------------------------
	const char * const DEVICE_PAYLOAD_MANIFEST_FILE_NAME =
		"orkige_payload.txt";
	//---------------------------------------------------------
	Orkige::String devicePayloadSetting(
		Orkige::String const & payloadDirectory, Orkige::String const & key)
	{
		Orkige::String text;
		if(payloadDirectory.empty() || !ExportFiles::readTextFile(
			ExportFiles::join(payloadDirectory,
				DEVICE_PAYLOAD_MANIFEST_FILE_NAME), text, 0))
		{
			return Orkige::String();
		}
		std::size_t begin = 0;
		while(begin <= text.size())
		{
			const std::size_t newline = text.find('\n', begin);
			const Orkige::String line = trimmed(text.substr(begin,
				newline == Orkige::String::npos ? Orkige::String::npos
					: newline - begin));
			const std::size_t colon = line.find(':');
			if(colon != Orkige::String::npos &&
				trimmed(line.substr(0, colon)) == key)
			{
				return trimmed(line.substr(colon + 1));
			}
			if(newline == Orkige::String::npos)
			{
				break;
			}
			begin = newline + 1;
		}
		return Orkige::String();
	}
	//---------------------------------------------------------
	Orkige::String payloadFlavor(Orkige::String const & payloadDirectory)
	{
		const Orkige::String flavor =
			devicePayloadSetting(payloadDirectory, "flavor");
		return flavor.empty() ? Orkige::String("classic") : flavor;
	}
}
