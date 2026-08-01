/********************************************************************
	created:	Friday 2026/07/31 at 12:00
	filename: 	ExportFiles.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "ExportFiles.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace OrkigeExport
{
	namespace
	{
		//! one shape for every "the filesystem said no" report: the operation,
		//! the path, and what the OS called it
		bool report(Orkige::String * error, Orkige::String const & what,
			Orkige::String const & path, std::error_code const & code)
		{
			if(error != 0)
			{
				*error = what + " '" + path + "': " + code.message();
			}
			return false;
		}
	}
	//---------------------------------------------------------
	bool ExportFiles::exists(Orkige::String const & path)
	{
		std::error_code ignored;
		return std::filesystem::exists(std::filesystem::path(path), ignored);
	}
	//---------------------------------------------------------
	bool ExportFiles::isDirectory(Orkige::String const & path)
	{
		std::error_code ignored;
		return std::filesystem::is_directory(std::filesystem::path(path),
			ignored);
	}
	//---------------------------------------------------------
	bool ExportFiles::isRegularFile(Orkige::String const & path)
	{
		std::error_code ignored;
		return std::filesystem::is_regular_file(std::filesystem::path(path),
			ignored);
	}
	//---------------------------------------------------------
	bool ExportFiles::makeDirectories(Orkige::String const & path,
		Orkige::String * error)
	{
		if(ExportFiles::isDirectory(path))
		{
			return true;
		}
		std::error_code code;
		std::filesystem::create_directories(std::filesystem::path(path), code);
		if(code)
		{
			return report(error, "cannot create the directory", path, code);
		}
		return true;
	}
	//---------------------------------------------------------
	bool ExportFiles::removeTree(Orkige::String const & path,
		Orkige::String * error)
	{
		std::error_code code;
		std::filesystem::remove_all(std::filesystem::path(path), code);
		if(code)
		{
			return report(error, "cannot remove", path, code);
		}
		return true;
	}
	//---------------------------------------------------------
	bool ExportFiles::copyFile(Orkige::String const & source,
		Orkige::String const & destination, Orkige::String * error)
	{
		const std::filesystem::path target(destination);
		if(target.has_parent_path() &&
			!ExportFiles::makeDirectories(target.parent_path().string(), error))
		{
			return false;
		}
		std::error_code code;
		std::filesystem::copy_file(std::filesystem::path(source), target,
			std::filesystem::copy_options::overwrite_existing, code);
		if(code)
		{
			return report(error, "cannot copy '" + source + "' to", destination,
				code);
		}
		return true;
	}
	//---------------------------------------------------------
	bool ExportFiles::copyTree(Orkige::String const & source,
		Orkige::String const & destination, Orkige::String * error,
		int * outFileCount)
	{
		const std::filesystem::path from(source);
		const std::filesystem::path to(destination);
		std::error_code code;
		if(!std::filesystem::is_directory(from, code))
		{
			if(error != 0)
			{
				*error = "cannot copy '" + source + "': not a directory";
			}
			return false;
		}
		if(!ExportFiles::makeDirectories(destination, error))
		{
			return false;
		}
		int staged = 0;
		// copy_symlink for links (a bundle's Versions/Current and a dylib's
		// loader aliases must stay links), recursive descent otherwise
		std::filesystem::recursive_directory_iterator walk(from,
			std::filesystem::directory_options::none, code);
		if(code)
		{
			return report(error, "cannot read", source, code);
		}
		const std::filesystem::recursive_directory_iterator end;
		for(; walk != end; walk.increment(code))
		{
			if(code)
			{
				return report(error, "cannot read", source, code);
			}
			// LEXICALLY relative: std::filesystem::relative resolves
			// symlinks, which would collapse a dylib's loader alias onto the
			// versioned file it points at and stage one entry where the
			// bundle needs two. Every entry is under `from` by construction,
			// so the textual form is exact.
			const std::filesystem::path relative =
				walk->path().lexically_relative(from);
			const std::filesystem::path target = to / relative;
			if(walk->is_symlink())
			{
				std::error_code removeCode;
				std::filesystem::remove(target, removeCode);
				std::filesystem::copy_symlink(walk->path(), target, code);
				if(code)
				{
					return report(error, "cannot copy the symlink",
						walk->path().string(), code);
				}
				// a symlink is not descended into
				walk.disable_recursion_pending();
				continue;
			}
			if(walk->is_directory())
			{
				std::filesystem::create_directories(target, code);
				if(code)
				{
					return report(error, "cannot create the directory",
						target.string(), code);
				}
				continue;
			}
			if(target.has_parent_path())
			{
				std::filesystem::create_directories(target.parent_path(), code);
			}
			std::filesystem::copy_file(walk->path(), target,
				std::filesystem::copy_options::overwrite_existing, code);
			if(code)
			{
				return report(error, "cannot copy", walk->path().string(),
					code);
			}
			++staged;
		}
		if(outFileCount != 0)
		{
			*outFileCount = staged;
		}
		return true;
	}
	//---------------------------------------------------------
	int ExportFiles::countFiles(Orkige::String const & path)
	{
		std::error_code code;
		const std::filesystem::path root(path);
		if(std::filesystem::is_regular_file(root, code))
		{
			return 1;
		}
		int count = 0;
		std::filesystem::recursive_directory_iterator walk(root,
			std::filesystem::directory_options::none, code);
		if(code)
		{
			return 0;
		}
		const std::filesystem::recursive_directory_iterator end;
		for(; walk != end; walk.increment(code))
		{
			if(code)
			{
				break;
			}
			if(!walk->is_symlink() && walk->is_regular_file())
			{
				++count;
			}
		}
		return count;
	}
	//---------------------------------------------------------
	unsigned long long ExportFiles::treeSize(Orkige::String const & path)
	{
		std::error_code code;
		const std::filesystem::path root(path);
		if(std::filesystem::is_regular_file(root, code))
		{
			const std::uintmax_t size = std::filesystem::file_size(root, code);
			return code ? 0ull : static_cast<unsigned long long>(size);
		}
		unsigned long long total = 0;
		std::filesystem::recursive_directory_iterator walk(root,
			std::filesystem::directory_options::none, code);
		if(code)
		{
			return 0;
		}
		const std::filesystem::recursive_directory_iterator end;
		for(; walk != end; walk.increment(code))
		{
			if(code)
			{
				break;
			}
			if(walk->is_symlink() || !walk->is_regular_file())
			{
				continue;
			}
			std::error_code sizeCode;
			const std::uintmax_t size =
				std::filesystem::file_size(walk->path(), sizeCode);
			if(!sizeCode)
			{
				total += static_cast<unsigned long long>(size);
			}
		}
		return total;
	}
	//---------------------------------------------------------
	bool ExportFiles::makeExecutable(Orkige::String const & path,
		Orkige::String * error)
	{
		std::error_code code;
		std::filesystem::permissions(std::filesystem::path(path),
			std::filesystem::perms::owner_all |
			std::filesystem::perms::group_read |
			std::filesystem::perms::group_exec |
			std::filesystem::perms::others_read |
			std::filesystem::perms::others_exec,
			std::filesystem::perm_options::replace, code);
		if(code)
		{
			return report(error, "cannot set the executable bit on", path,
				code);
		}
		return true;
	}
	//---------------------------------------------------------
	bool ExportFiles::makeSymlink(Orkige::String const & target,
		Orkige::String const & linkPath, Orkige::String * error)
	{
		std::error_code removeCode;
		std::filesystem::remove(std::filesystem::path(linkPath), removeCode);
		std::error_code code;
		std::filesystem::create_symlink(std::filesystem::path(target),
			std::filesystem::path(linkPath), code);
		if(code)
		{
			return report(error, "cannot create the symlink", linkPath, code);
		}
		return true;
	}
	//---------------------------------------------------------
	bool ExportFiles::writeTextFile(Orkige::String const & path,
		Orkige::String const & text, Orkige::String * error)
	{
		const std::filesystem::path target(path);
		if(target.has_parent_path() &&
			!ExportFiles::makeDirectories(target.parent_path().string(), error))
		{
			return false;
		}
		// binary, so the LF line endings the engine's files carry survive on
		// every host
		std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
		if(!file)
		{
			if(error != 0)
			{
				*error = "cannot write '" + path + "'";
			}
			return false;
		}
		file.write(text.data(), static_cast<std::streamsize>(text.size()));
		file.close();
		if(!file)
		{
			if(error != 0)
			{
				*error = "write failed for '" + path + "'";
			}
			return false;
		}
		return true;
	}
	//---------------------------------------------------------
	bool ExportFiles::readTextFile(Orkige::String const & path,
		Orkige::String & out, Orkige::String * error)
	{
		std::ifstream file(path.c_str(), std::ios::binary);
		if(!file)
		{
			if(error != 0)
			{
				*error = "cannot read '" + path + "'";
			}
			return false;
		}
		out.assign(std::istreambuf_iterator<char>(file),
			std::istreambuf_iterator<char>());
		return true;
	}
	//---------------------------------------------------------
	bool ExportFiles::writeBytes(Orkige::String const & path,
		std::vector<unsigned char> const & bytes, Orkige::String * error)
	{
		const std::filesystem::path target(path);
		if(target.has_parent_path() &&
			!ExportFiles::makeDirectories(target.parent_path().string(), error))
		{
			return false;
		}
		std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
		if(!file)
		{
			if(error != 0)
			{
				*error = "cannot write '" + path + "'";
			}
			return false;
		}
		file.write(reinterpret_cast<char const *>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		file.close();
		if(!file)
		{
			if(error != 0)
			{
				*error = "write failed for '" + path + "'";
			}
			return false;
		}
		return true;
	}
	//---------------------------------------------------------
	bool ExportFiles::readBytes(Orkige::String const & path,
		std::vector<unsigned char> & out, Orkige::String * error)
	{
		std::ifstream file(path.c_str(), std::ios::binary);
		if(!file)
		{
			if(error != 0)
			{
				*error = "cannot read '" + path + "'";
			}
			return false;
		}
		out.assign(std::istreambuf_iterator<char>(file),
			std::istreambuf_iterator<char>());
		return true;
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> ExportFiles::listFilesRecursive(
		Orkige::String const & root)
	{
		std::vector<Orkige::String> files;
		std::error_code code;
		const std::filesystem::path base(root);
		std::filesystem::recursive_directory_iterator walk(base,
			std::filesystem::directory_options::none, code);
		if(code)
		{
			return files;
		}
		const std::filesystem::recursive_directory_iterator end;
		for(; walk != end; walk.increment(code))
		{
			if(code)
			{
				break;
			}
			if(walk->is_symlink() || !walk->is_regular_file())
			{
				continue;
			}
			// lexically relative, for the same reason copyTree is
			files.push_back(
				walk->path().lexically_relative(base).generic_string());
		}
		std::sort(files.begin(), files.end());
		return files;
	}
	//---------------------------------------------------------
	Orkige::String ExportFiles::join(Orkige::String const & left,
		Orkige::String const & right)
	{
		// make_preferred is what makes the whole result speak ONE separator:
		// the fragments carry forward slashes and the host may not (it is a
		// no-op wherever the preferred separator already is '/')
		if(left.empty())
		{
			return std::filesystem::path(right).make_preferred().string();
		}
		return (std::filesystem::path(left) / right).make_preferred().string();
	}
	//---------------------------------------------------------
	Orkige::String ExportFiles::absolute(Orkige::String const & path)
	{
		std::error_code code;
		const std::filesystem::path resolved =
			std::filesystem::absolute(std::filesystem::path(path), code);
		if(code)
		{
			return path;
		}
		return resolved.lexically_normal().string();
	}
	//---------------------------------------------------------
	Orkige::String ExportFiles::stem(Orkige::String const & path)
	{
		return std::filesystem::path(path).stem().string();
	}
	//---------------------------------------------------------
	Orkige::String ExportFiles::fileName(Orkige::String const & path)
	{
		return std::filesystem::path(path).filename().string();
	}
	//---------------------------------------------------------
	Orkige::String ExportFiles::replaceExtension(Orkige::String const & path,
		Orkige::String const & extension)
	{
		std::filesystem::path target(path);
		target.replace_extension("." + extension);
		return target.string();
	}
}
