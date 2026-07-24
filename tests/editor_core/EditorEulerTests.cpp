/**************************************************************
	created:	2026/07/24 at 12:00
	filename: 	EditorEulerTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the Inspector quaternion<->Euler
	conversion (tools/editor/EditorEuler.{h,cpp}): round-trip
	identity across representative rotations including gimbal-
	adjacent and exact-gimbal cases, and the pure-axis mappings.
	Intrinsic Y-X-Z order.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <EditorEuler.h>

#include <cmath>

using Orkige::quatToEulerDegrees;
using Orkige::eulerDegreesToQuat;

namespace
{
	//! two quaternions are the same rotation when |dot| ~= 1 (q and -q are the
	//! same orientation)
	bool sameRotation(const float a[4], const float b[4], float eps = 1e-4f)
	{
		const float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
		return std::abs(dot) > 1.0f - eps;
	}

	//! quat -> euler -> quat must reproduce the original rotation
	void checkRoundTrip(const float q[4])
	{
		float euler[3];
		quatToEulerDegrees(q, euler);
		float back[4];
		eulerDegreesToQuat(euler, back);
		CHECK(sameRotation(q, back));
	}
}

TEST_CASE("euler: identity and pure-axis rotations", "[unit]")
{
	// identity
	const float ident[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
	float e[3];
	quatToEulerDegrees(ident, e);
	CHECK(std::abs(e[0]) < 1e-3f);
	CHECK(std::abs(e[1]) < 1e-3f);
	CHECK(std::abs(e[2]) < 1e-3f);

	// a pure 90 deg about Y (yaw): euler (0, 90, 0)
	float qY[4];
	const float eY[3] = { 0.0f, 90.0f, 0.0f };
	eulerDegreesToQuat(eY, qY);
	float outY[3];
	quatToEulerDegrees(qY, outY);
	CHECK(std::abs(outY[0]) < 1e-3f);
	CHECK(std::abs(outY[1] - 90.0f) < 1e-3f);
	CHECK(std::abs(outY[2]) < 1e-3f);

	// a pure 90 deg about X (pitch): euler (90, 0, 0) - exact gimbal
	float qX[4];
	const float eX[3] = { 90.0f, 0.0f, 0.0f };
	eulerDegreesToQuat(eX, qX);
	checkRoundTrip(qX);

	// a pure 45 deg about Z (roll): euler (0, 0, 45)
	float qZ[4];
	const float eZ[3] = { 0.0f, 0.0f, 45.0f };
	eulerDegreesToQuat(eZ, qZ);
	float outZ[3];
	quatToEulerDegrees(qZ, outZ);
	CHECK(std::abs(outZ[0]) < 1e-3f);
	CHECK(std::abs(outZ[1]) < 1e-3f);
	CHECK(std::abs(outZ[2] - 45.0f) < 1e-3f);
}

TEST_CASE("euler: round-trip across representative rotations", "[unit]")
{
	const float eulers[][3] = {
		{ 30.0f, 45.0f, 60.0f },
		{ -120.0f, 15.0f, 200.0f },
		{ 10.0f, -80.0f, 5.0f },
		{ 179.0f, 20.0f, -175.0f },
		{ 89.9f, 30.0f, 10.0f },		// gimbal-adjacent (near +90 pitch)
		{ -89.9f, -30.0f, 120.0f },		// gimbal-adjacent (near -90 pitch)
	};
	for (auto const& e : eulers)
	{
		float q[4];
		eulerDegreesToQuat(e, q);
		checkRoundTrip(q);
	}
}

TEST_CASE("euler: exact gimbal folds Z into Y but preserves the quat", "[unit]")
{
	// X = +90 with a non-zero Y and Z: the reported euler pins Z = 0, but the
	// reconstructed quaternion must still match
	const float e[3] = { 90.0f, 40.0f, 25.0f };
	float q[4];
	eulerDegreesToQuat(e, q);
	float euler[3];
	quatToEulerDegrees(q, euler);
	CHECK(std::abs(euler[2]) < 1e-2f);	// Z folded to ~0
	checkRoundTrip(q);
}
