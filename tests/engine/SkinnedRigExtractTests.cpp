/**************************************************************
	created:	2026/07/17 at 12:00
	filename: 	SkinnedRigExtractTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tests of the backend-neutral skinned-rig extraction
	(engine_render/SkinnedRigExtract.h) against the generated character
	rig fixture (Util/make_character_rig.py - a known seven-joint
	mannequin with walk/idle clips). This pins the ONE skeleton/clip/skin
	semantics both importer roads consume, sharper and cheaper than only
	probing it end to end through a render backend; the runtime motion
	proof stays player_character_rig_selfcheck on both flavors.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <engine_render/SkinnedRigExtract.h>

#include <fstream>
#include <map>
#include <vector>

using Catch::Approx;

namespace
{
	// the generated skinned mannequin (tests/projects/character/assets/
	// character_rig.glb), path injected by CMake - read straight off disk so
	// the extraction is exercised without the resource system or a renderer
	std::vector<char> readRigFixture()
	{
		std::ifstream file(ORKIGE_CHARACTER_RIG_GLB, std::ios::binary);
		return std::vector<char>(std::istreambuf_iterator<char>(file),
			std::istreambuf_iterator<char>());
	}

	Orkige::SkinnedRig extractRigFixture()
	{
		const std::vector<char> bytes = readRigFixture();
		REQUIRE_FALSE(bytes.empty());
		Orkige::SkinnedRig rig;
		REQUIRE(Orkige::extractSkinnedRigFromMemory(bytes.data(), bytes.size(),
			"glb", rig));
		return rig;
	}

	int jointIndex(Orkige::SkinnedRig const & rig, Orkige::String const & name)
	{
		for(size_t each = 0; each < rig.joints.size(); ++each)
		{
			if(rig.joints[each].name == name)
			{
				return static_cast<int>(each);
			}
		}
		return -1;
	}
}

TEST_CASE("SkinnedRigExtract: the mannequin's skeleton structure",
	"[skinnedrig]")
{
	const Orkige::SkinnedRig rig = extractRigFixture();
	REQUIRE(rig.hasSkeleton());

	// the seven generated joints are all present, parents before children
	// (the importer may add a synthetic scene-root joint above them)
	const char* names[7] =
		{"root", "spine", "head", "armL", "armR", "legL", "legR"};
	std::map<Orkige::String, int> indexOf;
	for(char const * name : names)
	{
		const int index = jointIndex(rig, name);
		REQUIRE(index >= 0);
		indexOf[name] = index;
	}
	// the generator's hierarchy: spine/legs under root, head/arms under spine
	CHECK(rig.joints[indexOf["spine"]].parent == indexOf["root"]);
	CHECK(rig.joints[indexOf["head"]].parent == indexOf["spine"]);
	CHECK(rig.joints[indexOf["armL"]].parent == indexOf["spine"]);
	CHECK(rig.joints[indexOf["armR"]].parent == indexOf["spine"]);
	CHECK(rig.joints[indexOf["legL"]].parent == indexOf["root"]);
	CHECK(rig.joints[indexOf["legR"]].parent == indexOf["root"]);
	for(size_t each = 0; each < rig.joints.size(); ++each)
	{
		CHECK(rig.joints[each].parent < static_cast<int>(each));
	}

	// local rest transforms are the generator's translations (spine sits
	// 0.55 above the pelvis root; the root carries the 1.90 hip height)
	CHECK(rig.joints[indexOf["root"]].position.y == Approx(1.90f));
	CHECK(rig.joints[indexOf["spine"]].position.y == Approx(0.55f));
	CHECK(rig.joints[indexOf["armL"]].position.x == Approx(0.50f));

	// composed model-space rest positions (translation-only chain)
	const Orkige::SkinnedRig::Vec3 head =
		rig.jointModelPosition(static_cast<size_t>(indexOf["head"]));
	CHECK(head.y == Approx(1.90f + 0.55f + 0.65f));
	const Orkige::SkinnedRig::Vec3 legR =
		rig.jointModelPosition(static_cast<size_t>(indexOf["legR"]));
	CHECK(legR.x == Approx(-0.22f));
	CHECK(legR.y == Approx(1.00f));
}

TEST_CASE("SkinnedRigExtract: the mannequin's skin weights", "[skinnedrig]")
{
	const Orkige::SkinnedRig rig = extractRigFixture();

	// exactly one skinned source mesh, every weight naming a real joint
	size_t skinnedMeshes = 0;
	for(Orkige::SkinnedRig::Skin const & skin : rig.skins)
	{
		if(skin.weights.empty())
		{
			continue;
		}
		++skinnedMeshes;
		// per-vertex weights sum to 1 (the generator's contract), with the
		// torso bottom blending two joints (a real multi-joint bind)
		std::map<unsigned int, float> sums;
		std::map<unsigned int, int> counts;
		for(Orkige::SkinnedRig::Weight const & weight : skin.weights)
		{
			REQUIRE(weight.joint >= 0);
			REQUIRE(static_cast<size_t>(weight.joint) < rig.joints.size());
			REQUIRE(weight.weight > 0.0f);
			sums[weight.vertexIndex] += weight.weight;
			counts[weight.vertexIndex] += 1;
		}
		REQUIRE_FALSE(sums.empty());
		bool sawBlended = false;
		for(auto const & each : sums)
		{
			CHECK(each.second == Approx(1.0f).margin(1e-4));
			sawBlended = sawBlended || counts[each.first] >= 2;
		}
		CHECK(sawBlended);
	}
	CHECK(skinnedMeshes == 1);
}

TEST_CASE("SkinnedRigExtract: the walk and idle clips", "[skinnedrig]")
{
	const Orkige::SkinnedRig rig = extractRigFixture();
	REQUIRE(rig.clips.size() == 2);

	// clip names and durations straight from the generator (walk 1 s with
	// five rotation channels + one head scale channel = six; idle 2 s with
	// four rotation channels)
	CHECK(rig.clips[0].name == "walk");
	CHECK(rig.clips[0].duration == Approx(1.0f).margin(1e-3));
	CHECK(rig.clips[0].channels.size() == 6);
	CHECK(rig.clips[1].name == "idle");
	CHECK(rig.clips[1].duration == Approx(2.0f).margin(1e-3));
	CHECK(rig.clips[1].channels.size() == 4);

	// every channel drives a real joint and carries keys of some kind (the
	// glTF importer normalises each channel to all three key arrays, filling a
	// track the source omits with a single bind-value key, so an authored
	// track is the one with more than one key)
	for(Orkige::SkinnedRig::Clip const & clip : rig.clips)
	{
		for(Orkige::SkinnedRig::Channel const & channel : clip.channels)
		{
			REQUIRE(channel.joint >= 0);
			REQUIRE(static_cast<size_t>(channel.joint) < rig.joints.size());
			REQUIRE_FALSE((channel.rotationKeys.empty() &&
				channel.scaleKeys.empty() && channel.positionKeys.empty()));
		}
	}

	// the head carries the walk clip's authored SCALE track (its rotation and
	// position tracks are the importer's single bind-value fill; the real,
	// multi-key track is the scale one). Its uniform scale pulses 1.0 -> 1.5x
	// over the cycle. The neutral extraction reads the raw scale track straight;
	// the classic Assimp merge road needs the scale-track fallback patch to land
	// the same values (this is the pin's headless half).
	const int head = jointIndex(rig, "head");
	REQUIRE(head >= 0);
	Orkige::SkinnedRig::Channel const * headScale = NULL;
	for(Orkige::SkinnedRig::Channel const & channel : rig.clips[0].channels)
	{
		if(channel.joint == head && channel.scaleKeys.size() > 1)
		{
			headScale = &channel;
		}
	}
	REQUIRE(headScale);
	CHECK(headScale->scaleKeys.back().time == Approx(1.0f).margin(1e-3));
	const Orkige::SkinnedRig::Vec3 unit{1.0f, 1.0f, 1.0f};
	const Orkige::SkinnedRig::Vec3 restScale =
		Orkige::SkinnedRig::sampleVecKeys(headScale->scaleKeys, 0.0f, unit);
	const Orkige::SkinnedRig::Vec3 peakScale =
		Orkige::SkinnedRig::sampleVecKeys(headScale->scaleKeys, 0.5f, unit);
	CHECK(restScale.x == Approx(1.0f).margin(1e-3));
	CHECK(peakScale.x == Approx(1.5f).margin(1e-3));

	// sampling: the walk legs swing +/-25 degrees about X - at half time the
	// left leg's sampled rotation crosses the opposite extreme, and the
	// sampler interpolates between keys (a quarter in, the angle is between)
	const int legL = jointIndex(rig, "legL");
	REQUIRE(legL >= 0);
	Orkige::SkinnedRig::Channel const * legChannel = NULL;
	for(Orkige::SkinnedRig::Channel const & channel : rig.clips[0].channels)
	{
		if(channel.joint == legL)
		{
			legChannel = &channel;
		}
	}
	REQUIRE(legChannel);
	const Orkige::SkinnedRig::Quat atStart =
		Orkige::SkinnedRig::sampleQuatKeys(legChannel->rotationKeys, 0.0f);
	const Orkige::SkinnedRig::Quat atHalf =
		Orkige::SkinnedRig::sampleQuatKeys(legChannel->rotationKeys, 0.5f);
	// quat_x(+25deg).x = sin(12.5deg), quat_x(-25deg).x = -sin(12.5deg)
	CHECK(atStart.x == Approx(0.21644f).margin(1e-3));
	CHECK(atHalf.x == Approx(-0.21644f).margin(1e-3));
	const Orkige::SkinnedRig::Quat atQuarter =
		Orkige::SkinnedRig::sampleQuatKeys(legChannel->rotationKeys, 0.25f);
	CHECK(atQuarter.x < atStart.x);
	CHECK(atQuarter.x > atHalf.x);
}
