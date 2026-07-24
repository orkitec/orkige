/**************************************************************
	created:	2026/07/24 at 10:00
	filename: 	PlayMirrorTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
//
// Headless unit tests for the play-mode motion mirror's PURE logic (parse/
// format, effective-active composition, snapshot/apply/restore) - exercised
// against a fake MirrorScene so no engine/render boot is needed. The concrete
// GameObjectManager-backed scene lives in the editor shell and is proven live
// by the editor_play_mirror integration ctest.

#include <catch2/catch_test_macros.hpp>

#include "PlayMirror.h"

#include <map>
#include <string>
#include <vector>

using namespace OrkigeEditor;

namespace
{
	//! a map-backed MirrorScene: transforms + visibility are plain records, so
	//! a test can assert exactly what the mirror wrote and that a restore
	//! returns every value byte-for-byte to its authored state.
	class FakeScene : public MirrorScene
	{
	public:
		struct Object
		{
			MirrorTransform transform;
			bool baselineVisible = true;	//!< authored activeInHierarchy
			bool visible = true;			//!< current (mirror-driven) visibility
		};
		std::map<std::string, Object> objects;

		std::vector<std::string> transformableIds() const override
		{
			std::vector<std::string> ids;
			for (auto const& entry : this->objects)
			{
				ids.push_back(entry.first);
			}
			return ids;
		}
		bool getLocalTransform(std::string const& id,
			MirrorTransform& out) const override
		{
			auto found = this->objects.find(id);
			if (found == this->objects.end())
			{
				return false;
			}
			out = found->second.transform;
			return true;
		}
		void setLocalTransform(std::string const& id,
			MirrorTransform const& transform) override
		{
			auto found = this->objects.find(id);
			if (found != this->objects.end())
			{
				found->second.transform = transform;
			}
		}
		bool getBaselineVisible(std::string const& id) const override
		{
			auto found = this->objects.find(id);
			return found == this->objects.end() ? true
				: found->second.baselineVisible;
		}
		void setVisible(std::string const& id, bool visible) override
		{
			auto found = this->objects.find(id);
			if (found != this->objects.end())
			{
				found->second.visible = visible;
			}
		}
	};

	MirrorTransform makeTransform(float px, float py, float pz)
	{
		MirrorTransform t;
		t.m = { px, py, pz, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };
		return t;
	}
}

TEST_CASE("parseMirrorTransform round-trips formatMirrorTransform", "[playmirror]")
{
	MirrorTransform original;
	original.m = { 1.5f, -2.25f, 3.0e-5f, 0.7071f, 0.0f, 0.7071f, 0.0f,
		2.0f, 2.0f, 2.0f };
	const std::string wire = formatMirrorTransform(original);
	MirrorTransform parsed;
	REQUIRE(parseMirrorTransform(wire, parsed));
	for (std::size_t i = 0; i < original.m.size(); ++i)
	{
		CHECK(parsed.m[i] == original.m[i]);	// %.9g is exact for these
	}
}

TEST_CASE("parseMirrorTransform rejects short / junk input", "[playmirror]")
{
	MirrorTransform out;
	CHECK_FALSE(parseMirrorTransform("", out));
	CHECK_FALSE(parseMirrorTransform("1 2 3", out));			// only 3 floats
	CHECK_FALSE(parseMirrorTransform("1 2 3 4 5 6 7 8 9 x", out)); // junk 10th
	CHECK(parseMirrorTransform("0 0 0 1 0 0 0 1 1 1", out));		// exactly 10
}

TEST_CASE("computeEffectiveActive composes activeSelf down the parent chain",
	"[playmirror]")
{
	// A(active) -> B(inactive) -> C(active): C is effective-inactive through B
	const std::vector<std::string> ids = { "A", "A/B", "A/B/C", "D" };
	const std::vector<std::string> parents = { "", "A", "A/B", "" };
	const std::vector<std::string> actives = { "1", "0", "1", "1" };
	const std::map<std::string, bool> effective =
		computeEffectiveActive(ids, parents, actives);
	CHECK(effective.at("A") == true);
	CHECK(effective.at("A/B") == false);
	CHECK(effective.at("A/B/C") == false);	// inactive ancestor B hides it
	CHECK(effective.at("D") == true);
}

TEST_CASE("computeEffectiveActive degrades to all-active without the lists",
	"[playmirror]")
{
	const std::vector<std::string> ids = { "A", "B" };
	// an older stream carries neither parents nor actives
	const std::map<std::string, bool> effective =
		computeEffectiveActive(ids, {}, {});
	CHECK(effective.at("A") == true);
	CHECK(effective.at("B") == true);
}

TEST_CASE("PlayMirror snapshots, applies motion and restores exactly",
	"[playmirror]")
{
	FakeScene scene;
	scene.objects["Cube"].transform = makeTransform(0.0f, 5.0f, 0.0f);
	scene.objects["Ground"].transform = makeTransform(0.0f, 0.0f, 0.0f);
	const MirrorTransform authoredCube = scene.objects["Cube"].transform;
	const MirrorTransform authoredGround = scene.objects["Ground"].transform;

	PlayMirror mirror;
	CHECK_FALSE(mirror.active());

	// a stream update moves Cube down (as the running game would); Ground still
	SECTION("apply moves matched objects and snapshots the authored pose")
	{
		mirror.applyTransforms(scene, { "Cube" },
			{ formatMirrorTransform(makeTransform(0.0f, 2.0f, 0.0f)) });
		CHECK(mirror.active());
		CHECK(mirror.trackedCount() == 2);	// snapshotted BOTH authored objects
		CHECK(scene.objects["Cube"].transform.m[1] == 2.0f);	// mirrored down
		CHECK(scene.objects["Ground"].transform.m[1] == 0.0f);	// untouched

		mirror.restore(scene);
		CHECK_FALSE(mirror.active());
		// exact restore: every float back to the authored snapshot
		for (std::size_t i = 0; i < 10; ++i)
		{
			CHECK(scene.objects["Cube"].transform.m[i] == authoredCube.m[i]);
			CHECK(scene.objects["Ground"].transform.m[i] == authoredGround.m[i]);
		}
	}

	SECTION("a runtime-spawned id (no authored counterpart) is skipped")
	{
		mirror.applyTransforms(scene, { "Cube", "Spawned" },
			{ formatMirrorTransform(makeTransform(0.0f, 3.0f, 0.0f)),
			  formatMirrorTransform(makeTransform(9.0f, 9.0f, 9.0f)) });
		CHECK(scene.objects.find("Spawned") == scene.objects.end());
		CHECK(scene.objects["Cube"].transform.m[1] == 3.0f);
	}
}

TEST_CASE("PlayMirror mirrors visibility and restores the baseline",
	"[playmirror]")
{
	FakeScene scene;
	scene.objects["Cube"].transform = makeTransform(0.0f, 0.0f, 0.0f);
	scene.objects["Cube"].baselineVisible = true;
	scene.objects["Hidden"].transform = makeTransform(1.0f, 0.0f, 0.0f);
	scene.objects["Hidden"].baselineVisible = false;	// authored inactive

	PlayMirror mirror;
	// the running game hid Cube and showed Hidden - the mirror flips both
	std::map<std::string, bool> effective;
	effective["Cube"] = false;
	effective["Hidden"] = true;
	mirror.applyActive(scene, effective);
	CHECK(scene.objects["Cube"].visible == false);
	CHECK(scene.objects["Hidden"].visible == true);

	mirror.restore(scene);
	// back to the authored baselines
	CHECK(scene.objects["Cube"].visible == true);
	CHECK(scene.objects["Hidden"].visible == false);
}
