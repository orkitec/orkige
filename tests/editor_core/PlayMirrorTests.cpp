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
			//! how spawnObject created it (empty for a hand-authored object)
			std::string spawnedParent;
			std::vector<std::string> spawnedComponents;
			std::size_t spawnedPropertyCount = 0;
		};
		std::map<std::string, Object> objects;
		std::vector<std::string> spawnOrder;	//!< spawnObject call order

		std::vector<std::string> transformableIds() const override
		{
			std::vector<std::string> ids;
			for (auto const& entry : this->objects)
			{
				ids.push_back(entry.first);
			}
			return ids;
		}
		bool hasObject(std::string const& id) const override
		{
			return this->objects.find(id) != this->objects.end();
		}
		bool spawnObject(MirrorSpawnDesc const& desc) override
		{
			if (this->hasObject(desc.id))
			{
				return false;
			}
			Object object;
			object.spawnedParent = desc.parent;
			object.spawnedComponents = desc.components;
			object.spawnedPropertyCount = desc.properties.size();
			this->objects[desc.id] = object;
			this->spawnOrder.push_back(desc.id);
			return true;
		}
		void destroyObject(std::string const& id) override
		{
			this->objects.erase(id);
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

TEST_CASE("parseSpawnDescriptors round-trips the wire lists", "[playmirror]")
{
	// two objects; the flat per-property records reference them by index
	const std::vector<MirrorSpawnDesc> parsed = parseSpawnDescriptors(
		{ "RuntimeProbe", "RuntimeProbe/Child" },
		{ "", "RuntimeProbe" },
		{ "TransformComponent ModelComponent", "SpriteComponent" },
		{ "0", "0", "1", "junk", "7" },	// junk / out-of-range records skipped
		{ "TransformComponent.position", "ModelComponent.mesh",
		  "SpriteComponent.texture", "SpriteComponent.width",
		  "SpriteComponent.height" },
		{ "5", "8", "8", "1", "1" },
		{ "0 1 0", "EditorCube.mesh", "ball.png", "2", "2" },
		{ "", "asset-id-1", "", "", "" });
	REQUIRE(parsed.size() == 2);
	CHECK(parsed[0].id == "RuntimeProbe");
	CHECK(parsed[0].parent == "");
	REQUIRE(parsed[0].components.size() == 2);
	CHECK(parsed[0].components[0] == "TransformComponent");
	CHECK(parsed[0].components[1] == "ModelComponent");
	REQUIRE(parsed[0].properties.size() == 2);
	CHECK(parsed[0].properties[0].component == "TransformComponent");
	CHECK(parsed[0].properties[0].name == "position");
	CHECK(parsed[0].properties[0].kind == 5);
	CHECK(parsed[0].properties[0].value == "0 1 0");
	CHECK(parsed[0].properties[1].reference == "asset-id-1");
	CHECK(parsed[1].parent == "RuntimeProbe");
	REQUIRE(parsed[1].properties.size() == 1);	// junk + out-of-range skipped
	CHECK(parsed[1].properties[0].name == "texture");
}

TEST_CASE("isMirrorVisualComponent allows looks, refuses behavior",
	"[playmirror]")
{
	CHECK(isMirrorVisualComponent("TransformComponent"));
	CHECK(isMirrorVisualComponent("ModelComponent"));
	CHECK(isMirrorVisualComponent("SpriteComponent"));
	CHECK(isMirrorVisualComponent("VectorShapeComponent"));
	CHECK(isMirrorVisualComponent("LightComponent"));
	CHECK_FALSE(isMirrorVisualComponent("ScriptComponent"));
	CHECK_FALSE(isMirrorVisualComponent("RigidBodyComponent"));
	CHECK_FALSE(isMirrorVisualComponent("SoundComponent"));
	CHECK_FALSE(isMirrorVisualComponent("player"));	// a script kind
}

TEST_CASE("PlayMirror asks once per unmatched id and tracks stand-ins",
	"[playmirror]")
{
	FakeScene scene;
	scene.objects["Authored"].transform = makeTransform(0.0f, 0.0f, 0.0f);
	PlayMirror mirror;

	// the hierarchy streams an authored id and a runtime-spawned one
	std::vector<std::string> ask =
		mirror.idsToQuery(scene, { "Authored", "Spawned" });
	REQUIRE(ask.size() == 1);
	CHECK(ask[0] == "Spawned");
	// asked exactly once while unresolved
	CHECK(mirror.idsToQuery(scene, { "Authored", "Spawned" }).empty());

	// the descriptor reply materializes the stand-in
	MirrorSpawnDesc desc;
	desc.id = "Spawned";
	desc.components = { "TransformComponent" };
	mirror.applySpawns(scene, { desc });
	CHECK(scene.hasObject("Spawned"));
	CHECK(mirror.instanceCount() == 1);
	CHECK(mirror.isMirrorInstance("Spawned"));

	// transforms now drive the stand-in like any matched object
	mirror.applyTransforms(scene, { "Spawned" },
		{ formatMirrorTransform(makeTransform(3.0f, 4.0f, 5.0f)) });
	CHECK(scene.objects["Spawned"].transform.m[0] == 3.0f);

	SECTION("a vanished id prunes the stand-in and may be re-asked")
	{
		mirror.pruneInstances(scene, { "Authored" });
		CHECK_FALSE(scene.hasObject("Spawned"));
		CHECK(mirror.instanceCount() == 0);
		// reappearing means asking again (a fresh spawn under the same id)
		std::vector<std::string> reAsk =
			mirror.idsToQuery(scene, { "Authored", "Spawned" });
		REQUIRE(reAsk.size() == 1);
		CHECK(reAsk[0] == "Spawned");
	}

	SECTION("restore destroys stand-ins and exact-restores authored state")
	{
		// move the authored object too, then restore everything
		mirror.applyTransforms(scene, { "Authored" },
			{ formatMirrorTransform(makeTransform(9.0f, 9.0f, 9.0f)) });
		mirror.restore(scene);
		CHECK_FALSE(mirror.active());
		CHECK(mirror.instanceCount() == 0);
		CHECK_FALSE(scene.hasObject("Spawned"));	// destroyed, not restored
		CHECK(scene.objects["Authored"].transform.m[0] == 0.0f);	// exact
	}
}

TEST_CASE("PlayMirror spawns parents before children across a batch",
	"[playmirror]")
{
	FakeScene scene;
	PlayMirror mirror;
	// child listed FIRST - the ordering pass must defer it until the parent
	MirrorSpawnDesc child;
	child.id = "Root/Child";
	child.parent = "Root";
	MirrorSpawnDesc root;
	root.id = "Root";
	mirror.applySpawns(scene, { child, root });
	REQUIRE(scene.spawnOrder.size() == 2);
	CHECK(scene.spawnOrder[0] == "Root");
	CHECK(scene.spawnOrder[1] == "Root/Child");
	CHECK(scene.objects["Root/Child"].spawnedParent == "Root");

	// an unresolvable parent never blocks the stand-in itself (lands as root)
	MirrorSpawnDesc orphan;
	orphan.id = "Orphan";
	orphan.parent = "NeverArrives";
	mirror.applySpawns(scene, { orphan });
	CHECK(scene.hasObject("Orphan"));
}
