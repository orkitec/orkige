/********************************************************************
	created:	Friday 2026/07/31 at 18:00
	filename: 	ExportBuildTree.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportBuildTree.h"

#include "ExportFiles.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace OrkigeExport
{
	namespace
	{
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
	}
	//---------------------------------------------------------
	Orkige::String parseCMakeCacheLine(Orkige::String const & line,
		Orkige::String const & variable)
	{
		// a cache entry is NAME:TYPE=VALUE; the name must match whole, so
		// ORKIGE_RENDER_BACKEND never answers for ORKIGE_RENDER_BACKEND_XYZ
		if(line.rfind(variable + ":", 0) != 0)
		{
			return "";
		}
		const std::size_t equals = line.find('=');
		if(equals == Orkige::String::npos)
		{
			return "";
		}
		return trimmed(line.substr(equals + 1));
	}
	//---------------------------------------------------------
	Orkige::String readCMakeCache(Orkige::String const & buildDirectory,
		Orkige::String const & variable)
	{
		const Orkige::String cachePath =
			ExportFiles::join(buildDirectory, "CMakeCache.txt");
		if(!ExportFiles::isRegularFile(cachePath))
		{
			return "";
		}
		std::ifstream cache(cachePath.c_str());
		if(!cache)
		{
			return "";
		}
		std::string line;
		while(std::getline(cache, line))
		{
			const Orkige::String value = parseCMakeCacheLine(line, variable);
			if(!value.empty())
			{
				return value;
			}
		}
		return "";
	}
	//---------------------------------------------------------
	Orkige::String vcpkgTripletDirectory(Orkige::String const & buildDirectory)
	{
		const Orkige::String installed =
			ExportFiles::join(buildDirectory, "vcpkg_installed");
		if(!ExportFiles::isDirectory(installed))
		{
			return "";
		}
		std::vector<Orkige::String> candidates;
		std::error_code code;
		std::filesystem::directory_iterator walk(
			std::filesystem::path(installed), code);
		if(code)
		{
			return "";
		}
		const std::filesystem::directory_iterator end;
		for(; walk != end; walk.increment(code))
		{
			if(code)
			{
				break;
			}
			// through join, so the answer is spelled the one way every
			// derived path is - never however the caller spelled its input
			candidates.push_back(ExportFiles::join(installed,
				walk->path().filename().string()));
		}
		// sorted, so the answer does not depend on directory order
		std::sort(candidates.begin(), candidates.end());
		for(Orkige::String const & candidate : candidates)
		{
			// the triplet directory is the one carrying headers; vcpkg also
			// keeps bookkeeping directories beside it
			if(ExportFiles::isDirectory(
				ExportFiles::join(candidate, "include")))
			{
				return candidate;
			}
		}
		return "";
	}
	//---------------------------------------------------------
	Orkige::String renderBackend(Orkige::String const & buildDirectory)
	{
		const Orkige::String flavor =
			readCMakeCache(buildDirectory, "ORKIGE_RENDER_BACKEND");
		return flavor.empty() ? Orkige::String("classic") : flavor;
	}
	//---------------------------------------------------------
	Orkige::String releaseTreeName(Orkige::String const & flavor)
	{
		return (flavor == "next") ? "macos-release" : "macos-release-classic";
	}
	//---------------------------------------------------------
	bool prefersSiblingReleasePlayer(Orkige::String const & buildType,
		bool testBuild)
	{
		if(testBuild)
		{
			return false;
		}
		return buildType != "Release";
	}
	//---------------------------------------------------------
	Orkige::String siblingReleaseTree(Orkige::String const & buildDirectory)
	{
		const Orkige::String parent = std::filesystem::path(
			ExportFiles::absolute(buildDirectory)).parent_path().string();
		return ExportFiles::join(parent,
			releaseTreeName(renderBackend(buildDirectory)));
	}
	//---------------------------------------------------------
	Orkige::String tripletArchitecture(Orkige::String const & triplet)
	{
		if(triplet.rfind("arm64-", 0) == 0)
		{
			return "arm64";
		}
		if(triplet.rfind("x64-", 0) == 0 || triplet.rfind("x86_64-", 0) == 0)
		{
			return "x86_64";
		}
		return "";
	}
	//---------------------------------------------------------
	Orkige::String engineTreeArchitecture(Orkige::String const & buildDirectory)
	{
		const Orkige::String triplet = vcpkgTripletDirectory(buildDirectory);
		return triplet.empty() ? Orkige::String()
			: tripletArchitecture(ExportFiles::fileName(triplet));
	}
	//---------------------------------------------------------
	namespace
	{
		//! `<triplet>/share/<package>/Media`, or "" when it is not there
		Orkige::String shareMedia(Orkige::String const & buildDirectory,
			Orkige::String const & package)
		{
			const Orkige::String triplet =
				vcpkgTripletDirectory(buildDirectory);
			if(triplet.empty())
			{
				return "";
			}
			const Orkige::String media = ExportFiles::join(
				ExportFiles::join(ExportFiles::join(triplet, "share"), package),
				"Media");
			return ExportFiles::isDirectory(media) ? media : Orkige::String();
		}
		//---------------------------------------------------------
		//! one directory under an engine media root, or "" when absent
		Orkige::String sourceMedia(Orkige::String const & mediaRoot,
			Orkige::String const & relative)
		{
			if(mediaRoot.empty())
			{
				return "";
			}
			const Orkige::String path =
				ExportFiles::join(mediaRoot, relative);
			return ExportFiles::isDirectory(path) ? path : Orkige::String();
		}
	}
	//---------------------------------------------------------
	Orkige::String ogreMediaDirectory(Orkige::String const & buildDirectory)
	{
		return shareMedia(buildDirectory, "ogre");
	}
	//---------------------------------------------------------
	Orkige::String ogreNextMediaDirectory(Orkige::String const & buildDirectory)
	{
		return shareMedia(buildDirectory, "ogre-next");
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> ogreNextMediaSubdirs(
		Orkige::String const & mediaDirectory)
	{
		std::vector<Orkige::String> subdirs;
		subdirs.push_back("Hlms");
		if(ExportFiles::isDirectory(
			ExportFiles::join(mediaDirectory, "Atmosphere")))
		{
			subdirs.push_back("Atmosphere");
		}
		return subdirs;
	}
	//---------------------------------------------------------
	EngineSourceMedia engineMediaFromRoot(Orkige::String const & mediaRoot,
		Orkige::String const & flavor)
	{
		EngineSourceMedia media;
		media.fonts = sourceMedia(mediaRoot, "fonts");
		media.water = sourceMedia(mediaRoot, "water");
		media.decals = sourceMedia(mediaRoot, "decals");
		media.rtss = sourceMedia(mediaRoot, "rtss");
		media.bloom = sourceMedia(mediaRoot, "bloom/" + flavor);
		media.grade = sourceMedia(mediaRoot, "grade/" + flavor);
		return media;
	}
	//---------------------------------------------------------
	EngineSourceMedia engineSourceMedia(Orkige::String const & repoRoot,
		Orkige::String const & flavor)
	{
		return engineMediaFromRoot(repoRoot.empty() ? Orkige::String()
			: ExportFiles::join(repoRoot, "orkige_engine/media"), flavor);
	}
}
