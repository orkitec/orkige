/**************************************************************
	LottieCorpusTests.cpp - end-to-end native-vector conformance for the
	pinned, MIT-licensed character corpus. Every cooked asset traverses the
	real C++ parser, evaluator, hierarchy composer and tessellator at several
	timeline positions. No renderer/window is required.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "core_util/VectorAnimAsset.h"
#include "core_util/VectorAnimEval.h"
#include "core_util/VectorShapeRaster.h"
#include "core_util/VectorTessellator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <sstream>
#include <vector>

using namespace Orkige;

TEST_CASE("lottie_character_corpus_is_native_vector_and_topology_stable",
	"[unit][lottie][vectoranim][corpus]")
{
	const char * names[] = {
		"hamster", "dragon", "cat_loader", "frog_vr", "snail"
	};
	bool sawOffCentreRadialGradient = false;
	for(const char * name : names)
	{
		CAPTURE(name);
		const String path = String(ORKIGE_BENCHMARK_ASSET_DIR) +
			"/lottie/" + name + ".oanim";
		std::ifstream input(path.c_str(), std::ios::binary);
		REQUIRE(input.good());
		std::ostringstream buffer;
		buffer << input.rdbuf();

		VectorAnimAsset::Document document;
		REQUIRE(VectorAnimAsset::parse(buffer.str(), document));
		VectorAnimEval evaluator;
		REQUIRE(evaluator.build(document));
		REQUIRE(evaluator.shapeCount() > 0u);

		std::vector<std::size_t> stableOuterCounts;
		std::vector<std::uint64_t> frameHashes;
		for(float fraction : {0.0f, 0.23f, 0.51f, 0.83f})
		{
			VectorAnimEval::Pose pose;
			const float seconds = document.duration * fraction / document.fps;
			REQUIRE(evaluator.evaluateAt(0, seconds, pose));
			std::vector<VectorTessellator::Region> regions;
			evaluator.composeRegions(pose, regions);
			REQUIRE_FALSE(regions.empty());
			for(VectorTessellator::Region const & region : regions)
			{
				if(region.paintType == VectorTessellator::PAINT_RADIAL_GRADIENT &&
					std::hypot(region.gradientFocal.x - region.gradientStart.x,
						region.gradientFocal.y - region.gradientStart.y) > 1.0e-5f)
				{
					sawOffCentreRadialGradient = true;
				}
			}
			if(stableOuterCounts.empty())
			{
				for(VectorTessellator::Region const & region : regions)
					stableOuterCounts.push_back(region.outer.size());
			}
			REQUIRE(regions.size() == stableOuterCounts.size());
			for(std::size_t i = 0; i < regions.size(); ++i)
				CHECK(regions[i].outer.size() == stableOuterCounts[i]);
			VectorTessellator::Mesh mesh;
			const VectorTessellator::Bounds bounds =
				VectorTessellator::computeBounds(regions);
			VectorTessellator::build(regions,
				VectorTessellator::defaultFeatherWidth(bounds), mesh);
			REQUIRE(mesh.triangleCount() > 0u);
			REQUIRE(mesh.positions.size() == mesh.colours.size());
			bool indicesValid = true;
			for(unsigned int index : mesh.indices)
				indicesValid = indicesValid && index < mesh.positions.size();
			CHECK(indicesValid);

			// Exercise the exact CPU raster used by both the asset thumbnail and
			// Animation Preview. A valid mesh is insufficient: a transparent or
			// solid-white output is a broken human preview.
			constexpr int SIDE = 128;
			std::vector<unsigned char> pixels(SIDE * SIDE * 4, 0);
			VectorShapeRaster::rasterize(mesh, SIDE, SIDE, pixels.data());
			std::size_t visible = 0;
			std::size_t coloured = 0;
			std::uint64_t hash = 1469598103934665603ull;
			for(std::size_t p = 0; p + 3 < pixels.size(); p += 4)
			{
				for(int channel = 0; channel < 4; ++channel)
				{
					hash ^= pixels[p + channel];
					hash *= 1099511628211ull;
				}
				if(pixels[p + 3] > 8)
				{
					++visible;
					if(pixels[p] < 245 || pixels[p + 1] < 245 ||
						pixels[p + 2] < 245)
					{
						++coloured;
					}
				}
			}
			CHECK(visible > 100u);
			CHECK(visible < static_cast<std::size_t>(SIDE * SIDE * 9 / 10));
			CHECK(coloured > 100u);
			frameHashes.push_back(hash);
		}
		CHECK(std::adjacent_find(frameHashes.begin(), frameHashes.end(),
			std::not_equal_to<std::uint64_t>()) != frameHashes.end());
	}
	CHECK(sawOffCentreRadialGradient);
}
