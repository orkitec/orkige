/********************************************************************
	created:	2026/07/30 at 10:00
	filename: 	FileWriter.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "core_filesystem/FileWriter.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace Orkige
{
	namespace
	{
		//! the temp-file suffix every unfinished transfer carries
		char const * const TEMP_SUFFIX = ".orkpart";
	}
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FileWriter::FileWriter()
	{
		this->mHandle = NULL;
		this->mWritten = 0;
	}
	//---------------------------------------------------------
	FileWriter::~FileWriter()
	{
		// an uncommitted transfer never leaves a file behind
		this->abort();
	}
	//---------------------------------------------------------
	bool FileWriter::begin(String const & path, String & error)
	{
		this->abort();
		if (path.empty())
		{
			error = "the target path is empty";
			return false;
		}
		const std::filesystem::path target(path);
		if (target.has_parent_path() && !target.parent_path().empty())
		{
			std::error_code directoryError;
			std::filesystem::create_directories(target.parent_path(),
				directoryError);
			// an EXISTING directory reports no error; a real failure only
			// matters if the directory is still missing afterwards
			std::error_code existsError;
			if (!std::filesystem::is_directory(target.parent_path(), existsError))
			{
				error = "could not create the directory '" +
					target.parent_path().string() + "'";
				return false;
			}
		}
		this->mPath = path;
		this->mTempPath = path + TEMP_SUFFIX;
		this->mHandle = std::fopen(this->mTempPath.c_str(), "wb");
		if (this->mHandle == NULL)
		{
			error = "could not open '" + this->mTempPath + "' for writing";
			this->mPath.clear();
			this->mTempPath.clear();
			return false;
		}
		this->mWritten = 0;
		return true;
	}
	//---------------------------------------------------------
	bool FileWriter::write(char const * bytes, unsigned long long count,
		String & error)
	{
		if (this->mHandle == NULL)
		{
			error = "no open transfer";
			return false;
		}
		if (count == 0)
		{
			return true;
		}
		if (bytes == NULL)
		{
			error = "no bytes to write";
			this->abort();
			return false;
		}
		FILE * handle = static_cast<FILE *>(this->mHandle);
		const size_t chunk = static_cast<size_t>(count);
		if (std::fwrite(bytes, 1, chunk, handle) != chunk)
		{
			error = "could not write to '" + this->mTempPath +
				"' (out of space?)";
			this->abort();
			return false;
		}
		this->mWritten += count;
		return true;
	}
	//---------------------------------------------------------
	bool FileWriter::commit(String & error)
	{
		if (this->mHandle == NULL)
		{
			error = "no open transfer";
			return false;
		}
		FILE * handle = static_cast<FILE *>(this->mHandle);
		const bool flushed = (std::fflush(handle) == 0);
		std::fclose(handle);
		this->mHandle = NULL;
		if (!flushed)
		{
			error = "could not flush '" + this->mTempPath + "'";
			this->abort();
			return false;
		}
		// THE atomic instant: the target either still holds its old content or
		// holds the complete new one, never a truncated mix
		std::error_code renameError;
		std::filesystem::rename(this->mTempPath, this->mPath, renameError);
		if (renameError)
		{
			error = "could not replace '" + this->mPath + "' (" +
				renameError.message() + ")";
			this->abort();
			return false;
		}
		this->mTempPath.clear();
		return true;
	}
	//---------------------------------------------------------
	void FileWriter::abort()
	{
		if (this->mHandle != NULL)
		{
			std::fclose(static_cast<FILE *>(this->mHandle));
			this->mHandle = NULL;
		}
		if (!this->mTempPath.empty())
		{
			std::error_code ignored;
			std::filesystem::remove(this->mTempPath, ignored);
			this->mTempPath.clear();
		}
		this->mPath.clear();
		this->mWritten = 0;
	}
	//---------------------------------------------------------
	bool FileWriter::writeWholeFile(String const & path, String const & bytes,
		String & error)
	{
		FileWriter writer;
		if (!writer.begin(path, error))
		{
			return false;
		}
		if (!writer.write(bytes.data(), bytes.size(), error))
		{
			return false;
		}
		return writer.commit(error);
	}
}
