/**************************************************************
	created:	2026/07/30 at 09:50
	filename: 	MeshAssetTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tests for the `.omesh` text asset and the 2D-to-3D operators: the
	grammar (shapes, placement modifiers, material sections, comments), the
	line-numbered refusal for every malformation, the injected shape-source seam
	that keeps the parser filesystem-free, and the extrude/revolve geometry over
	parsed `.oshape` regions (caps + walls, holes as real tunnels, the profile
	sweep). Pure - no renderer, no filesystem.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core_util/MeshAsset.h"
#include "core_util/MeshExtrude.h"
#include "core_util/ShapeCollider.h"
#include "core_util/VectorShapeAsset.h"

#include <cmath>
#include <map>
#include <vector>

using Orkige::MeshAsset;
using Orkige::MeshBuilder;
using Orkige::MeshExtrude;
using Orkige::ShapeCollider;
using Orkige::String;
using Orkige::VectorTessellator;
using Mesh = MeshBuilder::Mesh;
using Vec3f = MeshBuilder::Vec3f;
using Point = VectorTessellator::Point;

namespace
{
	//! a shape source over an in-memory name -> `.oshape` text table: the
	//! parser reads no file, so a unit test just hands it the text
	struct TextShapes : public MeshAsset::ShapeSource
	{
		std::map<String, String> files;

		bool loadShape(String const& name,
			std::vector<VectorTessellator::Region>& outRegions) const override
		{
			std::map<String, String>::const_iterator found = this->files.find(name);
			if (found == this->files.end())
			{
				return false;
			}
			return Orkige::VectorShapeAsset::parse(found->second, outRegions);
		}
	};

	//! a plain square `.oshape` (2x2, centred), the extrude fixture
	String squareShape()
	{
		return
			"version 1\n"
			"fill 1 1 1 1\n"
			"contour 4\n"
			"v -1 -1\n"
			"v 1 -1\n"
			"v 1 1\n"
			"v -1 1\n";
	}

	//! a square with a square hole - the tunnel fixture
	String ringShape()
	{
		return
			"version 1\n"
			"fill 1 1 1 1\n"
			"contour 4\n"
			"v -2 -2\n"
			"v 2 -2\n"
			"v 2 2\n"
			"v -2 2\n"
			"hole 4\n"
			"v -1 -1\n"
			"v -1 1\n"
			"v 1 1\n"
			"v 1 -1\n";
	}

	//! a straight vertical profile at radius 1 from y=2 down to y=0 - revolving
	//! it must give a cylinder wall
	String wallProfileShape()
	{
		return
			"version 1\n"
			"fill 1 1 1 1\n"
			"contour 4\n"
			"v 1 0\n"
			"v 1 2\n"
			"v 0.5 2\n"
			"v 0.5 0\n";
	}

	bool normalsAreUnit(Mesh const& mesh)
	{
		for (std::size_t each = 0; each < mesh.vertices.size(); ++each)
		{
			if (std::fabs(MeshBuilder::length(mesh.vertices[each].normal) -
				1.0f) > 1.0e-3f)
			{
				return false;
			}
		}
		return true;
	}

	//! does the reported error name this line
	bool errorNamesLine(String const& error, int line)
	{
		return error.find("line " + std::to_string(line) + ":") == 0;
	}
}

//--- the grammar ----------------------------------------------------------

TEST_CASE("a .omesh places several shapes into material sections",
	"[meshasset]")
{
	const String text =
		"# a small blockout\n"
		"version 1\n"
		"material stone\n"
		"box 2 1 2  at 0 0.5 0\n"
		"cylinder radius 0.4 height 3 segments 12  at 2 0 0  material metal\n"
		"box 1 1 1  at -2 0 0\n";
	Mesh mesh;
	String error;
	REQUIRE(MeshAsset::parse(text, mesh, NULL, &error));
	CHECK(error.empty());
	CHECK(MeshBuilder::validate(mesh, &error));
	CHECK(normalsAreUnit(mesh));
	// stone, metal, stone: the two stone runs are NOT adjacent, so three
	// sections in first-appearance order
	REQUIRE(mesh.sections.size() == 3);
	CHECK(mesh.sections[0].material == "stone");
	CHECK(mesh.sections[1].material == "metal");
	CHECK(mesh.sections[2].material == "stone");
	// the boxes landed where `at` put them
	const MeshBuilder::Bounds bounds = mesh.computeBounds();
	CHECK(bounds.minimum.x == Catch::Approx(-2.5f));
	CHECK(bounds.maximum.x == Catch::Approx(2.4f).margin(1.0e-3f));
	CHECK(bounds.maximum.y == Catch::Approx(1.5f));
}

TEST_CASE("adjacent shapes sharing a material merge into one draw section",
	"[meshasset]")
{
	const String text =
		"material stone\n"
		"box 1 1 1 at 0 0 0\n"
		"box 1 1 1 at 2 0 0\n"
		"box 1 1 1 at 4 0 0\n";
	Mesh mesh;
	String error;
	REQUIRE(MeshAsset::parse(text, mesh, NULL, &error));
	REQUIRE(mesh.sections.size() == 1);
	CHECK(mesh.sections[0].material == "stone");
	CHECK(mesh.sections[0].vertexCount == 72);
	CHECK(mesh.triangleCount() == 36);
}

TEST_CASE("every placement modifier applies", "[meshasset]")
{
	String error;
	SECTION("rotate turns the shape")
	{
		Mesh mesh;
		REQUIRE(MeshAsset::parse("box 4 1 1 rotate 0 90 0", mesh, NULL,
			&error));
		const MeshBuilder::Bounds bounds = mesh.computeBounds();
		// the long axis became Z
		CHECK(bounds.size().x == Catch::Approx(1.0f).margin(1.0e-4f));
		CHECK(bounds.size().z == Catch::Approx(4.0f).margin(1.0e-4f));
	}
	SECTION("scale takes one or three values")
	{
		Mesh uniform;
		REQUIRE(MeshAsset::parse("box 1 1 1 scale 3", uniform, NULL, &error));
		CHECK(uniform.computeBounds().size().y == Catch::Approx(3.0f));
		Mesh axes;
		REQUIRE(MeshAsset::parse("box 1 1 1 scale 2 4 6", axes, NULL, &error));
		CHECK(axes.computeBounds().size().x == Catch::Approx(2.0f));
		CHECK(axes.computeBounds().size().y == Catch::Approx(4.0f));
		CHECK(axes.computeBounds().size().z == Catch::Approx(6.0f));
	}
	SECTION("uv re-projects and takes an optional tiling scale")
	{
		Mesh tiled;
		REQUIRE(MeshAsset::parse("plane 10 10 segments 2 2 uv xz 5 5", tiled,
			NULL, &error));
		bool sawLargeUv = false;
		for (std::size_t each = 0; each < tiled.vertices.size(); ++each)
		{
			sawLargeUv = sawLargeUv || tiled.vertices[each].uv.x > 4.0f;
		}
		CHECK(sawLargeUv);
		CHECK_FALSE(MeshAsset::parse("plane 1 1 uv sideways", tiled, NULL,
			&error));
		CHECK(error.find("not a uv mode") != String::npos);
	}
	SECTION("smooth and flat rewrite the normals")
	{
		Mesh faceted;
		REQUIRE(MeshAsset::parse("sphere radius 1 segments 8 rings 4 flat",
			faceted, NULL, &error));
		CHECK(faceted.vertices.size() == faceted.triangleCount() * 3);
		CHECK(normalsAreUnit(faceted));
		Mesh soft;
		REQUIRE(MeshAsset::parse("box 1 1 1 smooth 180", soft, NULL, &error));
		CHECK(normalsAreUnit(soft));
		CHECK(std::fabs(soft.vertices[0].normal.x) < 0.9f);
		CHECK_FALSE(MeshAsset::parse("box 1 1 1 smooth flat", soft, NULL,
			&error));
	}
	SECTION("modifiers may appear in any order and only once")
	{
		Mesh mesh;
		REQUIRE(MeshAsset::parse(
			"box 1 1 1 material stone at 1 2 3 scale 2 rotate 0 45 0", mesh,
			NULL, &error));
		CHECK(mesh.sections[0].material == "stone");
		CHECK_FALSE(MeshAsset::parse("box 1 1 1 at 0 0 0 at 1 1 1", mesh, NULL,
			&error));
		CHECK(error.find("twice") != String::npos);
	}
}

TEST_CASE("every malformation is refused on its own line", "[meshasset]")
{
	Mesh mesh;
	String error;

	CHECK_FALSE(MeshAsset::parse("box 1 1 1\nsphre radius 1\n", mesh, NULL,
		&error));
	CHECK(errorNamesLine(error, 2));
	CHECK(error.find("unknown directive") != String::npos);
	CHECK(mesh.empty());

	CHECK_FALSE(MeshAsset::parse("box 1 1\n", mesh, NULL, &error));
	CHECK(errorNamesLine(error, 1));

	CHECK_FALSE(MeshAsset::parse("box 1 wide 1\n", mesh, NULL, &error));
	CHECK(errorNamesLine(error, 1));
	CHECK(error.find("not a number") != String::npos);

	CHECK_FALSE(MeshAsset::parse("box 1 1 1 wobble 3\n", mesh, NULL, &error));
	CHECK(error.find("unknown key 'wobble'") != String::npos);

	CHECK_FALSE(MeshAsset::parse("cylinder radius 1\n", mesh, NULL, &error));
	CHECK(errorNamesLine(error, 1));

	CHECK_FALSE(MeshAsset::parse("roundedbox 1 1 1 segments 3\n", mesh, NULL,
		&error));
	CHECK(error.find("radius") != String::npos);

	CHECK_FALSE(MeshAsset::parse("version 2\nbox 1 1 1\n", mesh, NULL,
		&error));
	CHECK(errorNamesLine(error, 1));

	CHECK_FALSE(MeshAsset::parse("box 1 1 1\nversion 1\n", mesh, NULL,
		&error));
	CHECK(errorNamesLine(error, 2));
	CHECK(error.find("first directive") != String::npos);

	CHECK_FALSE(MeshAsset::parse("version 1 extra\n", mesh, NULL, &error));

	// an empty or comment-only file carries no shape - honest, not a crash
	CHECK_FALSE(MeshAsset::parse("", mesh, NULL, &error));
	CHECK_FALSE(error.empty());
	CHECK_FALSE(MeshAsset::parse("# nothing here\n\n   \n", mesh, NULL,
		&error));

	// a refused SHAPE reports the shape's own words on its line
	CHECK_FALSE(MeshAsset::parse("box 1 1 1\ncylinder radius 0 height 1\n",
		mesh, NULL, &error));
	CHECK(errorNamesLine(error, 2));
	CHECK(error.find("positive") != String::npos);
}

TEST_CASE("comments, blank lines and case are handled", "[meshasset]")
{
	const String text =
		"\n"
		"# a header comment\n"
		"VERSION 1\n"
		"\n"
		"   BOX 2 2 2   # trailing comment\n"
		"\tSphere Radius 1 Segments 6 At 0 3 0\n";
	Mesh mesh;
	String error;
	REQUIRE(MeshAsset::parse(text, mesh, NULL, &error));
	CHECK(mesh.triangleCount() > 12);
	CHECK(MeshBuilder::validate(mesh));
}

TEST_CASE("the .omesh name probe and the reference scan", "[meshasset]")
{
	CHECK(MeshAsset::isMeshAssetName("tower.omesh"));
	CHECK(MeshAsset::isMeshAssetName("Tower.OMESH"));
	CHECK_FALSE(MeshAsset::isMeshAssetName("tower.glb"));
	CHECK_FALSE(MeshAsset::isMeshAssetName(".omesh2"));
	CHECK_FALSE(MeshAsset::isMeshAssetName("omesh"));

	const Orkige::StringVector references = MeshAsset::shapeReferences(
		"extrude shape vase.oshape depth 1\n"
		"revolve shape vase.oshape segments 8\n"
		"revolve shape cup.oshape\n"
		"box 1 1 1\n");
	REQUIRE(references.size() == 2);
	CHECK(references[0] == "vase.oshape");
	CHECK(references[1] == "cup.oshape");
}

//--- the shape-source seam ------------------------------------------------

TEST_CASE("extrude and revolve resolve through the injected shape source",
	"[meshasset]")
{
	TextShapes shapes;
	shapes.files["block.oshape"] = squareShape();
	shapes.files["wall.oshape"] = wallProfileShape();

	Mesh mesh;
	String error;
	REQUIRE(MeshAsset::parse(
		"material stone\n"
		"extrude shape block.oshape depth 0.5 at 0 1 0\n"
		"revolve shape wall.oshape segments 16 at 5 0 0\n",
		mesh, &shapes, &error));
	CHECK(error.empty());
	CHECK(MeshBuilder::validate(mesh, &error));
	CHECK(normalsAreUnit(mesh));
	REQUIRE(mesh.sections.size() == 1);

	// without a source the reference is unresolvable - reported, never a crash
	Mesh none;
	CHECK_FALSE(MeshAsset::parse("extrude shape block.oshape depth 1", none,
		NULL, &error));
	CHECK(error.find("could not be loaded") != String::npos);
	CHECK(none.empty());

	// a missing `shape` key is its own honest refusal
	CHECK_FALSE(MeshAsset::parse("extrude depth 1", none, &shapes, &error));
	CHECK(error.find("shape") != String::npos);
	CHECK_FALSE(MeshAsset::parse("extrude shape block.oshape", none, &shapes,
		&error));
	CHECK(error.find("depth") != String::npos);
	CHECK_FALSE(MeshAsset::parse("extrude shape absent.oshape depth 1", none,
		&shapes, &error));
	CHECK(error.find("absent.oshape") != String::npos);
}

//--- the operators over real .oshape regions ------------------------------

TEST_CASE("extrudeShape closes a square outline into a slab", "[meshextrude]")
{
	std::vector<VectorTessellator::Region> regions;
	REQUIRE(Orkige::VectorShapeAsset::parse(squareShape(), regions));

	Mesh mesh;
	String error;
	REQUIRE(MeshExtrude::extrudeShape(mesh, regions, 0.5f, false, &error));
	CHECK(MeshBuilder::validate(mesh, &error));
	CHECK(normalsAreUnit(mesh));
	// two caps of 2 triangles each plus 4 wall quads
	CHECK(mesh.triangleCount() == 2u * 2u + 4u * 2u);
	const MeshBuilder::Bounds bounds = mesh.computeBounds();
	CHECK(bounds.size().x == Catch::Approx(2.0f));
	CHECK(bounds.size().y == Catch::Approx(2.0f));
	CHECK(bounds.size().z == Catch::Approx(0.5f));
	CHECK(bounds.centre().z == Catch::Approx(0.0f));

	// the caps look along +/-Z and the walls point away from the centre
	int frontCap = 0;
	int backCap = 0;
	for (std::size_t each = 0; each < mesh.vertices.size(); ++each)
	{
		Vec3f const& normal = mesh.vertices[each].normal;
		if (normal.z > 0.9f) { ++frontCap; }
		if (normal.z < -0.9f) { ++backCap; }
	}
	CHECK(frontCap == 4);
	CHECK(backCap == 4);
	const std::size_t triangles = mesh.triangleCount();
	for (std::size_t each = 0; each < triangles; ++each)
	{
		Vec3f const& a = mesh.vertices[mesh.indices[each * 3 + 0]].position;
		Vec3f const& b = mesh.vertices[mesh.indices[each * 3 + 1]].position;
		Vec3f const& c = mesh.vertices[mesh.indices[each * 3 + 2]].position;
		const Vec3f face = MeshBuilder::cross(MeshBuilder::subtract(b, a),
			MeshBuilder::subtract(c, a));
		const Vec3f centroid((a.x + b.x + c.x) / 3.0f,
			(a.y + b.y + c.y) / 3.0f, (a.z + b.z + c.z) / 3.0f);
		CHECK(MeshBuilder::dot(face, centroid) > 0.0f);
	}

	CHECK_FALSE(MeshExtrude::extrudeShape(mesh, regions, 0.0f, false, &error));
	CHECK(mesh.empty());
	std::vector<VectorTessellator::Region> nothing;
	CHECK_FALSE(MeshExtrude::extrudeShape(mesh, nothing, 1.0f, false, &error));
	CHECK(error.find("no solid fill region") != String::npos);
}

TEST_CASE("extrudeShape sweeps a hole into a real tunnel", "[meshextrude]")
{
	std::vector<VectorTessellator::Region> regions;
	REQUIRE(Orkige::VectorShapeAsset::parse(ringShape(), regions));
	REQUIRE(regions.size() == 1);
	REQUIRE(regions[0].holes.size() == 1);

	Mesh mesh;
	String error;
	REQUIRE(MeshExtrude::extrudeShape(mesh, regions, 1.0f, false, &error));
	CHECK(MeshBuilder::validate(mesh, &error));
	// the hole's own 4 wall quads are there on top of the outer 4
	int inwardWalls = 0;
	for (std::size_t each = 0; each < mesh.vertices.size(); ++each)
	{
		Vec3f const& position = mesh.vertices[each].position;
		Vec3f const& normal = mesh.vertices[each].normal;
		if (std::fabs(position.x) < 1.5f && std::fabs(position.y) < 1.5f &&
			std::fabs(normal.z) < 0.1f)
		{
			// a wall vertex on the inner loop: its normal points back toward
			// the cut-out centre, i.e. INTO the hole
			if (MeshBuilder::dot(normal, Vec3f(position.x, position.y, 0.0f))
				< 0.0f)
			{
				++inwardWalls;
			}
		}
	}
	// the inner loop's 4 wall quads carry 4 vertices each, every one of them
	// looking back into the cut-out
	CHECK(inwardWalls == 16);
	// the caps honour the hole, so the slab has less area than a solid one
	std::vector<VectorTessellator::Region> solid = regions;
	solid[0].holes.clear();
	Mesh full;
	REQUIRE(MeshExtrude::extrudeShape(full, solid, 1.0f, false, &error));
	CHECK(full.triangleCount() < mesh.triangleCount());
}

TEST_CASE("extrudeShape can shade its walls smooth", "[meshextrude]")
{
	// a coarse octagon: the flat sweep gives each wall its own normal, the
	// smooth sweep averages neighbours so the silhouette reads as a curve
	String octagon = "version 1\nfill 1 1 1 1\ncontour 8\n";
	for (int each = 0; each < 8; ++each)
	{
		const float angle = 6.28318531f * static_cast<float>(each) / 8.0f;
		octagon += "v " + std::to_string(std::cos(angle)) + " " +
			std::to_string(std::sin(angle)) + "\n";
	}
	std::vector<VectorTessellator::Region> regions;
	REQUIRE(Orkige::VectorShapeAsset::parse(octagon, regions));

	String error;
	Mesh flat;
	REQUIRE(MeshExtrude::extrudeShape(flat, regions, 1.0f, false, &error));
	Mesh smooth;
	REQUIRE(MeshExtrude::extrudeShape(smooth, regions, 1.0f, true, &error));
	CHECK(flat.vertices.size() == smooth.vertices.size());
	CHECK(normalsAreUnit(flat));
	CHECK(normalsAreUnit(smooth));
	// the two share topology but not normals
	bool differs = false;
	for (std::size_t each = 0; each < flat.vertices.size(); ++each)
	{
		differs = differs || MeshBuilder::dot(flat.vertices[each].normal,
			smooth.vertices[each].normal) < 0.999f;
	}
	CHECK(differs);
}

TEST_CASE("revolveShape sweeps a profile into a surface of revolution",
	"[meshextrude]")
{
	std::vector<VectorTessellator::Region> regions;
	REQUIRE(Orkige::VectorShapeAsset::parse(wallProfileShape(), regions));

	Mesh mesh;
	String error;
	REQUIRE(MeshExtrude::revolveShape(mesh, regions, 16, 360.0f, &error));
	CHECK(MeshBuilder::validate(mesh, &error));
	CHECK(normalsAreUnit(mesh));
	const MeshBuilder::Bounds bounds = mesh.computeBounds();
	// the profile spans radius 0.5..1 and height 0..2 - the sweep keeps it
	CHECK(bounds.size().x == Catch::Approx(2.0f).margin(1.0e-3f));
	CHECK(bounds.minimum.y == Catch::Approx(0.0f).margin(1.0e-4f));
	CHECK(bounds.maximum.y == Catch::Approx(2.0f).margin(1.0e-4f));
	// placement is the profile's own: it stands on the ground plane, not centred
	CHECK(bounds.centre().y == Catch::Approx(1.0f).margin(1.0e-3f));

	// a partial sweep is legitimate
	Mesh partial;
	REQUIRE(MeshExtrude::revolveShape(partial, regions, 16, 120.0f, &error));
	CHECK(partial.triangleCount() == mesh.triangleCount());
	CHECK(partial.computeBounds().size().x < bounds.size().x);

	// a profile crossing the axis would sweep through itself
	std::vector<VectorTessellator::Region> crossing = regions;
	crossing[0].outer[0].x = -1.0f;
	CHECK_FALSE(MeshExtrude::revolveShape(mesh, crossing, 16, 360.0f, &error));
	CHECK(error.find("half-plane") != String::npos);
	CHECK(mesh.empty());

	std::vector<VectorTessellator::Region> nothing;
	CHECK_FALSE(MeshExtrude::revolveShape(mesh, nothing, 16, 360.0f, &error));
}

TEST_CASE("the operators consume the collider's own contour vocabulary",
	"[meshextrude]")
{
	// a stroke region encloses no area: extractContours skips it and so must
	// the extruder - one eligibility test, one contour source
	String withStroke = squareShape();
	withStroke +=
		"fill 0 0 0 1\n"
		"stroke 0.2 butt miter 4 open\n"
		"contour 2\n"
		"v -3 -3\n"
		"v 3 3\n";
	std::vector<VectorTessellator::Region> regions;
	REQUIRE(Orkige::VectorShapeAsset::parse(withStroke, regions));
	REQUIRE(regions.size() == 2);
	CHECK(ShapeCollider::isSolidRegion(regions[0]));
	CHECK_FALSE(ShapeCollider::isSolidRegion(regions[1]));

	std::vector<std::vector<Point> > contours;
	ShapeCollider::extractContours(regions, contours);
	REQUIRE(contours.size() == 1);

	Mesh mesh;
	String error;
	REQUIRE(MeshExtrude::extrudeShape(mesh, regions, 1.0f, false, &error));
	// only the square was extruded (2 caps of 2 triangles + 4 wall quads)
	CHECK(mesh.triangleCount() == 12u);

	// openLoop drops a repeated closing vertex for every consumer alike
	std::vector<Point> closed;
	closed.push_back(Point(0.0f, 0.0f));
	closed.push_back(Point(1.0f, 0.0f));
	closed.push_back(Point(1.0f, 1.0f));
	closed.push_back(Point(0.0f, 0.0f));
	CHECK(ShapeCollider::openLoop(closed).size() == 3);
}
