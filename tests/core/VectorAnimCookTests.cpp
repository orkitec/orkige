/**************************************************************
	created:	2026/07/31 at 10:00
	filename: 	VectorAnimCookTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The Lottie -> .oanim cook. Its contract is REPRODUCIBILITY: the same
	source document must cook to the same bytes, so the committed fixtures
	beside their sources (tests/assets/vectoranim and the pinned character
	corpus under projects/benchmark/assets/lottie) are compared BYTE FOR
	BYTE against a live cook here. A geometry change that shifts one vertex
	past a printed decimal, a compiler that contracts a multiply-add, an
	accumulation order that drifts - each fails here rather than silently
	corrupting an animation nobody re-inspects.

	Beside that, the small in-line documents pin the decisions the byte
	comparison cannot name on its own: the static-document suffix switch,
	the marker/override clip table, image layers becoming textured regions,
	and the named per-layer refusals that keep an unsupported feature from
	cooking to something silently wrong.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "core_util/VectorAnimAsset.h"
#include "core_util/VectorAnimCook.h"
#include "core_util/VectorShapeAsset.h"
#include "core_util/VectorTessellator.h"

#include <fstream>
#include <sstream>
#include <vector>

using namespace Orkige;

namespace
{
	//! read a committed fixture; an empty result means "not there"
	String readAsset(String const & path)
	{
		std::ifstream file(path.c_str(), std::ios::binary);
		if (!file)
		{
			return String();
		}
		std::ostringstream buffer;
		buffer << file.rdbuf();
		return buffer.str();
	}
	//! @brief cook a source document beside its committed output and compare
	//! the two byte for byte, naming the first differing line on a mismatch.
	void requireReproduces(String const & sourcePath,
		String const & expectedPath)
	{
		String source = readAsset(sourcePath);
		INFO("source " << sourcePath);
		REQUIRE_FALSE(source.empty());
		String expected = readAsset(expectedPath);
		INFO("expected " << expectedPath);
		REQUIRE_FALSE(expected.empty());

		VectorAnimCook::Result result;
		String errors;
		INFO("cook errors: " << errors);
		REQUIRE(VectorAnimCook::cook(source, VectorAnimCook::Options(), result,
			errors));
		if (result.text == expected)
		{
			SUCCEED();
			return;
		}
		std::istringstream got(result.text);
		std::istringstream want(expected);
		String gotLine;
		String wantLine;
		int line = 0;
		while (std::getline(want, wantLine))
		{
			++line;
			if (!std::getline(got, gotLine))
			{
				FAIL(sourcePath << ": the cook stopped at line " << line
					<< ", expected " << wantLine);
			}
			if (gotLine != wantLine)
			{
				FAIL(sourcePath << ": line " << line << "\n  expected: "
					<< wantLine << "\n  cooked:   " << gotLine);
			}
		}
		FAIL(sourcePath << ": the cook emitted more than the "
			<< line << " committed lines");
	}
	//! one animated ellipse with two markers - the smallest complete rig
	char const * const ANIMATED_DOCUMENT = R"({
	"v": "5.7.0", "fr": 30, "ip": 0, "op": 60, "w": 200, "h": 200,
	"markers": [{"tm": 0, "cm": "idle", "dr": 30},
	            {"tm": 30, "cm": "walk #once", "dr": 30}],
	"layers": [{"ty": 4, "nm": "body", "ind": 1,
		"ks": {"p": {"a": 1, "k": [{"t": 0, "s": [0, 0]},
		                           {"t": 60, "s": [0, -20]}]},
		       "a": {"a": 0, "k": [0, 0]}, "s": {"a": 0, "k": [100, 100]},
		       "r": {"a": 0, "k": 0}, "o": {"a": 0, "k": 100}},
		"shapes": [{"ty": "gr", "nm": "blob", "it": [
			{"ty": "el", "p": {"a": 0, "k": [0, 0]},
			 "s": {"a": 0, "k": [60, 80]}},
			{"ty": "fl", "c": {"a": 0, "k": [0.9, 0.42, 0.38, 1]},
			 "o": {"a": 0, "k": 100}}]}]}]})";
	//! the same drawing with nothing animated at all
	char const * const STATIC_DOCUMENT = R"({
	"v": "5.7.0", "fr": 30, "ip": 0, "op": 60, "w": 200, "h": 200,
	"layers": [{"ty": 4, "nm": "body", "ind": 1,
		"ks": {"p": {"a": 0, "k": [0, 0]}, "a": {"a": 0, "k": [0, 0]},
		       "s": {"a": 0, "k": [100, 100]}, "r": {"a": 0, "k": 0},
		       "o": {"a": 0, "k": 100}},
		"shapes": [{"ty": "gr", "nm": "blob", "it": [
			{"ty": "el", "p": {"a": 0, "k": [0, 0]},
			 "s": {"a": 0, "k": [60, 80]}},
			{"ty": "fl", "c": {"a": 0, "k": [0.9, 0.42, 0.38, 1]},
			 "o": {"a": 0, "k": 100}}]}]}]})";
	//! an image layer travelling across the composition
	char const * const IMAGE_DOCUMENT = R"({
	"v": "5.7.0", "fr": 30, "ip": 0, "op": 60, "w": 200, "h": 200,
	"assets": [{"id": "img_0", "w": 64, "h": 32, "u": "images/",
	            "p": "arm.png"}],
	"layers": [{"ty": 2, "nm": "photo", "ind": 1, "refId": "img_0",
		"ks": {"p": {"a": 1, "k": [{"t": 0, "s": [0, 0]},
		                           {"t": 60, "s": [40, 0]}]},
		       "a": {"a": 0, "k": [0, 0]}, "s": {"a": 0, "k": [100, 100]},
		       "r": {"a": 0, "k": 0}, "o": {"a": 0, "k": 100}}}]})";
	//! does the cooked text carry this line (leading indentation ignored)?
	bool hasLine(String const & text, String const & needle)
	{
		std::istringstream stream(text);
		String line;
		while (std::getline(stream, line))
		{
			size_t begin = line.find_first_not_of(" \t");
			if (begin != String::npos && line.substr(begin) == needle)
			{
				return true;
			}
		}
		return false;
	}
	//! cook and expect a refusal; hands back the joined error listing
	String cookRefusal(String const & document)
	{
		VectorAnimCook::Result result;
		String errors;
		REQUIRE_FALSE(VectorAnimCook::cook(document,
			VectorAnimCook::Options(), result, errors));
		return errors;
	}
}

//-------------------------------------------------------------
// the reproducibility contract
//-------------------------------------------------------------
TEST_CASE("vector_anim_cook_reproduces_the_committed_fixtures",
	"[unit][vectoranim][cook]")
{
	String const dir = ORKIGE_TESTS_ASSET_DIR "/vectoranim/";
	// roundtrip: transforms, parenting, easing and the marker clip table
	requireReproduces(dir + "roundtrip.json", dir + "roundtrip.oanim");
	// stroke: centrelines, caps/joins, animated width
	requireReproduces(dir + "stroke.json", dir + "stroke.oanim");
	// modifiers: rounded corners, pucker/bloat, dashes, group motion
	requireReproduces(dir + "modifiers.json", dir + "modifiers.oanim");
}

TEST_CASE("vector_anim_cook_reproduces_the_character_corpus",
	"[unit][vectoranim][cook][corpus]")
{
	// the pinned real-world documents: the only fixtures with the shape
	// density, nesting and keyframe volume that expose an accumulation or
	// rounding difference at all
	String const dir = ORKIGE_BENCHMARK_ASSET_DIR "/lottie/";
	char const * const NAMES[5] = { "hamster", "dragon", "cat_loader",
		"frog_vr", "snail" };
	for (char const * name : NAMES)
	{
		requireReproduces(dir + name + ".json", dir + name + ".oanim");
	}
}

//-------------------------------------------------------------
// what the byte comparison cannot name on its own
//-------------------------------------------------------------
TEST_CASE("vector_anim_cook_emits_a_parseable_rig", "[unit][vectoranim][cook]")
{
	VectorAnimCook::Result result;
	String errors;
	REQUIRE(VectorAnimCook::cook(ANIMATED_DOCUMENT, VectorAnimCook::Options(),
		result, errors));
	REQUIRE(result.kind == VectorAnimCook::KIND_OANIM);

	// the cooked text is what the RUNTIME parser accepts, not a private form
	VectorAnimAsset::Document doc;
	VectorAnimAsset::ParseError error;
	INFO("line " << error.line << ": " << error.message);
	REQUIRE(VectorAnimAsset::parse(result.text, doc, &error));
	CHECK(doc.fps == 30.0f);
	CHECK(doc.duration == 60.0f);

	// markers became the clip table; the '#once' suffix made walk a one-shot
	REQUIRE(doc.clips.size() == 2u);
	CHECK(doc.clips[0].name == "idle");
	CHECK(doc.clips[0].loop);
	CHECK(doc.clips[1].name == "walk");
	CHECK_FALSE(doc.clips[1].loop);

	// the synthetic root centres the composition on the origin
	REQUIRE(doc.layers.size() >= 2u);
	CHECK(doc.layers[0].name == "comp");
	CHECK(doc.layers[0].parent == -1);
	CHECK(hasLine(result.text, "kf 0 -1.00000 1.00000"));

	// -20 source pixels up is +0.2 world units (y flip and the extent scale)
	CHECK(hasLine(result.text, "kf 60 0.00000 0.20000"));
}

TEST_CASE("vector_anim_cook_overrides_markers_with_explicit_clips",
	"[unit][vectoranim][cook]")
{
	VectorAnimCook::Options options;
	options.clips = "one:0:20:once,two:20:60";
	VectorAnimCook::Result result;
	String errors;
	REQUIRE(VectorAnimCook::cook(ANIMATED_DOCUMENT, options, result, errors));
	CHECK(hasLine(result.text, "clip one 0 20 once"));
	CHECK(hasLine(result.text, "clip two 20 60 loop"));
	CHECK_FALSE(hasLine(result.text, "clip idle 0 30 loop"));

	// an empty range is named, never silently dropped
	options.clips = "bad:30:30";
	String message;
	VectorAnimCook::Result refused;
	REQUIRE_FALSE(VectorAnimCook::cook(ANIMATED_DOCUMENT, options, refused,
		message));
	CHECK(message.find("empty frame range") != String::npos);
}

TEST_CASE("vector_anim_cook_switches_a_static_document_to_a_shape",
	"[unit][vectoranim][cook]")
{
	VectorAnimCook::Result result;
	String errors;
	REQUIRE(VectorAnimCook::cook(STATIC_DOCUMENT, VectorAnimCook::Options(),
		result, errors));
	REQUIRE(result.kind == VectorAnimCook::KIND_OSHAPE);
	CHECK(result.text.find("layer ") == String::npos);

	// and it is a real `.oshape`, not merely shaped like one
	std::vector<VectorTessellator::Region> regions;
	REQUIRE(VectorShapeAsset::parse(result.text, regions));
	REQUIRE(regions.size() == 1u);
	CHECK(regions[0].outer.size() >= 12u);
}

TEST_CASE("vector_anim_cook_maps_an_image_layer_to_a_textured_region",
	"[unit][vectoranim][cook]")
{
	VectorAnimCook::Result result;
	String errors;
	REQUIRE(VectorAnimCook::cook(IMAGE_DOCUMENT, VectorAnimCook::Options(),
		result, errors));
	// a texture needs the newer grammar, so the version climbs - and only
	// then (an untextured cook must stay on the older stamp)
	CHECK(hasLine(result.text, "version 3"));
	CHECK(result.text.find("texture arm.png ") != String::npos);

	// the referenced file rides along for the caller to materialize
	REQUIRE(result.images.size() == 1u);
	CHECK(result.images[0].name == "arm.png");
	CHECK_FALSE(result.images[0].embedded);
	CHECK(result.images[0].source == "images/arm.png");

	// a textured rig NEVER degrades to a static shape: the `.oshape` emitter
	// bakes transforms into vertices, which an axis-aligned texture rect
	// cannot follow
	VectorAnimCook::Result stillARig;
	String staticImage = IMAGE_DOCUMENT;
	size_t animated = staticImage.find("\"a\": 1");
	REQUIRE(animated != String::npos);
	staticImage.replace(animated, 6, "\"a\": 0");
	REQUIRE(VectorAnimCook::cook(staticImage, VectorAnimCook::Options(),
		stillARig, errors));
	CHECK(stillARig.kind == VectorAnimCook::KIND_OANIM);
}

TEST_CASE("vector_anim_cook_names_every_refusal", "[unit][vectoranim][cook]")
{
	// a malformed document refuses before anything else is attempted
	CHECK(cookRefusal("{ not json").find("not valid JSON") != String::npos);
	CHECK(cookRefusal("{\"fr\": 30}").find("not a Lottie document") !=
		String::npos);

	// out-of-subset layer kinds name the FEATURE and the LAYER, so an artist
	// knows what to change and where
	String const header = "{\"v\":\"5.7.0\",\"fr\":30,\"ip\":0,\"op\":60,"
		"\"w\":200,\"h\":200,\"layers\":[{\"ty\":4,\"nm\":\"hero\","
		"\"ind\":1,\"ks\":{\"p\":{\"a\":0,\"k\":[0,0]}}";
	String const shapes = ",\"shapes\":[{\"ty\":\"gr\",\"it\":["
		"{\"ty\":\"el\",\"p\":{\"a\":0,\"k\":[0,0]},"
		"\"s\":{\"a\":0,\"k\":[50,50]}},"
		"{\"ty\":\"fl\",\"c\":{\"a\":0,\"k\":[1,0,0,1]},"
		"\"o\":{\"a\":0,\"k\":100}}]}]}]}";

	String matte = cookRefusal(header + ",\"tt\":1" + shapes);
	CHECK(matte.find("track matte") != String::npos);
	CHECK(matte.find("hero") != String::npos);

	CHECK(cookRefusal(header + ",\"ddd\":1" + shapes).find("3D layer") !=
		String::npos);
	CHECK(cookRefusal(header + ",\"sr\":2" + shapes).find("time stretch") !=
		String::npos);
	CHECK(cookRefusal(header + ",\"ao\":1" + shapes).find("auto-orient") !=
		String::npos);

	// an unsupported shape item names the item, not just the layer
	String repeater = cookRefusal(header +
		",\"shapes\":[{\"ty\":\"gr\",\"it\":["
		"{\"ty\":\"el\",\"p\":{\"a\":0,\"k\":[0,0]},"
		"\"s\":{\"a\":0,\"k\":[50,50]}},{\"ty\":\"rp\"}]}]}]}");
	CHECK(repeater.find("repeater") != String::npos);

	// an expression is a program, not a reference: refused with the property
	String expression = cookRefusal(
		"{\"v\":\"5.7.0\",\"fr\":30,\"ip\":0,\"op\":60,\"w\":200,\"h\":200,"
		"\"layers\":[{\"ty\":4,\"nm\":\"hero\",\"ind\":1,\"ks\":{\"p\":"
		"{\"a\":0,\"k\":[0,0],\"x\":\"var $bm_rt = [0,0];\"}}" + shapes);
	CHECK(expression.find("expression") != String::npos);
	CHECK(expression.find("position") != String::npos);

	// a document with nothing to fill says so instead of writing an empty rig
	CHECK(cookRefusal("{\"v\":\"5.7.0\",\"fr\":30,\"ip\":0,\"op\":60,"
		"\"w\":200,\"h\":200,\"layers\":[{\"ty\":3,\"nm\":\"empty\","
		"\"ind\":1,\"ks\":{}}]}").find("no fillable shapes") != String::npos);

	// every broken header field is reported, not just the first
	String header_errors = cookRefusal("{\"layers\":[]}");
	CHECK(header_errors.find("frame rate") != String::npos);
	CHECK(header_errors.find("empty timeline") != String::npos);
	CHECK(header_errors.find("composition size") != String::npos);
}

TEST_CASE("vector_anim_cook_resolves_a_transform_link_expression",
	"[unit][vectoranim][cook]")
{
	// the ONE expression form that is a reference rather than a program:
	// it bakes to a copy of the referenced keyframes and cooks normally
	String document = R"({
	"v": "5.7.0", "fr": 30, "ip": 0, "op": 60, "w": 200, "h": 200,
	"layers": [
	{"ty": 3, "nm": "driver", "ind": 1,
	 "ks": {"p": {"a": 1, "k": [{"t": 0, "s": [0, 0]},
	                            {"t": 60, "s": [0, -20]}]}}},
	{"ty": 4, "nm": "follower", "ind": 2,
	 "ks": {"p": {"a": 0, "k": [0, 0],
	              "x": "var $bm_rt = thisComp.layer('driver').transform.position;"}},
	 "shapes": [{"ty": "gr", "it": [
		{"ty": "el", "p": {"a": 0, "k": [0, 0]},
		 "s": {"a": 0, "k": [50, 50]}},
		{"ty": "fl", "c": {"a": 0, "k": [1, 0, 0, 1]},
		 "o": {"a": 0, "k": 100}}]}]}]})";
	VectorAnimCook::Result result;
	String errors;
	INFO(errors);
	REQUIRE(VectorAnimCook::cook(document, VectorAnimCook::Options(), result,
		errors));
	// the follower ends where the driver does, with no expression left to
	// refuse
	CHECK(hasLine(result.text, "kf 60 0.00000 0.20000"));
}
