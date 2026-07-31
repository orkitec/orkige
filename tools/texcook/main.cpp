/**************************************************************
	created:	2026/07/17 at 09:00
	filename: 	main.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
//! @brief texcook - the argv face of the export-time GPU texture encoder.
//!
//! The encoder itself is the `orkige_texcook` library (@see TextureEncode.h),
//! which the project exporter links directly. This CLI is the shell entry for
//! the paths that drive the cook as a separate process.
//!
//!   texcook --input <levels.rgba> --output <file> --width W --height H
//!           --levels N --format <fmt> --quality low|normal|high
//!           --container dds|ktx|oitd [--faces 1|6]
//!
//! The input file is the concatenation of every FACE's mip levels' RGBA8
//! pixels (face-major: face 0's whole chain, then face 1's, ... in the cubemap
//! face order +X,-X,+Y,-Y,+Z,-Z), level i sized max(1, W>>i) x max(1, H>>i).
//! Formats: bc1 bc3 bc7 etc2-rgb etc2-rgba astc-4x4 astc-6x6 astc-8x8. Exit 0
//! on success, 1 with a message on stderr otherwise - a cook treats any
//! failure as a refused export.

#include "TextureEncode.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace
{
	int fail(std::string const & message)
	{
		std::fprintf(stderr, "texcook: %s\n", message.c_str());
		return 1;
	}
}

//---------------------------------------------------------
int main(int argc, char ** argv)
{
	std::string input;
	std::string output;
	std::string format;
	std::string container;
	std::string quality = "normal";
	int width = 0;
	int height = 0;
	int levels = 1;
	int faces = 1;
	for(int index = 1; index < argc; ++index)
	{
		const std::string argument = argv[index];
		if(index + 1 >= argc)
		{
			return fail("missing value for '" + argument + "'");
		}
		const std::string value = argv[++index];
		if(argument == "--input") { input = value; }
		else if(argument == "--output") { output = value; }
		else if(argument == "--format") { format = value; }
		else if(argument == "--container") { container = value; }
		else if(argument == "--quality") { quality = value; }
		else if(argument == "--width") { width = std::atoi(value.c_str()); }
		else if(argument == "--height") { height = std::atoi(value.c_str()); }
		else if(argument == "--levels") { levels = std::atoi(value.c_str()); }
		else if(argument == "--faces") { faces = std::atoi(value.c_str()); }
		else { return fail("unknown argument '" + argument + "'"); }
	}
	if(input.empty() || output.empty() || format.empty() || container.empty())
	{
		std::fprintf(stderr,
			"usage: texcook --input <levels.rgba> --output <file> --width W "
			"--height H --levels N --format bc1|bc3|bc7|etc2-rgb|etc2-rgba|"
			"astc-4x4|astc-6x6|astc-8x8 --quality low|normal|high "
			"--container dds|ktx|oitd [--faces 1|6]\n");
		return 2;
	}

	std::string error;
	if(!OrkigeExport::TextureEncode::validate(format, quality, width, height,
		levels, faces, container, &error))
	{
		return fail(error);
	}
	OrkigeExport::TextureLevels rgbaLevels;
	if(!OrkigeExport::TextureEncode::readRgbaLevels(input, width, height,
		levels, faces, rgbaLevels, &error))
	{
		return fail(error);
	}
	std::vector<unsigned char> file;
	if(!OrkigeExport::TextureEncode::encodeToContainer(format, quality, width,
		height, faces, rgbaLevels, container, file, &error))
	{
		return fail(error);
	}
	std::ofstream stream(output.c_str(), std::ios::binary | std::ios::trunc);
	if(!stream.write(reinterpret_cast<char const *>(file.data()),
		static_cast<std::streamsize>(file.size())))
	{
		return fail("could not write output '" + output + "'");
	}
	return 0;
}
