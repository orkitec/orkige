/**************************************************************
	created:	2026/07/07 at 12:00
	filename: 	EditorCameraTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the editor's fly-camera math
	(tools/editor/EditorCamera.{h,cpp}) - the exact step function the
	Scene panel's right-mouse fly mode integrates every frame.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <EditorCamera.h>

#include <cmath>

namespace
{
	bool nearlyEqual(Ogre::Vector3 const& a, Ogre::Vector3 const& b,
		float epsilon = 1e-4f)
	{
		return (a - b).length() < epsilon;
	}
}

TEST_CASE("fly forward moves the camera along the view direction",
	"[editor][camera]")
{
	Orkige::EditorCameraState camera; // boot pose: yaw 0, pitch ~15.5, d ~9.3
	const Ogre::Vector3 positionBefore = Orkige::editorCameraPosition(camera);
	const Ogre::Vector3 forward = Orkige::editorCameraForward(camera);
	const float distanceBefore = camera.distance;

	float baseSpeed = 2.0f;
	Orkige::FlyInput input;
	input.moveForward = true;
	Orkige::flyCameraStep(camera, input, 1.0f, 0.4f, baseSpeed);

	const Ogre::Vector3 positionAfter = Orkige::editorCameraPosition(camera);
	// one second at speed 2 = 2 world units straight ahead
	REQUIRE(nearlyEqual(positionAfter, positionBefore + forward * 2.0f));
	// the target moved along, the orbit distance is untouched
	REQUIRE(camera.distance == distanceBefore);
	REQUIRE(nearlyEqual(camera.target,
		positionAfter + Orkige::editorCameraForward(camera) * camera.distance));
}

TEST_CASE("fly mouselook pivots around the camera position",
	"[editor][camera]")
{
	Orkige::EditorCameraState camera;
	const Ogre::Vector3 positionBefore = Orkige::editorCameraPosition(camera);
	const Ogre::Vector3 targetBefore = camera.target;
	const float yawBefore = camera.yawDeg;

	float baseSpeed = 6.0f;
	Orkige::FlyInput input;
	input.lookDeltaX = 100.0f; // mouse to the right = look right
	Orkige::flyCameraStep(camera, input, 0.016f, 0.4f, baseSpeed);

	// looking around must NOT move the camera - only the target re-derives
	REQUIRE(nearlyEqual(Orkige::editorCameraPosition(camera), positionBefore,
		1e-3f));
	REQUIRE(camera.yawDeg == yawBefore - 100.0f * 0.4f);
	REQUIRE_FALSE(nearlyEqual(camera.target, targetBefore, 1e-2f));
	// the re-derived target stays exactly "distance" ahead of the camera
	REQUIRE(std::abs((camera.target -
		Orkige::editorCameraPosition(camera)).length() - camera.distance) <
		1e-3f);
}

TEST_CASE("fly pitch clamps at +-85 degrees", "[editor][camera]")
{
	Orkige::EditorCameraState camera;
	float baseSpeed = 6.0f;
	Orkige::FlyInput input;
	input.lookDeltaY = 100000.0f; // yank the mouse all the way down
	Orkige::flyCameraStep(camera, input, 0.016f, 0.4f, baseSpeed);
	REQUIRE(camera.pitchDeg == 85.0f);
	input.lookDeltaY = -100000.0f;
	Orkige::flyCameraStep(camera, input, 0.016f, 0.4f, baseSpeed);
	REQUIRE(camera.pitchDeg == -85.0f);
}

TEST_CASE("default look sensitivity rotates 0.15 degrees per relative count",
	"[editor][camera]")
{
	// fly mode runs in SDL relative mouse mode: the look input is the raw
	// xrel/yrel COUNT (1:1 with physical mouse travel - relative counts are
	// never multiplied by the retina backing-store factor, so unlike the old
	// absolute-pixel path there is NO content-scale division anywhere). One
	// count turns the camera by exactly 0.15 degrees at the default
	// sensitivity, a 100-count sweep by 15 degrees.
	Orkige::EditorCameraState camera;
	const float yawBefore = camera.yawDeg;
	float baseSpeed = 6.0f;

	Orkige::FlyInput oneCount;
	oneCount.lookDeltaX = 1.0f;
	Orkige::flyCameraStep(camera, oneCount, 0.016f,
		Orkige::FLY_LOOK_SPEED_DEFAULT, baseSpeed);
	REQUIRE(camera.yawDeg == yawBefore - Orkige::FLY_LOOK_SPEED_DEFAULT);

	Orkige::FlyInput hundredCounts;
	hundredCounts.lookDeltaX = 100.0f;
	Orkige::flyCameraStep(camera, hundredCounts, 0.016f,
		Orkige::FLY_LOOK_SPEED_DEFAULT, baseSpeed);
	REQUIRE(std::abs(camera.yawDeg -
		(yawBefore - 101.0f * Orkige::FLY_LOOK_SPEED_DEFAULT)) < 1e-4f);
	// ... and the default really is the sane 0.15, not a radians-vs-degrees
	// or counts-vs-pixels accident
	REQUIRE(Orkige::FLY_LOOK_SPEED_DEFAULT == 0.15f);
}

TEST_CASE("relative look deltas keep FPS-style directions",
	"[editor][camera]")
{
	// invert consistency for the relative-mode deltas (+xrel = mouse moved
	// right, +yrel = mouse moved down): right turns the view right (yaw
	// decreases - the orbit yaw spins the camera the other way around its
	// target), down pitches the view down (orbit pitchDeg increases: the
	// camera RISES on the orbit sphere while looking at its own former
	// forward point = looking further down). No inversion doubles up.
	Orkige::EditorCameraState camera;
	const float yawBefore = camera.yawDeg;
	const float pitchBefore = camera.pitchDeg;
	float baseSpeed = 6.0f;

	Orkige::FlyInput mouseRightDown;
	mouseRightDown.lookDeltaX = 10.0f;	// 10 counts right
	mouseRightDown.lookDeltaY = 10.0f;	// 10 counts down
	Orkige::flyCameraStep(camera, mouseRightDown, 0.016f,
		Orkige::FLY_LOOK_SPEED_DEFAULT, baseSpeed);
	REQUIRE(camera.yawDeg < yawBefore);
	REQUIRE(camera.pitchDeg > pitchBefore);

	// and back: the exact opposite deltas restore the exact angles
	Orkige::FlyInput mouseLeftUp;
	mouseLeftUp.lookDeltaX = -10.0f;
	mouseLeftUp.lookDeltaY = -10.0f;
	Orkige::flyCameraStep(camera, mouseLeftUp, 0.016f,
		Orkige::FLY_LOOK_SPEED_DEFAULT, baseSpeed);
	REQUIRE(std::abs(camera.yawDeg - yawBefore) < 1e-4f);
	REQUIRE(std::abs(camera.pitchDeg - pitchBefore) < 1e-4f);
}

TEST_CASE("camera drag gate swallows the first frame of every hold",
	"[editor][camera]")
{
	Orkige::CameraDragGate gate;
	// idle: nothing to apply
	REQUIRE_FALSE(gate.update(false));
	// first held frame swallowed (may carry a huge stale delta), second on
	// applies
	REQUIRE_FALSE(gate.update(true));
	REQUIRE(gate.update(true));
	REQUIRE(gate.update(true));
	// release resets the gate - the NEXT hold swallows its first frame again
	REQUIRE_FALSE(gate.update(false));
	REQUIRE_FALSE(gate.update(true));
	REQUIRE(gate.update(true));
}

TEST_CASE("fly boost and scroll speed tuning", "[editor][camera]")
{
	Orkige::EditorCameraState slow;
	Orkige::EditorCameraState fast;
	float slowSpeed = 2.0f;
	float fastSpeed = 2.0f;
	Orkige::FlyInput input;
	input.moveForward = true;
	Orkige::flyCameraStep(slow, input, 1.0f, 0.4f, slowSpeed);
	input.boost = true; // Shift = FLY_BOOST_FACTOR x
	Orkige::flyCameraStep(fast, input, 1.0f, 0.4f, fastSpeed);
	const float slowDistance =
		(slow.target - Orkige::EditorCameraState().target).length();
	const float fastDistance =
		(fast.target - Orkige::EditorCameraState().target).length();
	REQUIRE(std::abs(fastDistance -
		slowDistance * Orkige::FLY_BOOST_FACTOR) < 1e-3f);

	// scroll adjusts the base speed multiplicatively, clamped to the limits
	float baseSpeed = 6.0f;
	Orkige::FlyInput scrollUp;
	scrollUp.speedScroll = 1.0f;
	Orkige::EditorCameraState camera;
	Orkige::flyCameraStep(camera, scrollUp, 0.016f, 0.4f, baseSpeed);
	REQUIRE(baseSpeed > 6.0f);
	scrollUp.speedScroll = 1000.0f;
	Orkige::flyCameraStep(camera, scrollUp, 0.016f, 0.4f, baseSpeed);
	REQUIRE(baseSpeed == Orkige::FLY_SPEED_MAX);
	scrollUp.speedScroll = -1000.0f;
	Orkige::flyCameraStep(camera, scrollUp, 0.016f, 0.4f, baseSpeed);
	REQUIRE(baseSpeed == Orkige::FLY_SPEED_MIN);
}
