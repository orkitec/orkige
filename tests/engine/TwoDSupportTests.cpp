/**************************************************************
	created:	2026/07/08 at 12:00
	filename: 	TwoDSupportTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests of the 2D support tier: SpriteComponent geometry
	helpers + serialization round-trip, CameraComponent projection state
	round-trip and the InputManager tilt simulation math. Everything here
	runs on DETACHED components (no Ogre::Root, no scene nodes) - the pure
	helpers were designed for exactly that. The rendered end-to-end proof
	is the player_roller_selfcheck integration run (projects/roller).
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "EngineTestEnvironment.h"

#include <engine_gocomponent/SpriteComponent.h>
#include <engine_gocomponent/SpriteAnimationComponent.h>
#include <engine_gocomponent/CameraComponent.h>
#include <engine_input/InputManager.h>
#include <core_serialization/XMLArchive.h>
// the classic render-queue constant the zOrder mapping is asserted against
// (renderQueueForZOrder itself is backend-free - base queue 50 on
// both Ogre backends; this classic-only test pins it to the classic enum)
#include <OgreRenderQueue.h>

#include <filesystem>

using Orkige::optr;
using Orkige::woptr;

using Catch::Approx;

namespace
{
	//! RAII temp file below std::filesystem::temp_directory_path()
	struct TempFile
	{
		Orkige::String path;
		explicit TempFile(std::string const & name)
			: path((std::filesystem::temp_directory_path() / name).string())
		{
			std::error_code ignored;
			std::filesystem::remove(this->path, ignored);
		}
		~TempFile()
		{
			std::error_code ignored;
			std::filesystem::remove(this->path, ignored);
		}
	};
	//! save/load are protected on components - widen for the round-trip tests
	struct TestSprite : Orkige::SpriteComponent
	{
		using Orkige::SpriteComponent::save;
		using Orkige::SpriteComponent::load;
	};
	struct TestCamera : Orkige::CameraComponent
	{
		using Orkige::CameraComponent::save;
		using Orkige::CameraComponent::load;
	};
	struct TestSpriteAnim : Orkige::SpriteAnimationComponent
	{
		using Orkige::SpriteAnimationComponent::save;
		using Orkige::SpriteAnimationComponent::load;
	};
}

TEST_CASE("SpriteComponent derives its size from the texture aspect", "[sprite]")
{
	float width = 0.0f, height = 0.0f;

	// both unset: unit height, width follows a 2:1 texture
	Orkige::SpriteComponent::resolveSize(0.0f, 0.0f, 128.0f, 64.0f, width, height);
	CHECK(width == Approx(2.0f));
	CHECK(height == Approx(1.0f));

	// height given: width from the aspect
	Orkige::SpriteComponent::resolveSize(0.0f, 3.0f, 128.0f, 64.0f, width, height);
	CHECK(width == Approx(6.0f));
	CHECK(height == Approx(3.0f));

	// width given: height from the aspect
	Orkige::SpriteComponent::resolveSize(4.0f, 0.0f, 64.0f, 128.0f, width, height);
	CHECK(width == Approx(4.0f));
	CHECK(height == Approx(8.0f));

	// both given win over the texture
	Orkige::SpriteComponent::resolveSize(5.0f, 2.0f, 64.0f, 64.0f, width, height);
	CHECK(width == Approx(5.0f));
	CHECK(height == Approx(2.0f));

	// no texture loaded yet: square fallback
	Orkige::SpriteComponent::resolveSize(0.0f, 0.0f, 0.0f, 0.0f, width, height);
	CHECK(width == Approx(1.0f));
	CHECK(height == Approx(1.0f));
}

TEST_CASE("SpriteComponent UV corners honor the rect and the flips", "[sprite]")
{
	Ogre::Vector2 uv[4];

	// plain full rect: TL, TR, BR, BL
	Orkige::SpriteComponent::computeUVCorners(0.0f, 0.0f, 1.0f, 1.0f,
		false, false, uv);
	CHECK(uv[0] == Ogre::Vector2(0.0f, 0.0f));
	CHECK(uv[1] == Ogre::Vector2(1.0f, 0.0f));
	CHECK(uv[2] == Ogre::Vector2(1.0f, 1.0f));
	CHECK(uv[3] == Ogre::Vector2(0.0f, 1.0f));

	// atlas sub-rect
	Orkige::SpriteComponent::computeUVCorners(0.25f, 0.5f, 0.75f, 1.0f,
		false, false, uv);
	CHECK(uv[0] == Ogre::Vector2(0.25f, 0.5f));
	CHECK(uv[2] == Ogre::Vector2(0.75f, 1.0f));

	// flip X mirrors u, flip Y mirrors v
	Orkige::SpriteComponent::computeUVCorners(0.0f, 0.0f, 1.0f, 1.0f,
		true, false, uv);
	CHECK(uv[0] == Ogre::Vector2(1.0f, 0.0f));
	CHECK(uv[1] == Ogre::Vector2(0.0f, 0.0f));
	Orkige::SpriteComponent::computeUVCorners(0.0f, 0.0f, 1.0f, 1.0f,
		false, true, uv);
	CHECK(uv[0] == Ogre::Vector2(0.0f, 1.0f));
	CHECK(uv[3] == Ogre::Vector2(0.0f, 0.0f));
	Orkige::SpriteComponent::computeUVCorners(0.0f, 0.0f, 1.0f, 1.0f,
		true, true, uv);
	CHECK(uv[0] == Ogre::Vector2(1.0f, 1.0f));
	CHECK(uv[2] == Ogre::Vector2(0.0f, 0.0f));
}

TEST_CASE("SpriteComponent zOrder maps to a clamped render queue", "[sprite]")
{
	CHECK(Orkige::SpriteComponent::renderQueueForZOrder(0) ==
		static_cast<Ogre::uint8>(Ogre::RENDER_QUEUE_MAIN));
	CHECK(Orkige::SpriteComponent::renderQueueForZOrder(3) ==
		static_cast<Ogre::uint8>(Ogre::RENDER_QUEUE_MAIN) + 3);
	CHECK(Orkige::SpriteComponent::renderQueueForZOrder(-3) ==
		static_cast<Ogre::uint8>(Ogre::RENDER_QUEUE_MAIN) - 3);
	// out-of-range zOrders clamp instead of wandering into overlay queues
	CHECK(Orkige::SpriteComponent::renderQueueForZOrder(10000) ==
		static_cast<Ogre::uint8>(Ogre::RENDER_QUEUE_MAIN) +
		Orkige::SpriteComponent::ZORDER_MAX);
	CHECK(Orkige::SpriteComponent::renderQueueForZOrder(-10000) ==
		static_cast<Ogre::uint8>(Ogre::RENDER_QUEUE_MAIN) +
		Orkige::SpriteComponent::ZORDER_MIN);
}

TEST_CASE("frameToUVRect maps grid cells to UV sub-rects", "[sprite][flipbook]")
{
	float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;

	// 4x4 sheet, no inset (unknown texel size): exact cell boundaries.
	// frame 0 = top-left cell
	Orkige::SpriteComponent::frameToUVRect(0, 4, 4, 0.0f, 0.0f, u0, v0, u1, v1);
	CHECK(u0 == Approx(0.0f));
	CHECK(v0 == Approx(0.0f));
	CHECK(u1 == Approx(0.25f));
	CHECK(v1 == Approx(0.25f));

	// row-major: frame 5 = column 1, row 1 on a 4-wide grid
	Orkige::SpriteComponent::frameToUVRect(5, 4, 4, 0.0f, 0.0f, u0, v0, u1, v1);
	CHECK(u0 == Approx(0.25f));
	CHECK(v0 == Approx(0.25f));
	CHECK(u1 == Approx(0.5f));
	CHECK(v1 == Approx(0.5f));

	// last cell of the sheet (frame 15)
	Orkige::SpriteComponent::frameToUVRect(15, 4, 4, 0.0f, 0.0f, u0, v0, u1, v1);
	CHECK(u0 == Approx(0.75f));
	CHECK(v0 == Approx(0.75f));
	CHECK(u1 == Approx(1.0f));
	CHECK(v1 == Approx(1.0f));

	// non-square grid: 8 columns x 2 rows, frame 9 = column 1, row 1
	Orkige::SpriteComponent::frameToUVRect(9, 8, 2, 0.0f, 0.0f, u0, v0, u1, v1);
	CHECK(u0 == Approx(0.125f));
	CHECK(u1 == Approx(0.25f));
	CHECK(v0 == Approx(0.5f));
	CHECK(v1 == Approx(1.0f));

	// out-of-range frame clamps onto the sheet instead of sampling past 1.0
	Orkige::SpriteComponent::frameToUVRect(999, 4, 4, 0.0f, 0.0f, u0, v0, u1, v1);
	CHECK(u1 == Approx(1.0f));
	CHECK(v1 == Approx(1.0f));

	// a degenerate grid falls back to the full texture
	Orkige::SpriteComponent::frameToUVRect(0, 0, 0, 0.0f, 0.0f, u0, v0, u1, v1);
	CHECK(u0 == Approx(0.0f));
	CHECK(v0 == Approx(0.0f));
	CHECK(u1 == Approx(1.0f));
	CHECK(v1 == Approx(1.0f));
}

TEST_CASE("frameToUVRect insets half a texel against seam bleeding", "[sprite][flipbook]")
{
	float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;

	// 2x2 sheet on a 64x64 texture: each cell is 32 texels; half a texel is
	// 0.5/64 = 0.0078125 in UV. Cell 0 (top-left) pulls in on all sides.
	Orkige::SpriteComponent::frameToUVRect(0, 2, 2, 64.0f, 64.0f, u0, v0, u1, v1);
	const float halfTexel = 0.5f / 64.0f;
	CHECK(u0 == Approx(0.0f + halfTexel));
	CHECK(v0 == Approx(0.0f + halfTexel));
	CHECK(u1 == Approx(0.5f - halfTexel));
	CHECK(v1 == Approx(0.5f - halfTexel));

	// a non-square texture insets U and V by different amounts (per-axis
	// texel size): 128 wide, 32 tall
	Orkige::SpriteComponent::frameToUVRect(0, 2, 2, 128.0f, 32.0f, u0, v0, u1, v1);
	CHECK(u0 == Approx(0.5f / 128.0f));
	CHECK(v0 == Approx(0.5f / 32.0f));
}

TEST_CASE("pixelRectToUV maps an atlas region rect to UV (shared inset)",
	"[sprite][atlas]")
{
	float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;

	// a 32x48 region at (64,0) on a 256x256 atlas, no inset (unknown size)
	Orkige::SpriteComponent::pixelRectToUV(64.0f, 0.0f, 32.0f, 48.0f,
		256.0f, 256.0f, u0, v0, u1, v1);
	const float insetX = 0.5f / 256.0f;
	const float insetY = 0.5f / 256.0f;
	CHECK(u0 == Approx(64.0f / 256.0f + insetX));
	CHECK(u1 == Approx(96.0f / 256.0f - insetX));
	CHECK(v0 == Approx(0.0f + insetY));
	CHECK(v1 == Approx(48.0f / 256.0f - insetY));

	// the full-texture rect (0,0,w,h) matches frameToUVRect's degenerate grid
	Orkige::SpriteComponent::pixelRectToUV(0.0f, 0.0f, 128.0f, 128.0f,
		128.0f, 128.0f, u0, v0, u1, v1);
	CHECK(u0 == Approx(0.5f / 128.0f));
	CHECK(v0 == Approx(0.5f / 128.0f));
	CHECK(u1 == Approx(1.0f - 0.5f / 128.0f));
	CHECK(v1 == Approx(1.0f - 0.5f / 128.0f));

	// an unknown texture size (<= 0) falls back to the whole texture
	Orkige::SpriteComponent::pixelRectToUV(10.0f, 10.0f, 5.0f, 5.0f,
		0.0f, 0.0f, u0, v0, u1, v1);
	CHECK(u0 == Approx(0.0f));
	CHECK(v0 == Approx(0.0f));
	CHECK(u1 == Approx(1.0f));
	CHECK(v1 == Approx(1.0f));

	// pixelRectToUV and frameToUVRect share the inset: a grid cell expressed
	// as its pixel rect must land where frameToUVRect puts it. 2x2 grid on
	// 64x64 -> cell (1,1) is the pixel rect (32,32,32,32)
	float gu0, gv0, gu1, gv1;
	Orkige::SpriteComponent::frameToUVRect(3, 2, 2, 64.0f, 64.0f,
		gu0, gv0, gu1, gv1);
	Orkige::SpriteComponent::pixelRectToUV(32.0f, 32.0f, 32.0f, 32.0f,
		64.0f, 64.0f, u0, v0, u1, v1);
	CHECK(u0 == Approx(gu0));
	CHECK(v0 == Approx(gv0));
	CHECK(u1 == Approx(gu1));
	CHECK(v1 == Approx(gv1));
}

TEST_CASE("frameForElapsed advances, wraps and ends", "[sprite][flipbook]")
{
	bool ended = false;

	// 4-frame clip at 10 fps: 0.1s per frame
	CHECK(Orkige::SpriteAnimationComponent::frameForElapsed(0.0f, 10.0f, 4, true, ended) == 0);
	CHECK_FALSE(ended);
	CHECK(Orkige::SpriteAnimationComponent::frameForElapsed(0.05f, 10.0f, 4, true, ended) == 0);
	CHECK(Orkige::SpriteAnimationComponent::frameForElapsed(0.15f, 10.0f, 4, true, ended) == 1);
	CHECK(Orkige::SpriteAnimationComponent::frameForElapsed(0.35f, 10.0f, 4, true, ended) == 3);

	// looping wraps back around and never ends
	CHECK(Orkige::SpriteAnimationComponent::frameForElapsed(0.45f, 10.0f, 4, true, ended) == 0);
	CHECK_FALSE(ended);
	CHECK(Orkige::SpriteAnimationComponent::frameForElapsed(0.55f, 10.0f, 4, true, ended) == 1);
	CHECK_FALSE(ended);
	// deep into many loops still wraps cleanly
	CHECK(Orkige::SpriteAnimationComponent::frameForElapsed(100.15f, 10.0f, 4, true, ended) == 1);
	CHECK_FALSE(ended);

	// non-looping clamps at the last frame and reports ended past the end
	CHECK(Orkige::SpriteAnimationComponent::frameForElapsed(0.25f, 10.0f, 4, false, ended) == 2);
	CHECK_FALSE(ended);
	CHECK(Orkige::SpriteAnimationComponent::frameForElapsed(0.35f, 10.0f, 4, false, ended) == 3);
	CHECK_FALSE(ended);	// exactly on the last frame is not yet ended
	CHECK(Orkige::SpriteAnimationComponent::frameForElapsed(0.45f, 10.0f, 4, false, ended) == 3);
	CHECK(ended);		// past the last frame: clamp + end

	// a single-frame non-looping clip is done as soon as time passes
	CHECK(Orkige::SpriteAnimationComponent::frameForElapsed(0.0f, 10.0f, 1, false, ended) == 0);
	CHECK_FALSE(ended);
	CHECK(Orkige::SpriteAnimationComponent::frameForElapsed(0.5f, 10.0f, 1, false, ended) == 0);
	CHECK(ended);

	// a zero fps clip cannot advance (guarded, no divide-by-anything)
	CHECK(Orkige::SpriteAnimationComponent::frameForElapsed(1.0f, 0.0f, 4, true, ended) == 0);
}

TEST_CASE("SpriteComponent state round-trips through an XMLArchive", "[sprite]")
{
	Orkige::EngineTestEnvironment::get();
	TempFile file("orkige_sprite_roundtrip.xml");

	{
		TestSprite sprite;
		// detached setters only store state (no quad exists) - exactly the
		// path the scene serializer writes
		sprite.setSize(2.5f, 0.0f);
		sprite.setUVRect(0.25f, 0.0f, 0.75f, 0.5f);
		sprite.setTint(0.9f, 0.5f, 0.25f, 0.75f);
		sprite.setFlip(true, false);
		sprite.setZOrder(7);
		sprite.setSpriteVisible(false);
		// the texture name is normally set by loadSprite - the detached
		// serialization path writes whatever is stored
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startWriting(file.path));
		optr<Orkige::IArchive> archive = ar;
		sprite.save(archive);
		REQUIRE(ar->stopWriting());
	}

	{
		TestSprite loaded;
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startReading(file.path));
		optr<Orkige::IArchive> archive = ar;
		loaded.load(archive);
		REQUIRE(ar->stopReading());

		CHECK(loaded.getWidth() == Approx(2.5f));
		CHECK(loaded.getHeight() == Approx(0.0f));
		CHECK(loaded.getTint().r == Approx(0.9f));
		CHECK(loaded.getTint().g == Approx(0.5f));
		CHECK(loaded.getTint().b == Approx(0.25f));
		CHECK(loaded.getTint().a == Approx(0.75f));
		CHECK(loaded.getFlipX());
		CHECK_FALSE(loaded.getFlipY());
		CHECK(loaded.getZOrder() == 7);
		CHECK_FALSE(loaded.isSpriteVisible());
		// detached load: no renderer, so no quad - but the state is back
		CHECK_FALSE(loaded.hasSprite());
	}
}

TEST_CASE("SpriteComponent zOrder setter clamps", "[sprite]")
{
	Orkige::EngineTestEnvironment::get();
	Orkige::SpriteComponent sprite;
	sprite.setZOrder(10000);
	CHECK(sprite.getZOrder() == Orkige::SpriteComponent::ZORDER_MAX);
	sprite.setZOrder(-10000);
	CHECK(sprite.getZOrder() == Orkige::SpriteComponent::ZORDER_MIN);
}

TEST_CASE("SpriteAnimationComponent grid + clips round-trip through an XMLArchive", "[sprite][flipbook]")
{
	Orkige::EngineTestEnvironment::get();
	TempFile file("orkige_sprite_anim_roundtrip.xml");

	{
		TestSpriteAnim anim;
		anim.setGrid(4, 2);
		anim.addClip("idle", 0, 1, 1.0f, true);
		anim.addClip("run", 1, 6, 12.0f, true);
		anim.addClip("die", 7, 3, 8.0f, false);
		anim.setDefaultClip("idle");
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startWriting(file.path));
		optr<Orkige::IArchive> archive = ar;
		anim.save(archive);
		REQUIRE(ar->stopWriting());
	}

	{
		TestSpriteAnim loaded;
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startReading(file.path));
		optr<Orkige::IArchive> archive = ar;
		loaded.load(archive);
		REQUIRE(ar->stopReading());

		CHECK(loaded.getGridColumns() == 4);
		CHECK(loaded.getGridRows() == 2);
		CHECK(loaded.getDefaultClip() == "idle");
		CHECK(loaded.getClipCount() == 3);
		REQUIRE(loaded.hasClip("run"));
		Orkige::SpriteAnimationComponent::Clip const & run =
			loaded.getClips().at("run");
		CHECK(run.startFrame == 1);
		CHECK(run.frameCount == 6);
		CHECK(run.fps == Approx(12.0f));
		CHECK(run.loop);
		Orkige::SpriteAnimationComponent::Clip const & die =
			loaded.getClips().at("die");
		CHECK(die.startFrame == 7);
		CHECK_FALSE(die.loop);
	}
}

TEST_CASE("CameraComponent projection state round-trips through an XMLArchive", "[camera]")
{
	Orkige::EngineTestEnvironment::get();
	TempFile file("orkige_camera_roundtrip.xml");

	{
		TestCamera camera;
		CHECK(camera.getProjectionMode() ==
			Orkige::CameraComponent::PM_PERSPECTIVE);	// the default
		camera.setProjectionMode(Orkige::CameraComponent::PM_ORTHOGRAPHIC);
		camera.setOrthoSize(7.5f);
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startWriting(file.path));
		optr<Orkige::IArchive> archive = ar;
		camera.save(archive);
		REQUIRE(ar->stopWriting());
	}

	{
		TestCamera loaded;
		optr<Orkige::XMLArchive> ar = Orkige::onew(new Orkige::XMLArchive());
		REQUIRE(ar->startReading(file.path));
		optr<Orkige::IArchive> archive = ar;
		loaded.load(archive);
		REQUIRE(ar->stopReading());

		CHECK(loaded.getProjectionMode() ==
			Orkige::CameraComponent::PM_ORTHOGRAPHIC);
		CHECK(loaded.getOrthoSize() == Approx(7.5f));
	}
}

TEST_CASE("Tilt simulation math steers, clamps and normalizes", "[input][tilt]")
{
	// no steer: the angle stays (a tilted phone stays tilted)
	CHECK(Orkige::InputManager::advanceTiltAngle(0.3f, false, false, 1.0f) ==
		Approx(0.3f));
	// both keys cancel out
	CHECK(Orkige::InputManager::advanceTiltAngle(0.3f, true, true, 1.0f) ==
		Approx(0.3f));
	// steering right increases toward +X, left decreases
	CHECK(Orkige::InputManager::advanceTiltAngle(0.0f, false, true, 0.5f) ==
		Approx(0.5f * Orkige::InputManager::TILT_SIM_RATE));
	CHECK(Orkige::InputManager::advanceTiltAngle(0.0f, true, false, 0.5f) ==
		Approx(-0.5f * Orkige::InputManager::TILT_SIM_RATE));
	// holding a key forever saturates at the clamp
	float angle = 0.0f;
	for (int frame = 0; frame < 600; ++frame)
	{
		angle = Orkige::InputManager::advanceTiltAngle(angle, false, true,
			1.0f / 60.0f);
	}
	CHECK(angle == Approx(Orkige::InputManager::TILT_SIM_MAX_ANGLE));

	// the tilt vector: upright = straight-down gravity, unit length always
	const Ogre::Vector3 upright = Orkige::InputManager::tiltVectorFromAngle(0.0f);
	CHECK(upright.x == Approx(0.0f).margin(1e-6));
	CHECK(upright.y == Approx(-1.0f));
	CHECK(upright.z == Approx(0.0f).margin(1e-6));
	const Ogre::Vector3 right = Orkige::InputManager::tiltVectorFromAngle(0.6f);
	CHECK(right.x > 0.0f);		// positive angle tilts gravity toward +X
	CHECK(right.y < 0.0f);		// but never above the horizon at sane angles
	CHECK(right.length() == Approx(1.0f));
	const Ogre::Vector3 left = Orkige::InputManager::tiltVectorFromAngle(-0.6f);
	CHECK(left.x == Approx(-right.x));
	CHECK(left.y == Approx(right.y));
}
