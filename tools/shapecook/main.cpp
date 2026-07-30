/********************************************************************
	created:	Wednesday 2026/07/29 at 21:00
	filename: 	main.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	shapecook - cook an SVG drawing into the native .oshape text asset.

	  shapecook <in.svg> <out.oshape> [--extent E] [--tolerance T]
	                                  [--targets pose.svg ...]
	  shapecook --selftest

	The editor performs the same conversion in-process on import; this CLI is
	the batch/generator face of it, and the only way to reach the MORPH SET
	mode (several pose files, one output). A cook error - an unusable drawing,
	a pose whose structure does not match the base, an unreadable file -
	reports as one readable block on stderr and exit 1, never a crash.
*********************************************************************/

#include "engine_gui/SvgShapeCook.h"

#include <core_util/String.h>
#include <core_util/VectorShapeAsset.h>
#include <core_util/VectorShapeCook.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace
{
	using Orkige::String;

	//! @brief read a whole file into bytes; false (+ reason) when it cannot be
	bool readFile(String const & path, std::vector<unsigned char> & out,
		String & error)
	{
		std::ifstream file(path.c_str(), std::ios::binary);
		if(!file)
		{
			error = "cannot open '" + path + "'";
			return false;
		}
		out.assign(std::istreambuf_iterator<char>(file),
			std::istreambuf_iterator<char>());
		return true;
	}

	//! @brief write text out; false (+ reason) when it cannot be
	bool writeFile(String const & path, String const & text, String & error)
	{
		std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
		if(!file)
		{
			error = "cannot write '" + path + "'";
			return false;
		}
		file << text;
		if(!file)
		{
			error = "write failed for '" + path + "'";
			return false;
		}
		return true;
	}

	//! @brief the file stem, the name a pose contributes to its morph target
	String fileStem(String const & path)
	{
		const std::size_t slash = path.find_last_of("/\\");
		String name = slash == String::npos ? path : path.substr(slash + 1);
		const std::size_t dot = name.find_last_of('.');
		return dot == String::npos ? name : name.substr(0, dot);
	}

	//! @brief report a cook/file failure the way every Orkige cook does: one
	//! headline naming the input, then the reason indented, and exit 1
	int reportFailure(String const & input, String const & reason)
	{
		std::cerr << "shapecook: cannot cook " << input << ":\n";
		std::istringstream lines(reason);
		String line;
		while(std::getline(lines, line))
		{
			std::cerr << "  " << line << "\n";
		}
		return 1;
	}

	void usage()
	{
		std::cerr <<
			"usage: shapecook <in.svg> <out.oshape> [--extent E]\n"
			"                 [--tolerance T] [--targets pose.svg ...]\n"
			"       shapecook --selftest\n"
			"\n"
			"  --extent E     world units the drawing's larger side spans "
				"(default 2)\n"
			"  --tolerance T  absolute flatten chord tolerance in the "
				"drawing's own\n"
			"                 units (default: 1% of its larger side)\n"
			"  --targets ...  cook a MORPH SET: each extra pose SVG becomes a\n"
			"                 morph target named after its file stem, and must\n"
			"                 share the base's contour structure\n";
	}

	//--- the self-test -------------------------------------------------
	//! a synthetic drawing exercising the mapped element kinds
	char const * const SELFTEST_SVG =
		"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
		"<rect x=\"10\" y=\"10\" width=\"80\" height=\"60\" fill=\"#e56b61\"/>"
		"<path d=\"M 20 20 L 60 20 L 40 50 Z\" fill=\"rgb(40,40,60)\"/>"
		"<circle cx=\"50\" cy=\"50\" r=\"20\" fill=\"blue\"/>"
		"</svg>";
	char const * const SELFTEST_BASE_SVG =
		"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
		"<polygon points=\"20,20 80,20 80,80 20,80\" fill=\"#88cc44\"/>"
		"</svg>";
	char const * const SELFTEST_SQUASH_SVG =
		"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
		"<polygon points=\"10,35 90,35 90,65 10,65\" fill=\"#88cc44\"/>"
		"</svg>";
	char const * const SELFTEST_TRIANGLE_SVG =
		"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
		"<polygon points=\"50,20 80,80 20,80\" fill=\"#88cc44\"/>"
		"</svg>";
	//! a donut: the inner ring is CONTAINED, so it must cook to a hole
	char const * const SELFTEST_DONUT_SVG =
		"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
		"<path d=\"M 10 10 L 90 10 L 90 90 L 10 90 Z "
		"M 30 30 L 30 70 L 70 70 L 70 30 Z\" fill=\"#334455\"/>"
		"</svg>";

	Orkige::SvgShapeCook::Source source(char const * svg, char const * name)
	{
		Orkige::SvgShapeCook::Source out;
		out.data = reinterpret_cast<unsigned char const *>(svg);
		out.size = int(std::strlen(svg));
		out.name = name;
		return out;
	}

	bool check(bool condition, char const * what)
	{
		if(!condition)
		{
			std::cerr << "shapecook selftest FAILED: " << what << "\n";
		}
		return condition;
	}

	//! cook a synthetic drawing, re-parse the result through the REAL runtime
	//! parser and assert the shape it describes - the tooling-level
	//! cook-then-load smoke every Orkige cook carries
	int selftest()
	{
		Orkige::VectorShapeCook::Options options;
		String text;
		String error;
		if(!check(Orkige::SvgShapeCook::cook(
			reinterpret_cast<unsigned char const *>(SELFTEST_SVG),
			int(std::strlen(SELFTEST_SVG)), options, text, &error),
			"the synthetic drawing did not cook"))
		{
			std::cerr << "  " << error << "\n";
			return 1;
		}
		std::vector<Orkige::VectorTessellator::Region> regions;
		if(!check(Orkige::VectorShapeAsset::parse(text, regions),
			"the cooked .oshape did not re-parse") ||
			!check(regions.size() == 3, "expected 3 regions"))
		{
			return 1;
		}
		std::size_t rectRegion = regions.size();
		std::size_t triangleRegion = regions.size();
		float extentX = 0.0f;
		for(std::size_t i = 0; i < regions.size(); ++i)
		{
			if(!check(regions[i].outer.size() >= 3,
				"a region has fewer than 3 vertices"))
			{
				return 1;
			}
			if(regions[i].outer.size() == 4) { rectRegion = i; }
			if(regions[i].outer.size() == 3) { triangleRegion = i; }
			for(Orkige::VectorTessellator::Point const & p : regions[i].outer)
			{
				extentX = std::max(extentX, std::fabs(p.x));
			}
		}
		if(!check(rectRegion < regions.size() &&
			triangleRegion < regions.size(),
			"the rect/triangle vertex counts did not survive") ||
			!check(std::fabs(regions[rectRegion].fill.r - 0.898f) < 0.01f,
				"the rect's fill colour did not round-trip") ||
			!check(std::fabs(extentX - 1.0f) < 1.0e-4f,
				"the drawing was not scaled to the requested extent"))
		{
			return 1;
		}
		// a contained subpath becomes a HOLE, not a second opaque region
		std::vector<Orkige::VectorTessellator::Region> donut;
		if(!check(Orkige::SvgShapeCook::cook(
			reinterpret_cast<unsigned char const *>(SELFTEST_DONUT_SVG),
			int(std::strlen(SELFTEST_DONUT_SVG)), options, text, &error),
			"the donut did not cook") ||
			!check(Orkige::VectorShapeAsset::parse(text, donut),
				"the donut .oshape did not re-parse") ||
			!check(donut.size() == 1 && donut[0].holes.size() == 1,
				"the donut's inner ring did not cook to a hole"))
		{
			return 1;
		}
		// a morph set: matching poses -> base + one named target
		std::vector<Orkige::SvgShapeCook::Source> poses;
		poses.push_back(source(SELFTEST_BASE_SVG, ""));
		poses.push_back(source(SELFTEST_SQUASH_SVG, "squash"));
		Orkige::VectorShapeAsset::ParsedShape morphed;
		if(!check(Orkige::SvgShapeCook::cookMorphSet(poses, options, text,
			&error), "the morph set did not cook") ||
			!check(Orkige::VectorShapeAsset::parse(text, morphed),
				"the morph .oshape did not re-parse") ||
			!check(morphed.base.size() == 1 && morphed.morphs.size() == 1,
				"expected one base region and one morph target") ||
			!check(morphed.morphs[0].name == "squash",
				"the morph target name was lost") ||
			!check(morphed.base[0].outer.size() ==
				morphed.morphs[0].regions[0].outer.size(),
				"the morph target's vertex count must match the base"))
		{
			return 1;
		}
		// a structure MISMATCH must be a clear refusal, never a bad shape
		poses[1] = source(SELFTEST_TRIANGLE_SVG, "bad");
		error.clear();
		if(!check(!Orkige::SvgShapeCook::cookMorphSet(poses, options, text,
			&error), "a structure mismatch was accepted") ||
			!check(error.find("does not match the base") != String::npos,
				"the mismatch message does not name the structures"))
		{
			return 1;
		}
		// an unusable drawing refuses with a readable reason
		char const * const empty =
			"<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>";
		error.clear();
		if(!check(!Orkige::SvgShapeCook::cook(
			reinterpret_cast<unsigned char const *>(empty),
			int(std::strlen(empty)), options, text, &error),
			"an empty drawing was accepted") ||
			!check(error == "no fillable shapes in the SVG",
				"the empty-drawing reason is not the honest one"))
		{
			return 1;
		}
		std::cout << "shapecook selftest OK: 3 regions cooked and re-parsed, "
			"donut hole detected, morph set (base + 1 target, mismatch "
			"rejected), empty drawing refused\n";
		return 0;
	}
}

int main(int argc, char ** argv)
{
	String input;
	String output;
	std::vector<String> targets;
	Orkige::VectorShapeCook::Options options;
	bool collectingTargets = false;

	for(int i = 1; i < argc; ++i)
	{
		const String argument = argv[i];
		if(argument == "--selftest")
		{
			return selftest();
		}
		if(argument == "--help" || argument == "-h")
		{
			usage();
			return 0;
		}
		if(argument == "--targets")
		{
			collectingTargets = true;
			continue;
		}
		if(argument == "--extent" || argument == "--tolerance")
		{
			if(i + 1 >= argc)
			{
				std::cerr << "shapecook: " << argument << " needs a value\n";
				return 2;
			}
			const double value = std::atof(argv[++i]);
			if(argument == "--extent") { options.extent = value; }
			else { options.tolerance = value; }
			collectingTargets = false;
			continue;
		}
		if(!argument.empty() && argument[0] == '-')
		{
			std::cerr << "shapecook: unknown option '" << argument << "'\n";
			usage();
			return 2;
		}
		if(collectingTargets) { targets.push_back(argument); }
		else if(input.empty()) { input = argument; }
		else if(output.empty()) { output = argument; }
		else
		{
			std::cerr << "shapecook: unexpected argument '" << argument
				<< "'\n";
			return 2;
		}
	}
	if(input.empty() || output.empty())
	{
		std::cerr << "shapecook: an input .svg and an output .oshape are "
			"required (or use --selftest)\n";
		usage();
		return 2;
	}
	if(!(options.extent > 0.0))
	{
		return reportFailure(input, "--extent must be greater than zero");
	}

	std::vector<unsigned char> baseBytes;
	String error;
	if(!readFile(input, baseBytes, error))
	{
		return reportFailure(input, error);
	}
	String text;
	if(targets.empty())
	{
		if(!Orkige::SvgShapeCook::cook(baseBytes.data(), int(baseBytes.size()),
			options, text, &error))
		{
			return reportFailure(input, error);
		}
	}
	else
	{
		// the pose bytes must outlive the cook call, so they are all read first
		std::vector<std::vector<unsigned char> > poseBytes;
		poseBytes.reserve(targets.size());
		for(String const & path : targets)
		{
			poseBytes.push_back(std::vector<unsigned char>());
			if(!readFile(path, poseBytes.back(), error))
			{
				return reportFailure(input, error);
			}
		}
		std::vector<Orkige::SvgShapeCook::Source> poses;
		Orkige::SvgShapeCook::Source base;
		base.data = baseBytes.data();
		base.size = int(baseBytes.size());
		poses.push_back(base);
		for(std::size_t i = 0; i < targets.size(); ++i)
		{
			Orkige::SvgShapeCook::Source pose;
			pose.data = poseBytes[i].data();
			pose.size = int(poseBytes[i].size());
			pose.name = fileStem(targets[i]);
			poses.push_back(pose);
		}
		if(!Orkige::SvgShapeCook::cookMorphSet(poses, options, text, &error))
		{
			return reportFailure(input, error);
		}
	}
	if(!writeFile(output, text, error))
	{
		return reportFailure(input, error);
	}
	if(targets.empty())
	{
		std::cout << "cooked " << input << " -> " << output << "\n";
	}
	else
	{
		std::cout << "cooked morph set " << input << " (+" << targets.size()
			<< " target[s]) -> " << output << "\n";
	}
	return 0;
}
