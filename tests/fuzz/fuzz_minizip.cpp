/********************************************************************
	created:	Monday 2026/07/21 at 10:00
	filename: 	fuzz_minizip.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file fuzz_minizip.cpp
//! @brief libFuzzer entry point over the MiniZip central-directory reader -
//! the HIGHEST-value target: it parses zip central directories from UNTRUSTED
//! archives (a downloaded pak, an APK) on every mobile boot. MiniZip is
//! seekable-file backed, so the fuzz input is written to one reused temp file
//! and opened; on success every entry name is enumerated and read, so the
//! local-header + inflate paths are fuzzed alongside the directory scan
//! (@see Docs/fuzzing.md).

#include "engine_filesystem/MiniZip.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
	//! one temp path for the whole process - MiniZip opens a real seekable
	//! file, so each input is flushed here and handed to open() by path
	std::string const & fuzzTempPath()
	{
		static std::string const path = []
		{
			std::string base = "/tmp/orkige_fuzz_minizip";
			if(char const * dir = std::getenv("TMPDIR"))
			{
				base = std::string(dir) + "/orkige_fuzz_minizip";
			}
			base += "-" + std::to_string(static_cast<long>(std::rand()));
			base += ".zip";
			return base;
		}();
		return path;
	}
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size)
{
	std::string const & path = fuzzTempPath();
	{
		std::FILE * file = std::fopen(path.c_str(), "wb");
		if(!file)
		{
			return 0;
		}
		if(size > 0)
		{
			std::fwrite(data, 1, size, file);
		}
		std::fclose(file);
	}

	Orkige::MiniZip zip;
	if(zip.open(path))
	{
		std::vector<std::string> const names = zip.names();
		for(std::string const & name : names)
		{
			std::uint64_t declaredSize = 0;
			zip.sizeOf(name, declaredSize);
			std::vector<unsigned char> bytes;
			zip.read(name, bytes);
		}
	}
	return 0;
}
