// EditorCamera - orbit/fly camera math (see header).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "EditorCamera.h"

#include <algorithm>
#include <cmath>

namespace Orkige
{
	namespace
	{
		//! unit offset from the target towards the camera for the given angles
		Vec3 orbitOffset(float yawDeg, float pitchDeg)
		{
			const float yaw = Radian(Degree(yawDeg)).valueRadians();
			const float pitch = Radian(Degree(pitchDeg)).valueRadians();
			return Vec3(
				std::cos(pitch) * std::sin(yaw),
				std::sin(pitch),
				std::cos(pitch) * std::cos(yaw));
		}
	}
	//---------------------------------------------------------
	Vec3 editorCameraPosition(EditorCameraState const& camera)
	{
		return camera.target +
			orbitOffset(camera.yawDeg, camera.pitchDeg) * camera.distance;
	}
	//---------------------------------------------------------
	Vec3 editorCameraForward(EditorCameraState const& camera)
	{
		return -orbitOffset(camera.yawDeg, camera.pitchDeg);
	}
	//---------------------------------------------------------
	void flyCameraStep(EditorCameraState& camera, FlyInput const& input,
		float deltaSeconds, float lookSpeedDegPerPixel, float& baseSpeed)
	{
		// scroll while flying tunes the base speed (multiplicative feels
		// right across the 0.5..50 range)
		if (input.speedScroll != 0.0f)
		{
			baseSpeed = std::clamp(
				baseSpeed * std::pow(1.2f, input.speedScroll),
				FLY_SPEED_MIN, FLY_SPEED_MAX);
		}

		// mouselook: pivot around the camera position - remember it, turn the
		// angles, then re-derive the orbit target "distance" units ahead so
		// the position solves back to exactly where it was
		const Vec3 position = editorCameraPosition(camera);
		camera.yawDeg -= input.lookDeltaX * lookSpeedDegPerPixel;
		camera.pitchDeg = std::clamp(
			camera.pitchDeg + input.lookDeltaY * lookSpeedDegPerPixel,
			-85.0f, 85.0f);
		camera.target = position + editorCameraForward(camera) * camera.distance;

		// WASD/QE: translate camera and target together (position is derived,
		// so moving the target IS moving the camera)
		const Vec3 forward = editorCameraForward(camera);
		Vec3 right = forward.crossProduct(Vec3::UNIT_Y);
		if (right.squaredLength() < 1e-8f)
		{
			right = Vec3::UNIT_X; // looking straight up/down
		}
		else
		{
			right.normalise();
		}
		Vec3 move =
			forward * (static_cast<float>(input.moveForward) -
				static_cast<float>(input.moveBack)) +
			right * (static_cast<float>(input.moveRight) -
				static_cast<float>(input.moveLeft)) +
			Vec3::UNIT_Y * (static_cast<float>(input.moveUp) -
				static_cast<float>(input.moveDown));
		if (move.squaredLength() > 0.0f)
		{
			move.normalise();
			const float speed =
				baseSpeed * (input.boost ? FLY_BOOST_FACTOR : 1.0f);
			camera.target += move * speed * deltaSeconds;
		}
	}
}
