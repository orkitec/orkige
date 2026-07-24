/**************************************************************
	created:	2026/07/24 at 12:00
	filename: 	EditorCameraGizmoTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the pure camera-frustum gizmo line builder
	(tools/editor/EditorCameraGizmo.h) - the geometry the Scene panel uploads
	to draw a selected camera's view volume. The panel-side node/mesh plumbing
	is exercised by the editor_camera integration selfcheck.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <EditorCameraGizmo.h>

#include <cmath>

namespace
{
	// widest |x| / tallest |y| among the points at a given depth |z| (the
	// builder interleaves near/far edges, so select by depth rather than index)
	float maxAbsXAtDepth(std::vector<Orkige::Vec3> const& points, float depth)
	{
		float m = 0.0f;
		for (auto const& p : points)
		{
			if (std::abs(std::abs(p.z) - depth) < 1e-3f)
			{
				m = std::max(m, std::abs(p.x));
			}
		}
		return m;
	}
	float maxAbsYAtDepth(std::vector<Orkige::Vec3> const& points, float depth)
	{
		float m = 0.0f;
		for (auto const& p : points)
		{
			if (std::abs(std::abs(p.z) - depth) < 1e-3f)
			{
				m = std::max(m, std::abs(p.y));
			}
		}
		return m;
	}
	float maxAbsX(std::vector<Orkige::Vec3> const& points)
	{
		float m = 0.0f;
		for (auto const& p : points)
		{
			m = std::max(m, std::abs(p.x));
		}
		return m;
	}
}

TEST_CASE("frustum gizmo emits 12 segments (24 points) both modes",
	"[editor][camera][gizmo]")
{
	std::vector<Orkige::Vec3> points;
	std::vector<Orkige::Color> colours;

	Orkige::CameraFrustumParams perspective;	// default = PM_PERSPECTIVE
	Orkige::buildCameraFrustumLines(perspective, 1.6f, points, colours);
	REQUIRE(points.size() == 24);
	REQUIRE(colours.size() == 24);
	REQUIRE(points.size() ==
		static_cast<std::size_t>(2 * Orkige::editorCameraFrustumSegmentCount()));

	Orkige::CameraFrustumParams ortho;
	ortho.projectionMode = 1;	// PM_ORTHOGRAPHIC
	Orkige::buildCameraFrustumLines(ortho, 1.6f, points, colours);
	REQUIRE(points.size() == 24);
	REQUIRE(colours.size() == 24);
}

TEST_CASE("frustum gizmo colours every vertex with the neutral gizmo colour",
	"[editor][camera][gizmo]")
{
	std::vector<Orkige::Vec3> points;
	std::vector<Orkige::Color> colours;
	Orkige::CameraFrustumParams params;
	Orkige::buildCameraFrustumLines(params, 1.6f, points, colours);
	const Orkige::Color expected = Orkige::editorCameraFrustumColour();
	for (auto const& c : colours)
	{
		REQUIRE(c.r == expected.r);
		REQUIRE(c.g == expected.g);
		REQUIRE(c.b == expected.b);
	}
}

TEST_CASE("perspective frustum grows with depth and looks down -Z",
	"[editor][camera][gizmo]")
{
	std::vector<Orkige::Vec3> points;
	std::vector<Orkige::Color> colours;
	Orkige::CameraFrustumParams params;	// perspective, fov 45
	const float aspect = 1.6f;
	Orkige::buildCameraFrustumLines(params, aspect, points, colours);

	// every point sits at negative z (the camera looks down local -Z)
	for (auto const& p : points)
	{
		REQUIRE(p.z <= 0.0f);
	}
	const float nearZ = Orkige::editorCameraFrustumNear();
	const float farZ = Orkige::editorCameraFrustumFar();
	const float nearHalfW = maxAbsXAtDepth(points, nearZ);
	const float farHalfW = maxAbsXAtDepth(points, farZ);
	const float farHalfH = maxAbsYAtDepth(points, farZ);
	REQUIRE(nearHalfW > 0.0f);
	REQUIRE(farHalfW > 0.0f);
	// a perspective frustum grows: the far plane is wider than the near plane
	REQUIRE(farHalfW > nearHalfW);
	// the width follows the aspect: |x| / |y| == aspect at any corner
	REQUIRE(farHalfW == Catch::Approx(farHalfH * aspect));
}

TEST_CASE("orthographic box keeps a constant cross-section sized by orthoSize",
	"[editor][camera][gizmo]")
{
	std::vector<Orkige::Vec3> points;
	std::vector<Orkige::Color> colours;
	Orkige::CameraFrustumParams params;
	params.projectionMode = 1;		// PM_ORTHOGRAPHIC
	params.fitMode = 0;				// FM_HEIGHT: orthoSize is the half-height
	params.orthoSize = 5.0f;
	const float aspect = 2.0f;
	Orkige::buildCameraFrustumLines(params, aspect, points, colours);

	// near and far rectangles are the same size (a box, not a cone)
	const float nearZ = Orkige::editorCameraFrustumNear();
	const float farZ = Orkige::editorCameraFrustumFar();
	REQUIRE(maxAbsYAtDepth(points, nearZ) == Catch::Approx(params.orthoSize));
	REQUIRE(maxAbsYAtDepth(points, farZ) == Catch::Approx(params.orthoSize));
	REQUIRE(maxAbsXAtDepth(points, nearZ) ==
		Catch::Approx(params.orthoSize * aspect));
	REQUIRE(maxAbsXAtDepth(points, farZ) ==
		Catch::Approx(maxAbsXAtDepth(points, nearZ)));
}

TEST_CASE("orthographic FM_WIDTH derives the half-height via CameraFit",
	"[editor][camera][gizmo]")
{
	std::vector<Orkige::Vec3> points;
	std::vector<Orkige::Color> colours;
	Orkige::CameraFrustumParams params;
	params.projectionMode = 1;
	params.fitMode = static_cast<int>(Orkige::CameraFit::FIT_WIDTH);
	params.designWidth = 10.0f;
	params.designHeight = 6.0f;
	const float aspect = 1.5f;
	Orkige::buildCameraFrustumLines(params, aspect, points, colours);

	const float expectedHalfHeight = Orkige::CameraFit::orthoHalfHeight(
		Orkige::CameraFit::FIT_WIDTH, params.designWidth, params.designHeight,
		aspect);
	REQUIRE(maxAbsX(points) == Catch::Approx(expectedHalfHeight * aspect));
}
