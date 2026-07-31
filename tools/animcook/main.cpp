/********************************************************************
	created:	Thursday 2026/07/31 at 10:00
	filename: 	main.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	animcook - cook a Lottie JSON animation into the native .oanim rig.

	  animcook <in.json> [out.oanim] [--extent E] [--tolerance T]
	                                 [--clips name:start:end[:loop|once],...]

	The editor performs the same conversion in-process on import; this CLI is
	the batch/generator face of it (the shapecook precedent). A document where
	nothing animates cooks to a plain .oshape instead - the output suffix
	switches and the tool says so. Image layers reference files: an embedded
	base64 PNG is written beside the output, a file-referenced one is copied
	there, and a missing source refuses BEFORE any rig lands. A cook error
	reports as one readable block on stderr and exit 1, never a crash.
*********************************************************************/

#include <core_util/String.h>
#include <core_util/VectorAnimCook.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace
{
	using Orkige::String;
	using Orkige::VectorAnimCook;

	//! @brief read a whole file into a string; false (+ reason) when it cannot
	bool readFile(String const & path, String & out, String & error)
	{
		std::ifstream file(path.c_str(), std::ios::binary);
		if (!file)
		{
			error = "cannot read " + path;
			return false;
		}
		std::ostringstream buffer;
		buffer << file.rdbuf();
		out = buffer.str();
		return true;
	}
	//! @brief write bytes out; false (+ reason) when it cannot be
	bool writeFile(String const & path, char const * data, size_t size,
		String & error)
	{
		std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
		if (!file)
		{
			error = "cannot write " + path;
			return false;
		}
		file.write(data, static_cast<std::streamsize>(size));
		if (!file)
		{
			error = "cannot write " + path;
			return false;
		}
		return true;
	}
	//! the directory part of a path, with its trailing separator ("" for none)
	String directoryOf(String const & path)
	{
		size_t slash = path.find_last_of("/\\");
		return slash == String::npos ? String() : path.substr(0, slash + 1);
	}
	//! the path with its suffix replaced (a suffix-less path just gains one)
	String withSuffix(String const & path, String const & suffix)
	{
		size_t slash = path.find_last_of("/\\");
		size_t dot = path.find_last_of('.');
		if (dot == String::npos || (slash != String::npos && dot < slash))
		{
			return path + suffix;
		}
		return path.substr(0, dot) + suffix;
	}
	//! @brief the post-cook readback: the clip table, fps/duration and layer
	//! count of a cooked rig, so the first question after a cook - what are the
	//! clips called - is answered without opening the file.
	String summarize(String const & cooked)
	{
		String fps;
		String duration;
		std::vector<String> clips;
		int layers = 0;
		std::istringstream stream(cooked);
		String line;
		while (std::getline(stream, line))
		{
			std::istringstream words(line);
			std::vector<String> parts;
			String word;
			while (words >> word)
			{
				parts.push_back(word);
			}
			if (parts.empty())
			{
				continue;
			}
			if (parts[0] == "fps" && parts.size() > 1)
			{
				fps = parts[1];
			}
			else if (parts[0] == "duration" && parts.size() > 1)
			{
				duration = parts[1];
			}
			else if (parts[0] == "clip" && parts.size() > 4)
			{
				clips.push_back(parts[1] + " " + parts[2] + "-" + parts[3] +
					" " + parts[4]);
			}
			else if (parts[0] == "layer")
			{
				++layers;
			}
		}
		String out = "  clips: ";
		if (clips.empty())
		{
			out += "no markers in the document - the runtime plays one looping "
				"clip named 'default' over the whole timeline";
		}
		else
		{
			for (size_t index = 0; index < clips.size(); ++index)
			{
				if (index > 0)
				{
					out += ", ";
				}
				out += clips[index];
			}
		}
		std::ostringstream tail;
		tail << "\n  " << fps << " fps, " << duration << " frames, " << layers
			<< " layer(s)";
		return out + tail.str();
	}
	//---------------------------------------------------------
	void usage()
	{
		std::cerr <<
			"usage: animcook <in.json> [out.oanim] [--extent E]\n"
			"                [--tolerance T] [--clips SPEC]\n";
	}
}

int main(int argc, char ** argv)
{
	String input;
	String output;
	VectorAnimCook::Options options;
	for (int index = 1; index < argc; ++index)
	{
		String argument = argv[index];
		auto value = [&](String & target)
		{
			if (index + 1 >= argc)
			{
				std::cerr << "animcook: " << argument << " needs a value\n";
				std::exit(2);
			}
			target = argv[++index];
		};
		if (argument == "--extent")
		{
			String text;
			value(text);
			options.extent = strtod(text.c_str(), nullptr);
		}
		else if (argument == "--tolerance")
		{
			String text;
			value(text);
			options.tolerance = strtod(text.c_str(), nullptr);
		}
		else if (argument == "--clips")
		{
			value(options.clips);
		}
		else if (argument == "--help" || argument == "-h")
		{
			usage();
			return 0;
		}
		else if (!argument.empty() && argument[0] == '-')
		{
			std::cerr << "animcook: unknown option " << argument << "\n";
			return 2;
		}
		else if (input.empty())
		{
			input = argument;
		}
		else if (output.empty())
		{
			output = argument;
		}
		else
		{
			usage();
			return 2;
		}
	}
	if (input.empty())
	{
		usage();
		return 2;
	}

	String text;
	String error;
	if (!readFile(input, text, error))
	{
		std::cerr << "animcook: " << error << "\n";
		return 1;
	}
	VectorAnimCook::Result result;
	String errors;
	if (!VectorAnimCook::cook(text, options, result, errors))
	{
		std::cerr << "animcook: cannot cook " << input << ":\n";
		std::istringstream stream(errors);
		String line;
		while (std::getline(stream, line))
		{
			std::cerr << "  " << line << "\n";
		}
		return 1;
	}

	String outPath = output.empty() ? withSuffix(input, ".oanim") : output;
	if (result.kind == VectorAnimCook::KIND_OSHAPE)
	{
		outPath = withSuffix(outPath, ".oshape");
		std::cout << "nothing animates - cooked a static .oshape instead\n";
	}
	// materialize the referenced images beside the output FIRST (a missing
	// source refuses before any rig lands - never a rig whose textures cannot
	// resolve)
	String outDirectory = directoryOf(outPath);
	String inDirectory = directoryOf(input);
	for (VectorAnimCook::Image const & image : result.images)
	{
		String destination = outDirectory + image.name;
		if (image.embedded)
		{
			if (!writeFile(destination,
				reinterpret_cast<char const *>(image.data.data()),
				image.data.size(), error))
			{
				std::cerr << "animcook: " << error << "\n";
				return 1;
			}
			std::cout << "wrote embedded image " << destination << "\n";
			continue;
		}
		String source = inDirectory + image.source;
		String payload;
		if (!readFile(source, payload, error))
		{
			std::cerr << "animcook: image file " << source << " (referenced by "
				<< input << ") not found\n";
			return 1;
		}
		if (source != destination)
		{
			if (!writeFile(destination, payload.data(), payload.size(), error))
			{
				std::cerr << "animcook: " << error << "\n";
				return 1;
			}
			std::cout << "copied image " << source << " -> " << destination
				<< "\n";
		}
	}
	if (!writeFile(outPath, result.text.data(), result.text.size(), error))
	{
		std::cerr << "animcook: " << error << "\n";
		return 1;
	}
	std::cout << "cooked " << input << " -> " << outPath << "\n";
	if (result.kind == VectorAnimCook::KIND_OANIM)
	{
		std::cout << summarize(result.text) << "\n";
	}
	return 0;
}
