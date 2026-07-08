// EditorCamera - the Scene panel's camera model (orbit sphere + fly mode).
//
// The scene camera is parametrized as an orbit: spherical yaw/pitch/distance
// around a target point. Every navigation mode mutates the SAME state:
// - orbit (Alt+left drag): yaw/pitch change, target/distance fixed
// - pan (middle drag): target slides in the camera plane
// - zoom (scroll): distance changes
// - fly (right-mouse hold): flyCameraStep below - mouselook pivots around the
//   CAMERA position (not the target) and WASD/QE translate it; the target is
//   continuously re-derived as "distance units in front of the camera", so
//   releasing the right button drops straight back into sane orbit behavior
//   and the ViewManipulate corner gizmo keeps working off the same numbers.
//
// Pure math functions (engine math vocabulary, engine_render/RenderMath.h),
// no ImGui/SDL/scene dependencies - the unit tests (tests/editor_core)
// exercise them headlessly.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#pragma once

#include <engine_render/RenderMath.h>

namespace Orkige
{
	//! spherical orbit camera state; defaults reproduce the editor's classic
	//! boot camera at (0, 2.5, 9) looking at the origin
	struct EditorCameraState
	{
		float yawDeg = 0.0f;			//!< orbit yaw (degrees)
		float pitchDeg = 15.524f;		//!< orbit pitch (degrees, clamped +-85)
		float distance = 9.3408f;		//!< camera distance from the target
		Vec3 target = Vec3::ZERO;		//!< orbit pivot
	};

	//! world position of the camera on its orbit sphere
	Vec3 editorCameraPosition(EditorCameraState const& camera);
	//! unit view direction (camera towards target)
	Vec3 editorCameraForward(EditorCameraState const& camera);

	//! one frame of fly-mode input (sampled while the right button is held)
	struct FlyInput
	{
		//! mouse look deltas in raw relative-mode counts (SDL xrel/yrel
		//! accumulated over the frame), +x = right, +y = down. Relative
		//! counts track physical mouse travel 1:1 - no retina/backing-store
		//! scale applies (unlike the absolute pixel positions ImGui works in)
		float lookDeltaX = 0.0f;
		float lookDeltaY = 0.0f;
		bool moveForward = false;	//!< W
		bool moveBack = false;		//!< S
		bool moveLeft = false;		//!< A
		bool moveRight = false;		//!< D
		bool moveDown = false;		//!< Q (world down)
		bool moveUp = false;		//!< E (world up)
		bool boost = false;			//!< Shift - FLY_BOOST_FACTOR x speed
		float speedScroll = 0.0f;	//!< scroll wheel steps - adjust base speed
	};

	//! Shift multiplies the fly speed by this
	const float FLY_BOOST_FACTOR = 3.0f;
	//! base fly speed limits (world units per second)
	const float FLY_SPEED_MIN = 0.5f;
	const float FLY_SPEED_MAX = 50.0f;
	//! default mouselook sensitivity: degrees of rotation per relative-mode
	//! mouse COUNT (one count ~= one point of physical mouse travel). Fly
	//! mode feeds SDL's relative xrel/yrel counts straight in - relative
	//! counts are never scaled by the retina backing-store factor, so unlike
	//! the old absolute-delta path there is NO content-scale division; the
	//! constant is directly the "per unit of hand movement" feel.
	const float FLY_LOOK_SPEED_DEFAULT = 0.15f;

	//! @brief first-frame delta swallow for held-button camera drags.
	//! The frame a fly/orbit hold begins can carry a bogus mouse delta (a
	//! click after refocus, a cursor warp, an event backlog) - applying it
	//! yanks the camera before the user even drags. Feed the per-frame hold
	//! state; the delta may only be applied from the SECOND held frame on.
	//! Deliberately KEPT for the relative-mode fly path too: entering SDL
	//! relative mouse mode mid-frame can leave an absolute-motion backlog
	//! and some platforms synthesize a jump delta on capture - the gate
	//! costs one frame of a genuine (tiny) delta and swallows both.
	class CameraDragGate
	{
	public:
		//! @param held is the drag button down this frame?
		//! @return true when this frame's mouse delta may be applied (always
		//! false on the first frame of a hold and while not held)
		bool update(bool held)
		{
			const bool apply = held && mWasHeld;
			mWasHeld = held;
			return apply;
		}

	private:
		bool mWasHeld = false;	//!< was the button down last frame too?
	};

	//! @brief integrate one frame of fly mode into the orbit camera state.
	//! Mouselook rotates around the camera POSITION (the position stays put,
	//! the target re-derives distance units along the new view direction);
	//! WASD/QE translate camera and target together. Scroll input adjusts
	//! baseSpeed multiplicatively (clamped to FLY_SPEED_MIN/MAX).
	//! @param camera the orbit state to advance
	//! @param input this frame's fly input
	//! @param deltaSeconds frame time
	//! @param lookSpeedDegPerPixel mouselook sensitivity
	//! @param baseSpeed [in/out] base movement speed (world units/second)
	void flyCameraStep(EditorCameraState& camera, FlyInput const& input,
		float deltaSeconds, float lookSpeedDegPerPixel, float& baseSpeed);
}
