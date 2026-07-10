/**************************************************************
	created:	2026/07/11 at 10:00
	filename: 	TiltCalibrationTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tilt-calibration unit tests: the pure offset math
	(TiltCalibration::angleForPose + ::apply) that InputManager wraps -
	"hold a pose, tap Calibrate, that pose becomes neutral (0,-1)". No
	window, no accelerometer.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core_util/TiltCalibration.h"

#include <cmath>

using namespace Orkige;
using Catch::Matchers::WithinAbs;

namespace
{
	//! rotate (x,y) by -calibAngle then normalize - what InputManager::getTilt
	//! does to the raw pose
	void applyNormalized(float & x, float & y, float calibAngle)
	{
		TiltCalibration::apply(x, y, calibAngle);
		const float len = std::sqrt(x * x + y * y);
		if (len > 1.0e-4f)
		{
			x /= len;
			y /= len;
		}
	}
}

TEST_CASE("tilt calibration of the upright reference is the identity",
	"[unit][tilt]")
{
	// (0,-1) is already neutral - calibrating there captures a zero offset
	const float angle = TiltCalibration::angleForPose(0.0f, -1.0f);
	CHECK_THAT(angle, WithinAbs(0.0f, 1.0e-5f));
}

TEST_CASE("a near-zero pose calibrates to the identity (no direction)",
	"[unit][tilt]")
{
	CHECK(TiltCalibration::angleForPose(0.0f, 0.0f) == 0.0f);
}

TEST_CASE("calibrating at a pose makes that pose read as neutral (fixed point)",
	"[unit][tilt]")
{
	// a pose tilted ~30 degrees to the right of upright
	const float poseX = std::sin(0.5f);
	const float poseY = -std::cos(0.5f);
	const float angle = TiltCalibration::angleForPose(poseX, poseY);

	float x = poseX;
	float y = poseY;
	applyNormalized(x, y, angle);
	// the calibrated-at pose comes back as the upright reference (0,-1)
	CHECK_THAT(x, WithinAbs(0.0f, 1.0e-4f));
	CHECK_THAT(y, WithinAbs(-1.0f, 1.0e-4f));
}

TEST_CASE("calibration is a pure rotation: it preserves the deflection angle",
	"[unit][tilt]")
{
	// calibrate at pose P, then feed a pose rotated a further delta from P:
	// after calibration it must read as rotated the same delta from neutral
	const float baseAngle = 0.4f;		// the calibrated neutral pose
	const float delta = 0.25f;			// how far the probe pose is turned from P

	const float calib = TiltCalibration::angleForPose(
		std::sin(baseAngle), -std::cos(baseAngle));

	float x = std::sin(baseAngle + delta);
	float y = -std::cos(baseAngle + delta);
	applyNormalized(x, y, calib);

	// the resulting direction should be tiltVectorFromAngle(delta) = (sin, -cos)
	CHECK_THAT(x, WithinAbs(std::sin(delta), 1.0e-4f));
	CHECK_THAT(y, WithinAbs(-std::cos(delta), 1.0e-4f));
}

TEST_CASE("a zero calibration angle leaves the pose untouched", "[unit][tilt]")
{
	float x = 0.3f;
	float y = -0.9f;
	TiltCalibration::apply(x, y, 0.0f);
	CHECK(x == 0.3f);
	CHECK(y == -0.9f);
}
