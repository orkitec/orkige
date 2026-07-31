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
		// container and the auto formats). The sidecars ship alongside,
		// renamed with any compressed texture, so the runtime keeps reading
		// the LIVE sampler settings and asset ids from them.
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
		if(!stageMedia(sourceMedia.fonts, resources, "fonts", error) ||
			!stageMedia(sourceMedia.water, resources, "water", error) ||
			!stageMedia(sourceMedia.decals, resources, "decals", error))
		{
			return false;
		}
		// the compositor media engine:setBloom / engine:setGrade need, kept
		// under their flavor like the runtime looks them up
		if(!stageMedia(sourceMedia.bloom, resources, "bloom/" + flavor,
			error) ||
			!stageMedia(sourceMedia.grade, resources, "grade/" + flavor,
			error))
		{
			return false;
		}
		return true;
	}
	//---------------------------------------------------------
	bool writeProjectMarker(Orkige::String const & directory,
		Orkige::String * error)
	{
		return ExportFiles::writeTextFile(
			ExportFiles::join(directory, PROJECT_MARKER_FILE_NAME),
			Orkige::String(PAYLOAD_DIR_NAME) + "\n", error);
	}
}
