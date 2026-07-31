/********************************************************************
	created:	Friday 2026/07/31 at 16:00
	filename: 	ExportSelfContain.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportSelfContain.h"

#include "ExportFiles.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <sstream>
#include <string>
#include <system_error>

namespace OrkigeExport
{
	namespace
	{
		bool report(Orkige::String * error, Orkige::String const & message)
		{
			if(error != 0)
			{
				*error = message;
			}
			return false;
		}
		//---------------------------------------------------------
		Orkige::String trimmed(Orkige::String const & text)
		{
			std::size_t first = 0;
			std::size_t last = text.size();
			while(first < last && std::isspace(
				static_cast<unsigned char>(text[first])) != 0)
			{
				++first;
			}
			while(last > first && std::isspace(
				static_cast<unsigned char>(text[last - 1])) != 0)
			{
				--last;
			}
			return text.substr(first, last - first);
		}
		//---------------------------------------------------------
		void emit(std::function<void(Orkige::String const &)> const & log,
			Orkige::String const & message)
		{
			if(log)
			{
				log(message);
			}
		}
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> parseOtoolDependencies(
		Orkige::String const & otoolOutput)
	{
		std::vector<Orkige::String> dependencies;
		std::istringstream stream(otoolOutput);
		Orkige::String line;
		bool first = true;
		while(std::getline(stream, line))
		{
			if(first)
			{
				// the first line names the file being examined, not a
				// dependency
				first = false;
				continue;
			}
			Orkige::String entry = trimmed(line);
			const std::size_t compatibility = entry.find(" (");
			if(compatibility != Orkige::String::npos)
			{
				entry = entry.substr(0, compatibility);
			}
			if(entry.empty())
			{
				continue;
			}
			// the OS's own libraries stay dynamic; everything else has to ride
			// inside the bundle
			if(entry.rfind("/usr/lib/", 0) == 0 ||
				entry.rfind("/System/", 0) == 0)
			{
				continue;
			}
			dependencies.push_back(entry);
		}
		return dependencies;
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> parseOtoolRpaths(
		Orkige::String const & otoolOutput)
	{
		std::vector<Orkige::String> rpaths;
		std::vector<Orkige::String> lines;
		std::istringstream stream(otoolOutput);
		Orkige::String line;
		while(std::getline(stream, line))
		{
			lines.push_back(line);
		}
		for(std::size_t index = 0; index < lines.size(); ++index)
		{
			if(lines[index].find("cmd LC_RPATH") == Orkige::String::npos)
			{
				continue;
			}
			// the path sits within the next few lines of the load command
			const std::size_t end = std::min(index + 4, lines.size());
			for(std::size_t scan = index; scan < end; ++scan)
			{
				const Orkige::String candidate = trimmed(lines[scan]);
				if(candidate.rfind("path ", 0) != 0)
				{
					continue;
				}
				Orkige::String value = trimmed(candidate.substr(5));
				// "path <value> (offset 12)" - drop the trailing note
				const std::size_t offsetNote = value.find(" (offset");
				if(offsetNote != Orkige::String::npos)
				{
					value = trimmed(value.substr(0, offsetNote));
				}
				if(!value.empty())
				{
					rpaths.push_back(value);
				}
				break;
			}
		}
		return rpaths;
	}
	//---------------------------------------------------------
	bool isBuildMachineRpath(Orkige::String const & rpath,
		std::vector<Orkige::String> const & bannedMarkers)
	{
		for(Orkige::String const & marker : bannedMarkers)
		{
			if(!marker.empty() &&
				rpath.find(marker) != Orkige::String::npos)
			{
				return true;
			}
		}
		return false;
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> dylibAliases(Orkige::String const & directory,
		Orkige::String const & dylibName)
	{
		std::vector<Orkige::String> aliases;
		std::error_code code;
		const std::filesystem::path target = std::filesystem::canonical(
			std::filesystem::path(directory) / dylibName, code);
		if(code)
		{
			return aliases;
		}
		std::filesystem::directory_iterator walk(
			std::filesystem::path(directory), code);
		if(code)
		{
			return aliases;
		}
		const std::filesystem::directory_iterator end;
		for(; walk != end; walk.increment(code))
		{
			if(code)
			{
				break;
			}
			const Orkige::String name = walk->path().filename().string();
			if(name == dylibName || !walk->is_symlink())
			{
				continue;
			}
			std::error_code resolveCode;
			const std::filesystem::path resolved =
				std::filesystem::canonical(walk->path(), resolveCode);
			if(!resolveCode && resolved == target)
			{
				aliases.push_back(name);
			}
		}
		// a deterministic order, so a re-staged bundle is byte-stable
		std::sort(aliases.begin(), aliases.end());
		return aliases;
	}
	//---------------------------------------------------------
	Orkige::String resolveDylibDependency(Orkige::String const & dependency,
		std::vector<Orkige::String> const & searchDirectories)
	{
		const Orkige::String rpathPrefix = "@rpath/";
		if(dependency.rfind(rpathPrefix, 0) == 0)
		{
			const Orkige::String name =
				dependency.substr(rpathPrefix.size());
			for(Orkige::String const & directory : searchDirectories)
			{
				const Orkige::String candidate =
					ExportFiles::join(directory, name);
				if(ExportFiles::isRegularFile(candidate))
				{
					return candidate;
				}
			}
			return "";
		}
		return ExportFiles::isRegularFile(dependency) ? dependency
			: Orkige::String();
	}
	//---------------------------------------------------------
	bool makeSelfContained(SelfContainRequest const & request,
		ProcessRunner const & runner,
		std::function<void(Orkige::String const &)> const & log,
		Orkige::String * error)
	{
		auto run = [&runner, &log, error](
			std::vector<Orkige::String> const & arguments) -> bool
		{
			emit(log, "$ " + commandLine(arguments));
			const ProcessResult result = runner(arguments);
			if(!result.launched)
			{
				return report(error, "could not run '" + arguments[0] + "'");
			}
			if(result.exitCode != 0)
			{
				return report(error, "command failed (exit " +
					std::to_string(result.exitCode) + "): " + arguments[0] +
					(result.output.empty() ? "" : " - " + result.output));
			}
			return true;
		};

		const ProcessResult listing =
			runner({ "otool", "-L", request.executable });
		if(!listing.launched || listing.exitCode != 0)
		{
			return report(error, "could not read the dylib dependencies of '" +
				request.executable + "'");
		}
		const ProcessResult loadCommands =
			runner({ "otool", "-l", request.executable });
		if(!loadCommands.launched || loadCommands.exitCode != 0)
		{
			return report(error, "could not read the load commands of '" +
				request.executable + "'");
		}
		const std::vector<Orkige::String> existingRpaths =
			parseOtoolRpaths(loadCommands.output);
		// an @rpath dependency resolves against the binary's OWN rpaths first,
		// then the directories the caller named
		std::vector<Orkige::String> searchDirectories = existingRpaths;
		searchDirectories.insert(searchDirectories.end(),
			request.searchDirectories.begin(), request.searchDirectories.end());

		std::vector<DylibDependency> dependencies;
		for(Orkige::String const & dependency :
			parseOtoolDependencies(listing.output))
		{
			const Orkige::String resolved =
				resolveDylibDependency(dependency, searchDirectories);
			if(resolved.empty())
			{
				return report(error, "cannot resolve dylib dependency '" +
					dependency + "' of '" + request.executable + "'");
			}
			dependencies.push_back({ dependency, resolved });
		}

		bool changed = false;
		if(!dependencies.empty() &&
			!ExportFiles::makeDirectories(request.frameworksDirectory, error))
		{
			return false;
		}
		for(DylibDependency const & dependency : dependencies)
		{
			const Orkige::String name =
				ExportFiles::fileName(dependency.resolved);
			if(!ExportFiles::copyFile(dependency.resolved,
				ExportFiles::join(request.frameworksDirectory, name), error))
			{
				return false;
			}
			emit(log, "bundled dylib " + name);
			const Orkige::String sourceDirectory = std::filesystem::path(
				dependency.resolved).parent_path().string();
			for(Orkige::String const & alias :
				dylibAliases(sourceDirectory, name))
			{
				if(!ExportFiles::makeSymlink(name,
					ExportFiles::join(request.frameworksDirectory, alias),
					error))
				{
					return false;
				}
				emit(log, "aliased dylib " + alias + " -> " + name);
			}
			if(dependency.dependency.rfind("@rpath/", 0) != 0)
			{
				// an absolute dev path: load it via the bundle rpath instead
				if(!run({ "install_name_tool", "-change",
					dependency.dependency, "@rpath/" + name,
					request.executable }))
				{
					return false;
				}
			}
			changed = true;
		}

		const Orkige::String bundleRpath = "@executable_path/../Frameworks";
		for(Orkige::String const & rpath : existingRpaths)
		{
			if(!isBuildMachineRpath(rpath, request.bannedRpathMarkers))
			{
				continue;
			}
			if(!run({ "install_name_tool", "-delete_rpath", rpath,
				request.executable }))
			{
				return false;
			}
			changed = true;
		}
		if(!dependencies.empty() &&
			std::find(existingRpaths.begin(), existingRpaths.end(),
				bundleRpath) == existingRpaths.end())
		{
			if(!run({ "install_name_tool", "-add_rpath", bundleRpath,
				request.executable }))
			{
				return false;
			}
			changed = true;
		}
		if(changed)
		{
			// install_name_tool invalidates the linker's ad-hoc signature and
			// arm64 macOS refuses to run an unsigned binary
			if(!run({ "codesign", "--force", "-s", "-", request.executable }))
			{
				return false;
			}
		}
		return true;
	}
}
