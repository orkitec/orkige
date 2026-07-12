/**************************************************************
	created:	2026/07/12 at 12:00
	filename: 	BenchmarkRecorderTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core_debug/BenchmarkRecorder.h"
#include "core_debugnet/Json.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace Orkige;

namespace
{
	//! a frame sample with just a frame-ms value (the common test case)
	BenchmarkFrameSample msSample(double frameMs)
	{
		BenchmarkFrameSample s;
		s.frameMs = frameMs;
		return s;
	}

	//! read a whole file to a string
	String slurp(String const & path)
	{
		std::ifstream in(path.c_str(), std::ios::binary);
		std::ostringstream out;
		out << in.rdbuf();
		return out.str();
	}

	//! split JSONL text into its non-empty lines
	std::vector<String> lines(String const & text)
	{
		std::vector<String> out;
		std::istringstream in(text);
		String line;
		while (std::getline(in, line))
		{
			if (!line.empty())
			{
				out.push_back(line);
			}
		}
		return out;
	}
}

// ============================ aggregation math =============================
// The pure BenchmarkSceneStats reduction is the contract: feed synthetic
// per-frame samples with a KNOWN distribution and assert min/avg/max/p95 (and
// the phase/alloc/gpu means) exactly.
TEST_CASE("BenchmarkSceneStats reduces frame-ms exactly", "[unit][benchmark]")
{
	BenchmarkSceneStats stats;
	// twenty frames with frame times 1.0 .. 20.0 ms, plus per-frame allocs,
	// triangles, RSS and two named phases scaled off the frame index
	for (int i = 1; i <= 20; ++i)
	{
		BenchmarkFrameSample s;
		s.frameMs = static_cast<double>(i);
		s.allocCount = static_cast<std::size_t>(i);
		s.rssBytes = static_cast<std::size_t>(i) * 1000u;
		s.triangles = static_cast<unsigned int>(i) * 100u;
		s.batches = static_cast<unsigned int>(i) * 2u;
		s.texMemMB = static_cast<float>(i);
		s.phasesMs.emplace_back("render", static_cast<double>(i) * 0.5);
		s.phasesMs.emplace_back("physics", static_cast<double>(i) * 0.1);
		stats.addFrame(s);
	}

	REQUIRE(stats.frames() == 20);
	// min / max / avg (1..20 => min 1, max 20, mean 10.5)
	CHECK_THAT(stats.minFrameMs(), WithinAbs(1.0, 1e-9));
	CHECK_THAT(stats.maxFrameMs(), WithinAbs(20.0, 1e-9));
	CHECK_THAT(stats.avgFrameMs(), WithinAbs(10.5, 1e-9));
	// nearest-rank percentiles, integer index = floor(p*n/100), clamped:
	//   p50 => floor(50*20/100)=10 => sorted[10] = 11.0
	//   p95 => floor(95*20/100)=19 => sorted[19] = 20.0
	//   p99 => floor(99*20/100)=19 => sorted[19] = 20.0
	CHECK_THAT(stats.percentileFrameMs(50.0), WithinAbs(11.0, 1e-9));
	CHECK_THAT(stats.percentileFrameMs(95.0), WithinAbs(20.0, 1e-9));
	CHECK_THAT(stats.percentileFrameMs(99.0), WithinAbs(20.0, 1e-9));
	// totalSeconds = sum(1..20)/1000 = 210/1000
	CHECK_THAT(stats.totalSeconds(), WithinAbs(0.210, 1e-9));
	// avgFps = 1000 / 10.5
	CHECK_THAT(stats.avgFps(), WithinAbs(1000.0 / 10.5, 1e-6));

	// allocs: sum 210 over 20 frames => avg 10.5, peak the last frame (20)
	CHECK_THAT(stats.allocPerFrameAvg(), WithinAbs(10.5, 1e-9));
	CHECK(stats.allocPeakFrame() == 20u);
	CHECK(stats.rssPeakBytes() == 20000u);

	// gpu means: triangles 100*(1..20) => mean 100*10.5 = 1050; batches 2*10.5;
	// texMemMB mean 10.5
	CHECK_THAT(stats.trianglesAvg(), WithinAbs(1050.0, 1e-6));
	CHECK_THAT(stats.batchesAvg(), WithinAbs(21.0, 1e-6));
	CHECK_THAT(stats.texMemMBAvg(), WithinAbs(10.5, 1e-6));

	// phase means, sorted by name: physics = 0.1*10.5 = 1.05, render = 0.5*10.5
	std::vector<std::pair<String, double>> const phases = stats.phaseMeansMs();
	REQUIRE(phases.size() == 2);
	CHECK(phases[0].first == "physics");
	CHECK_THAT(phases[0].second, WithinAbs(1.05, 1e-9));
	CHECK(phases[1].first == "render");
	CHECK_THAT(phases[1].second, WithinAbs(5.25, 1e-9));
}

TEST_CASE("BenchmarkSceneStats handles empty and single-sample cases",
	"[unit][benchmark]")
{
	BenchmarkSceneStats empty;
	CHECK(empty.frames() == 0);
	CHECK_THAT(empty.avgFrameMs(), WithinAbs(0.0, 1e-9));
	CHECK_THAT(empty.percentileFrameMs(95.0), WithinAbs(0.0, 1e-9));
	CHECK_THAT(empty.avgFps(), WithinAbs(0.0, 1e-9));

	BenchmarkSceneStats one;
	one.addFrame(msSample(7.0));
	CHECK(one.frames() == 1);
	CHECK_THAT(one.minFrameMs(), WithinAbs(7.0, 1e-9));
	CHECK_THAT(one.maxFrameMs(), WithinAbs(7.0, 1e-9));
	// every percentile of a single sample is that sample
	CHECK_THAT(one.percentileFrameMs(50.0), WithinAbs(7.0, 1e-9));
	CHECK_THAT(one.percentileFrameMs(95.0), WithinAbs(7.0, 1e-9));
	CHECK_THAT(one.percentileFrameMs(100.0), WithinAbs(7.0, 1e-9));
}

// ============================ artifact schema =============================
// The recorder writes a JSONL artifact: one "meta" line, one "scene" line per
// recorded scene, a closing "summary" line. Assert the shape (every line is
// valid JSON with the documented keys) end to end through the real file path.
TEST_CASE("BenchmarkRecorder writes a well-formed JSONL artifact",
	"[unit][benchmark]")
{
	namespace fs = std::filesystem;
	const auto unique = std::chrono::steady_clock::now()
		.time_since_epoch().count();
	const fs::path dir = fs::temp_directory_path() /
		("orkige_benchmark_test_" + std::to_string(unique));
	fs::create_directories(dir);
	const String file = (dir / "benchmark-test.jsonl").string();

	{
		BenchmarkRecorder recorder;
		BenchmarkMeta meta;
		meta.engineSha = "abc1234";
		meta.flavor = "next";
		meta.renderSystem = "Metal";
		meta.build = "Debug";
		meta.platform = "macos";
		meta.deviceModel = "Mac";
		meta.deviceOs = "macOS";
		meta.deviceGpu = "Apple";
		meta.scenario = "smoke";
		meta.project = "vista";
		recorder.setMeta(meta);

		// disarmed until a file is set: every entry point is a no-op
		CHECK_FALSE(recorder.isArmed());
		recorder.beginScene("before-arm");
		recorder.sampleFrame(0, 0, 0.0f);
		CHECK(recorder.scenesWritten() == 0);

		recorder.setFile(file);
		CHECK(recorder.isArmed());

		recorder.beginScene("sceneA");
		for (int i = 1; i <= 5; ++i)
		{
			recorder.addFrame(msSample(static_cast<double>(i)));
		}
		recorder.beginScene("sceneB");	// composes: closes sceneA, opens sceneB
		for (int i = 1; i <= 3; ++i)
		{
			recorder.addFrame(msSample(10.0));
		}
		recorder.finish(false);
		CHECK(recorder.scenesWritten() == 2);
	}

	const String text = slurp(file);
	std::vector<String> const jsonl = lines(text);
	REQUIRE(jsonl.size() == 4);	// meta + 2 scenes + summary

	// every line parses as JSON
	for (String const & line : jsonl)
	{
		JsonValue value;
		INFO("line: " << line);
		REQUIRE(JsonValue::parse(line, value));
		REQUIRE(value.isObject());
	}

	// meta line
	JsonValue metaLine;
	REQUIRE(JsonValue::parse(jsonl[0], metaLine));
	CHECK(metaLine.get("type").asString() == "meta");
	CHECK(metaLine.get("schema").asInt() == 1);
	CHECK(metaLine.get("engineSha").asString() == "abc1234");
	CHECK(metaLine.get("flavor").asString() == "next");
	CHECK(metaLine.get("renderSystem").asString() == "Metal");
	CHECK(metaLine.get("scenario").asString() == "smoke");
	CHECK(metaLine.get("project").asString() == "vista");
	REQUIRE(metaLine.get("device").isObject());
	CHECK(metaLine.get("device").get("os").asString() == "macOS");

	// first scene line
	JsonValue sceneA;
	REQUIRE(JsonValue::parse(jsonl[1], sceneA));
	CHECK(sceneA.get("type").asString() == "scene");
	CHECK(sceneA.get("name").asString() == "sceneA");
	CHECK(sceneA.get("frames").asInt() == 5);
	REQUIRE(sceneA.get("frameMs").isObject());
	CHECK_THAT(sceneA.get("frameMs").get("min").asNumber(), WithinAbs(1.0, 1e-3));
	CHECK_THAT(sceneA.get("frameMs").get("max").asNumber(), WithinAbs(5.0, 1e-3));
	CHECK_THAT(sceneA.get("frameMs").get("avg").asNumber(), WithinAbs(3.0, 1e-3));
	REQUIRE(sceneA.get("allocs").isObject());
	REQUIRE(sceneA.get("gpu").isObject());
	REQUIRE(sceneA.get("subsystemsMs").isObject());

	// second scene line
	JsonValue sceneB;
	REQUIRE(JsonValue::parse(jsonl[2], sceneB));
	CHECK(sceneB.get("name").asString() == "sceneB");
	CHECK(sceneB.get("frames").asInt() == 3);

	// summary line
	JsonValue summary;
	REQUIRE(JsonValue::parse(jsonl[3], summary));
	CHECK(summary.get("type").asString() == "summary");
	CHECK(summary.get("scenes").asInt() == 2);
	CHECK(summary.get("aborted").asBool() == false);

	std::error_code ignored;
	fs::remove_all(dir, ignored);
}
