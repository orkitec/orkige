/**************************************************************
	created:	2026/07/16 at 06:00
	filename: 	PlayerSelfChecks.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

// Every ORKIGE_* environment-hook verification the player carries - the
// scripted end-to-end checks the ctest suite drives (see each block's own
// comment). State lives on the struct (PlayerSelfChecks.h) so it persists
// across frames; the per-frame observation helpers are lambdas rebuilt each
// perFrame call (they are stateless closures over the frame's aliases). The
// bodies are the historical main.cpp blocks, moved verbatim - the exact
// SDL_Log lines are what the ctest drivers grep.
#include "PlayerSelfChecks.h"
#include "PlayerContext.h"

#include <SDL3/SDL.h>
#include <core_debug/Profile.h>
#include <core_debug/MemoryManager.h>
#include <core_event/GlobalEventManager.h>
#include <core_game/GameObjectManager.h>
#include <core_game/GameObject.h>
#include <core_game/SceneSerializer.h>
#include <core_game/LevelSequence.h>
#include <core_script/ScriptRuntime.h>
#include <core_script/ScriptEventBus.h>
#include <core_tween/EaseLibrary.h>
#include <core_util/PlatformUtil.h>
#include <core_util/SoftBodyDeform.h>
#include <core_util/VectorTessellator.h>
#include <core_util/VectorShapeAsset.h>
#include <core_util/VectorAnimAsset.h>
#include <core_util/VectorAnimEval.h>
#include <core_debug/CVarManager.h>
#include <engine_gocomponent/TransformComponent.h>
#include <engine_gocomponent/SpriteBatcher.h>
#include <engine_gocomponent/CameraComponent.h>
#include <engine_gocomponent/ModelComponent.h>
#include <engine_gocomponent/AnimationComponent.h>
#include <engine_gocomponent/BoneAttachComponent.h>
#include <engine_gocomponent/SpriteComponent.h>
#include <engine_gocomponent/RigidBodyComponent.h>
#include <engine_gocomponent/ScriptComponent.h>
#include <engine_gocomponent/ParticleComponent.h>
#include <engine_gocomponent/VectorShapeComponent.h>
#include <engine_gocomponent/VectorAnimationComponent.h>
#include <engine_gui/GuiManager.h>
#include <engine_render/RenderSystem.h>
#include <engine_render/RenderCamera.h>
#include <engine_render/MeshInstance.h>
#include <engine_runtime/AppHost.h>
#include <engine_sound/SoundManager.h>
#include <engine_sound/MusicStream.h>
#include <engine_util/StringUtil.h>
#include <core_debugnet/Json.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// the engine's shared-ownership alias, used throughout this TU
using Orkige::optr;
using Orkige::woptr;
// the synthetic-input pushers the selfchecks script with are the shared
// engine_runtime/AppHost.h helpers
using Orkige::pushKeyEvent;
using Orkige::pushMouseMove;
using Orkige::pushMouseButton;

//---------------------------------------------------------
void PlayerSelfChecks::readEnvironment(PlayerContext& context)
{
	unsigned long& frameLimit = context.frameLimit;
	bool& automatedRun = context.automatedRun;

	// automation hooks (read before engine setup - they gate the vsync
	// and frame-pacing decisions): ORKIGE_DEMO_FRAMES frame-limits the
	// run, ORKIGE_JUMPER_LUA_SELFCHECK runs the scripted verification
	frameLimit = 0;
	if (const char* demoFrames = std::getenv("ORKIGE_DEMO_FRAMES"))
	{
		frameLimit = std::strtoul(demoFrames, nullptr, 10);
	}
	jumperLuaCheck =
		(std::getenv("ORKIGE_JUMPER_LUA_SELFCHECK") != nullptr);
	// ORKIGE_ROLLER_SELFCHECK verifies the 2D tier end to end against
	// projects/roller; ORKIGE_ROLLER_SCREENSHOT_DIR (optional) additionally
	// dumps roller_play.png / roller_move_mode.png there
	rollerCheck =
		(std::getenv("ORKIGE_ROLLER_SELFCHECK") != nullptr);
	const char* rollerShotDirEnv =
		std::getenv("ORKIGE_ROLLER_SCREENSHOT_DIR");
	rollerShotDir =
		rollerShotDirEnv ? rollerShotDirEnv : "";
	// ORKIGE_SOFTBODY_SELFCHECK verifies soft, deformable organic shapes end
	// to end against projects/vectorshapes (scenes/softbody.oscene): a blob
	// with a RigidBody falls, SQUASHES on landing and wobbles (the dynamic
	// deform upload), then returns to rest; a second blob is MORPHED via a Lua
	// self.shape:playMorph call. It also logs the measured per-frame deform
	// cost (the mobile-viability budget number).
	softbodyCheck =
		(std::getenv("ORKIGE_SOFTBODY_SELFCHECK") != nullptr);
	// ORKIGE_VECTORANIM_SELFCHECK verifies vector (Lottie) animation rigs end
	// to end against projects/vectorshapes (scenes/vectoranim.oscene): the
	// hero's `idle` clip advances (the pose changes as frames tick), a
	// scripted crossFade switches it to the one-shot `hop`, and the hop's
	// completion fires the ended event into the script (shared.heroanim). It
	// also logs the measured per-frame evaluate+tessellate cost per character.
	vectorAnimCheck =
		(std::getenv("ORKIGE_VECTORANIM_SELFCHECK") != nullptr);
	// ORKIGE_CUTOUT_SELFCHECK verifies TEXTURED cutout parts on a vector rig
	// end to end against projects/vectorshapes (scenes/cutout.oscene): the
	// mixed flat+textured rig builds its per-texture draw runs, the wave
	// clip moves the pose through the dynamic section path, and (with
	// ORKIGE_CUTOUT_SCREENSHOT_DIR set) a mid-clip screenshot lands for the
	// driver's pixel probes.
	cutoutCheck =
		(std::getenv("ORKIGE_CUTOUT_SELFCHECK") != nullptr);
	const char* cutoutShotDirEnv =
		std::getenv("ORKIGE_CUTOUT_SCREENSHOT_DIR");
	cutoutShotDir = cutoutShotDirEnv ? cutoutShotDirEnv : "";
	// ORKIGE_CHARACTER_RIG_SELFCHECK verifies 3D SKELETAL character animation
	// end to end against tests/projects/character (a generated skinned
	// mannequin): the walk clip moves bone-driven bounds, a crossfade blends to
	// idle. Runs on both flavors - the drift alarm between the two importer
	// roads (Docs/character-animation.md); skips honestly on a flavor that
	// imports glTF statically (no skeleton, no clips).
	characterRigCheck =
		(std::getenv("ORKIGE_CHARACTER_RIG_SELFCHECK") != nullptr);
	// ORKIGE_ROLLER_PROGRESSION_SELFCHECK verifies the level sequence +
	// deferred scene switch + progression save end to end against
	// projects/roller: solve level 1 (the proven tile-slide + roll), assert
	// the runtime SWITCHED to level 2 (shared.roller.levelIndex incremented,
	// the progression file written), then solve the straight-shot level 2
	rollerProgressionCheck =
		(std::getenv("ORKIGE_ROLLER_PROGRESSION_SELFCHECK") != nullptr);
	// ORKIGE_ASSETID_SELFCHECK=<expected texture> verifies asset-id
	// rename survival end to end (run with --project
	// tests/projects/asset_rename): the scene's "Sprite" object carries a
	// STALE texture name plus the sidecar asset id of the real file - the
	// sprite must come up under the CURRENT name, resolved via the
	// project's AssetDatabase (core_project/AssetDatabase.h)
	const char* assetIdCheckEnv =
		std::getenv("ORKIGE_ASSETID_SELFCHECK");
	assetIdCheckTexture =
		assetIdCheckEnv ? assetIdCheckEnv : "";
	// ORKIGE_COOKED_SELFCHECK=<expected texture> verifies a block-compressed
	// export payload end to end (run against a COOKED copy of
	// tests/projects/asset_rename - see integration_driver/
	// run_cooked_textures_test.py): the "Sprite" object must come up showing
	// the expected texture (a .dds/.oitd the cook wrote in place of the
	// source PNG) and the render backend must measure real texel dimensions
	// for it - the GPU actually accepted the compressed container
	const char* cookedCheckEnv =
		std::getenv("ORKIGE_COOKED_SELFCHECK");
	cookedCheckTexture =
		cookedCheckEnv ? cookedCheckEnv : "";
	// ORKIGE_TWEEN_SELFCHECK verifies the tween system end to end against
	// tests/projects/tween (run with --project tests/projects/tween)
	tweenCheck =
		(std::getenv("ORKIGE_TWEEN_SELFCHECK") != nullptr);
	// ORKIGE_HOTRELOAD_SELFCHECK verifies Lua hot-reload end to
	// end against tests/projects/hotreload: overwrite the running script on
	// disk, drive ScriptComponent::hotReload() (the player-directed swap),
	// and assert (a) the behavior changed AND (b) an engine-side value
	// persisted; then a broken-file variant must keep the OLD instance
	// ticking with a non-fatal error. The committed script is restored at
	// the end (the selfcheck rewrites it in place).
	hotreloadCheck =
		(std::getenv("ORKIGE_HOTRELOAD_SELFCHECK") != nullptr);
	// ORKIGE_SCRIPTPROP_SELFCHECK verifies Lua script EXPORTED properties
	// end to end against tests/projects/scriptprop: the
	// scene bakes the "Mover" object's exported moveSpeed at a non-default
	// value (5); the selfcheck asserts the running script SAW the injected
	// value (init published it) and BEHAVES with it (moves at that speed),
	// then flips it live over the debug-protocol setter (moveSpeed -> 0
	// stops the motion) and re-saves+reloads the scene to prove the value
	// round-trips per-instance.
	scriptPropCheck =
		(std::getenv("ORKIGE_SCRIPTPROP_SELFCHECK") != nullptr);
	// ORKIGE_INTEGRATION_CONTACT_SELFCHECK verifies a CROSS-FEATURE chain
	// against tests/projects/integration (scenes/contact.oscene): a scripted
	// ball discovers the goal by TAG (world.findByTag), an injected named
	// INPUT ACTION ("jump") turns gravity on, the PHYSICS drop overlaps the
	// goal SENSOR and the CONTACT EVENT (onContactBegin) fires - tags +
	// input actions + physics + contact events cooperating in one run.
	integrationContactCheck =
		(std::getenv("ORKIGE_INTEGRATION_CONTACT_SELFCHECK") != nullptr);
	// ORKIGE_INTEGRATION_LEVELSWITCH_SELFCHECK verifies a DEFERRED level
	// switch (scenes/levelA -> levelB) fired WHILE a TWEEN and a live
	// PARTICLE emitter run: the switch must tear the running tween + emitter
	// down cleanly (GameObjectManager::clear), the new level must tick and
	// the shared table must survive - level system + tweens + particles +
	// script lifecycle + teardown in one run.
	integrationLevelCheck =
		(std::getenv("ORKIGE_INTEGRATION_LEVELSWITCH_SELFCHECK") != nullptr);
	// ORKIGE_BREADCRUMB_SELFCHECK verifies the crash breadcrumb trail against
	// tests/projects/breadcrumb: a ScriptComponent raises a Lua error at init;
	// the player must record it as a "script_error" line in breadcrumbs.jsonl
	// (flushed to disk) while the game keeps running. Isolate the trail with
	// ORKIGE_BREADCRUMB_DIR.
	breadcrumbCheck =
		(std::getenv("ORKIGE_BREADCRUMB_SELFCHECK") != nullptr);
	// ORKIGE_FADE_SELFCHECK verifies the full-screen fade transition against
	// tests/projects/fade: drive a screen.loadScene-style fade (out -> switch
	// sceneA to sceneB while opaque -> in) and assert the overlay alpha climbs
	// to opacity, the deferred scene switch applies WHILE the screen is
	// covered, and the fade clears afterwards. Exercises the real render loop
	// on BOTH flavors (see the ORKIGE_FADE_SELFCHECK block in this file).
	fadeCheck =
		(std::getenv("ORKIGE_FADE_SELFCHECK") != nullptr);
	// ORKIGE_LIFECYCLE_SELFCHECK verifies the mobile app-lifecycle contract
	// end to end against tests/projects/lifecycle, driving the REAL player
	// wiring with SYNTHETIC SDL lifecycle events (SDL_PushEvent, the same
	// path a device's background/foreground takes): background the app and
	// assert the sim gate engaged, the save store was FLUSHED (its dirty flag
	// cleared, the value the script wrote from onAppPause persisted) and a
	// "background" breadcrumb landed; then foreground it and assert the sim
	// resumed, onAppResume fired and a "foreground" breadcrumb landed. See
	// the ORKIGE_LIFECYCLE_SELFCHECK block near the loop below.
	lifecycleCheck =
		(std::getenv("ORKIGE_LIFECYCLE_SELFCHECK") != nullptr);
	// ORKIGE_RESIZE_SELFCHECK verifies the window-resize plumbing (the path
	// a device rotation takes): request a different window size through the
	// host window and require the render system to report the new drawable
	// size with the window camera's aspect re-derived to match (see the
	// ORKIGE_RESIZE_SELFCHECK block at the loop bottom). Runs against any
	// scene; desktop-only in ctest (a mobile window ignores size requests).
	resizeCheck =
		(std::getenv("ORKIGE_RESIZE_SELFCHECK") != nullptr);
	// ORKIGE_PERF_SELFCHECK=1: run a real project for ~70 frames and
	// assert the performance instruments produce a truthful readback -
	// the profiler snapshot carries the canonical tick phases at depth 0,
	// the frame boundary folded (frames sampled, frame time measured) and
	// the allocation counters ran. The MEASURED numbers are logged as the
	// deliverable; nothing gates on wall-clock (CI machines are slower).
	perfCheck =
		(std::getenv("ORKIGE_PERF_SELFCHECK") != nullptr);
	benchmarkCheck =
		(std::getenv("ORKIGE_BENCHMARK_SELFCHECK") != nullptr);
	// ORKIGE_STATICMOVE_SELFCHECK verifies the static-mobility contract
	// against projects/benchmark scenes/fixture_static.oscene (@see the
	// header's block comment): a runtime move of a static object must warn
	// once AND land correctly through the backend repair path.
	staticMoveCheck =
		(std::getenv("ORKIGE_STATICMOVE_SELFCHECK") != nullptr);
	// ORKIGE_SPRITEBATCH_SELFCHECK verifies sprite-run batching against
	// projects/benchmark scenes/fixture_sprites.oscene (@see the header's
	// block comment): exact run structure, the live r.spriteBatching escape
	// hatch, and a moved member re-uploading its run. Counts are read as
	// mode-over-window (driver-dependent absolute numbers; the ctest driver
	// asserts the driver-independent run count/delta contract).
	spriteBatchCheck =
		(std::getenv("ORKIGE_SPRITEBATCH_SELFCHECK") != nullptr);
	// ORKIGE_PAK_SELFCHECK=<pakfile>: the pak-mount contract end to end (the
	// classic BigZip acceptance test reborn, flavor-neutral). The player mounts
	// the zip's sub-tree ORKIGE_PAK_MOUNTPOINT (default "game/", the APK
	// "assets/" case), loads its scene THROUGH the resource system (no fopen),
	// resolves a texture from it and streams an OGG from it - all resolving like
	// loose files. See gameplaySynchronousChecks (the whole run is synchronous).
	if (const char* pak = std::getenv("ORKIGE_PAK_SELFCHECK"))
	{
		pakCheck = true;
		pakPath = pak;
		pakMountPoint = "game/";
		if (const char* mount = std::getenv("ORKIGE_PAK_MOUNTPOINT"))
		{
			pakMountPoint = mount;
		}
	}
	// ORKIGE_PAK_SCRIPT_SELFCHECK=<pakfile>: the archive-in-place SCRIPT read.
	// A path-bound ScriptComponent whose "scripts/pak_script.lua" lives ONLY
	// inside the mounted pak (no loose file, no --project) must LOAD AND RUN -
	// proving Lua scripts read through the resource system from an archive
	// instead of via fopen (the extraction the Android stored mode still does).
	if (const char* pakScript = std::getenv("ORKIGE_PAK_SCRIPT_SELFCHECK"))
	{
		pakScriptCheck = true;
		pakScriptPath = pakScript;
	}
	// automated runs (ctest, the editor's play-mode tests - they inherit
	// ORKIGE_DEMO_FRAMES from the editor's environment) render as fast as
	// the machine allows; a HUMAN run gets vsync so games neither spin
	// uncapped nor tear
	automatedRun = jumperLuaCheck || rollerCheck ||
		rollerProgressionCheck || tweenCheck ||
		hotreloadCheck || scriptPropCheck ||
		integrationContactCheck || integrationLevelCheck ||
		breadcrumbCheck || fadeCheck || lifecycleCheck || resizeCheck ||
		softbodyCheck || perfCheck || benchmarkCheck || vectorAnimCheck ||
		characterRigCheck || staticMoveCheck || spriteBatchCheck ||
		!assetIdCheckTexture.empty() || !cookedCheckTexture.empty() ||
		frameLimit != 0;
}

//---------------------------------------------------------
std::optional<int> PlayerSelfChecks::earlySynchronousChecks(PlayerContext& context)
{
	Orkige::InputManager& inputManager = *context.inputManager;
	Orkige::HapticManager& hapticManager = *context.hapticManager;

	// ORKIGE_TILT_CALIB_SELFCHECK: the calibration wiring end to end,
	// synchronous (no render loop needed) - set a simulated tilt pose,
	// calibrate so it reads as neutral (0,-1,0), steer further to confirm it
	// deflects again, then round-trip the auto-written save file. Exercises
	// what the pure-math unit test cannot: getTilt applying the offset and
	// setCalibrationSaveFile/save/load persistence. Runs against any scene.
	if (std::getenv("ORKIGE_TILT_CALIB_SELFCHECK") != nullptr)
	{
		bool ok = true;
		std::string detail;
		inputManager.clearTiltCalibration();
		inputManager.setTiltAngle(0.6f);
		const Orkige::Vec3 rawPose = inputManager.getTilt();
		if (std::abs(rawPose.x) < 0.3f)
		{
			ok = false;
			detail = "raw pose not tilted";
		}
		inputManager.calibrateTilt();
		const Orkige::Vec3 neutral = inputManager.getTilt();
		if (ok && (std::abs(neutral.x) > 0.02f || neutral.y > -0.98f))
		{
			ok = false;
			detail = "calibrated pose did not read as neutral";
		}
		inputManager.setTiltAngle(1.0f);
		const Orkige::Vec3 deflected = inputManager.getTilt();
		if (ok && std::abs(deflected.x) < 0.3f)
		{
			ok = false;
			detail = "steering past the neutral pose did not deflect";
		}
		// persistence: calibrateTilt above auto-wrote the file; drop the
		// in-memory value and reload it
		const float savedAngle = inputManager.getTiltCalibration();
		inputManager.setTiltCalibration(0.0f);
		if (ok && (!inputManager.loadCalibration() ||
			std::abs(inputManager.getTiltCalibration() - savedAngle) > 1.0e-4f))
		{
			ok = false;
			detail = "calibration save/load did not round-trip";
		}
		SDL_Log("orkige_player: TILT CALIB SELFCHECK %s%s%s",
			ok ? "PASSED" : "FAILED", ok ? "" : " - ", detail.c_str());
		return ok ? 0 : 1;
	}
	// ORKIGE_HAPTIC_SELFCHECK: the honest desktop no-op + API shape (device
	// vibration itself cannot be asserted headlessly). Asserts isAvailable()
	// is false on desktop, the play/pattern calls do not throw and no-op
	// cleanly (incl. while muted), and the pure name->pattern mapping defaults
	// an unknown name. Synchronous - no render loop needed.
	if (std::getenv("ORKIGE_HAPTIC_SELFCHECK") != nullptr)
	{
		bool ok = true;
		std::string detail;
		if (hapticManager.isAvailable())
		{
			ok = false;
			detail = "isAvailable() reported true on desktop";
		}
		// these must all return cleanly (honest no-op on desktop)
		hapticManager.play(0.6f, 120);
		hapticManager.playPatternByName("success");
		hapticManager.playPatternByName("unknown-name-defaults-to-medium");
		hapticManager.setEnabled(false);
		hapticManager.play(1.0f, 50);
		hapticManager.setEnabled(true);
		if (ok && Orkige::HapticManager::patternFromName("nope") !=
			Orkige::HapticManager::Pattern::Medium)
		{
			ok = false;
			detail = "unknown pattern name did not default to Medium";
		}
		SDL_Log("orkige_player: HAPTIC SELFCHECK %s%s%s",
			ok ? "PASSED" : "FAILED", ok ? "" : " - ", detail.c_str());
		return ok ? 0 : 1;
	}
	return std::nullopt;
}

//---------------------------------------------------------
std::optional<int> PlayerSelfChecks::afterSceneLoad(PlayerContext& context)
{
	Orkige::GameObjectManager& gameObjectManager = *context.gameObjectManagerPtr;

	// --- ORKIGE_ASSETID_SELFCHECK: prove the rename survived - the
	// scene's stale texture name must have been replaced by the expected
	// current one (resolved through the sidecar id) and the sprite must
	// actually be showing it
	if (!assetIdCheckTexture.empty())
	{
		optr<Orkige::GameObject> spriteObject =
			gameObjectManager.getGameObject("Sprite").lock();
		Orkige::SpriteComponent* sprite = (spriteObject &&
			spriteObject->hasComponent<Orkige::SpriteComponent>())
			? spriteObject->getComponentPtr<Orkige::SpriteComponent>()
			: nullptr;
		if (!sprite || !sprite->hasSprite() ||
			sprite->getTextureName() != assetIdCheckTexture ||
			sprite->getTextureAssetId().empty())
		{
			SDL_Log("orkige_player: ASSETID SELFCHECK FAILED - "
				"texture='%s' assetId='%s' hasSprite=%d (expected the "
				"stale scene reference to resolve to '%s' via its "
				"sidecar id)",
				sprite ? sprite->getTextureName().c_str() : "<no sprite "
				"component>",
				sprite ? sprite->getTextureAssetId().c_str() : "",
				sprite ? (sprite->hasSprite() ? 1 : 0) : 0,
				assetIdCheckTexture.c_str());
			return 1;
		}
		SDL_Log("orkige_player: ASSETID SELFCHECK PASSED - stale "
			"reference resolved to '%s' (id %s)",
			sprite->getTextureName().c_str(),
			sprite->getTextureAssetId().c_str());
	}

	// --- ORKIGE_COOKED_SELFCHECK: prove a block-compressed export payload
	// renders - the sprite must be up under the expected (cooked or raw)
	// texture name AND the render backend must measure real texel
	// dimensions for it, which means the GPU accepted the compressed
	// container (.dds/.oitd) the export cook wrote
	if (!cookedCheckTexture.empty())
	{
		optr<Orkige::GameObject> spriteObject =
			gameObjectManager.getGameObject("Sprite").lock();
		Orkige::SpriteComponent* sprite = (spriteObject &&
			spriteObject->hasComponent<Orkige::SpriteComponent>())
			? spriteObject->getComponentPtr<Orkige::SpriteComponent>()
			: nullptr;
		unsigned int texW = 0;
		unsigned int texH = 0;
		const bool measured = sprite && Orkige::RenderSystem::get() &&
			Orkige::RenderSystem::get()->getTextureSize(
				sprite->getTextureName(), texW, texH);
		if (!sprite || !sprite->hasSprite() ||
			sprite->getTextureName() != cookedCheckTexture ||
			!measured || texW == 0 || texH == 0)
		{
			SDL_Log("orkige_player: COOKED SELFCHECK FAILED - "
				"texture='%s' hasSprite=%d measured=%ux%u (expected the "
				"cooked payload to render '%s' from its compressed "
				"container)",
				sprite ? sprite->getTextureName().c_str() : "<no sprite "
				"component>",
				sprite ? (sprite->hasSprite() ? 1 : 0) : 0, texW, texH,
				cookedCheckTexture.c_str());
			return 1;
		}
		SDL_Log("orkige_player: COOKED SELFCHECK PASSED - '%s' renders at "
			"%ux%u from the cooked payload",
			sprite->getTextureName().c_str(), texW, texH);
	}
	return std::nullopt;
}

//---------------------------------------------------------
std::optional<int> PlayerSelfChecks::gameplaySynchronousChecks(PlayerContext& context)
{
	Orkige::AppHost& host = context.host;
	Orkige::GameObjectManager& gameObjectManager = *context.gameObjectManagerPtr;
	Orkige::optr<Orkige::RenderNode>& cameraNode = context.cameraNode;
	Orkige::TweenManager& tweenManager = *context.tweenManager;
	Orkige::TimerManager& timerManager = *context.timerManager;
	Orkige::GameState& gameState = *context.gameState;
	Orkige::ScreenShake& screenShake = *context.screenShake;
	Orkige::TimeControl& timeControl = *context.timeControl;
	Orkige::SaveStore& saveStore = *context.saveStore;

	// ORKIGE_PAK_SELFCHECK: the pak-mount contract, end to end and synchronous
	// (the classic BigZip acceptance test reborn, flavor-neutral). The pak was
	// mounted in the boot resource callback (main.cpp) and its scene was loaded
	// THROUGH the resource system (SceneSerializer::loadSceneFromString) at the
	// scene-load point - so reaching here with a populated world already proves
	// the scene resolved from the mounted zip. This leg adds the texture + OGG
	// legs (resource resolution + seek-heavy streaming) and reports the verdict.
	if (this->pakCheck)
	{
		bool ok = true;
		std::string detail;
		Orkige::RenderSystem* render = context.render;

		// (1) the scene loaded from the pak (loaded before this hook runs)
		const std::size_t objectCount =
			gameObjectManager.getGameObjects().size();
		if (objectCount == 0)
		{
			ok = false;
			detail += " scene-from-pak-empty";
		}
		else
		{
			SDL_Log("orkige_pak_selfcheck: scene loaded from mounted pak "
				"(%zu GameObjects)", objectCount);
		}

		// (2) a texture resolves from the pak like a loose file (the sprite in
		// the scene already loaded it; this is the explicit probe)
		unsigned int texW = 0;
		unsigned int texH = 0;
		if (render != nullptr &&
			render->getTextureSize("pak_tex.png", texW, texH) &&
			texW > 0 && texH > 0)
		{
			SDL_Log("orkige_pak_selfcheck: texture 'pak_tex.png' resolved from "
				"pak (%ux%u)", texW, texH);
		}
		else
		{
			ok = false;
			detail += " texture-from-pak-missing";
		}

		// (3) the OGG streams from the pak: SoundManager reads the bytes THROUGH
		// the resource system (readResourceBytes) off the mounted zip, decodes
		// them and primes the ring - the seek-heavy path (stb_vorbis seeks
		// within the mounted bytes; priming a short loop already wraps via
		// seekStart). A silent build (OpenAL down) still proves the bytes were
		// read + decoded (isOpen); the audible stop/replay is skipped honestly.
		Orkige::SoundManager& soundManager = *context.soundManager;
		const bool played = soundManager.playMusic("bgm", "music.ogg", true);
		Orkige::MusicStreamPtr track = soundManager.getMusic("bgm");
		if (!track || !track->isOpen())
		{
			ok = false;
			detail += " ogg-from-pak-not-decoded";
		}
		else
		{
			SDL_Log("orkige_pak_selfcheck: OGG 'music.ogg' streamed from pak "
				"(duration %.3fs, primed=%d, played=%d)",
				track->getDuration(), track->isPrimed() ? 1 : 0,
				played ? 1 : 0);
			if (track->getDuration() <= 0.0f)
			{
				ok = false;
				detail += " ogg-zero-duration";
			}
			// exercise the seek path explicitly: stop rewinds the decoder
			// (MusicDecode::seekStart over the mounted bytes) and a fresh play
			// re-primes from the top - the seek-heavy streaming proof
			if (track->isPrimed())
			{
				soundManager.update(0.0f);
				track->stop();
				track->play();
			}
		}

		if (ok)
		{
			SDL_Log("orkige_pak_selfcheck: PASS - scene, texture and streamed "
				"OGG all resolved from the mounted pak on this flavor");
			return 0;
		}
		SDL_Log("orkige_pak_selfcheck: FAILED -%s", detail.c_str());
		return 1;
	}

	// ORKIGE_PAK_SCRIPT_SELFCHECK: the archive-in-place SCRIPT read, end to end
	// and synchronous. The script "scripts/pak_script.lua" lives ONLY inside the
	// pak mounted at boot - there is no --project and no loose file, so its
	// on-disk resolution MUST miss. A path-bound ScriptComponent that loads AND
	// runs from that name therefore proves Lua scripts read through the resource
	// system (RenderSystem::readResourceText, via the injected ResourceReader)
	// off a mounted archive, no fopen - the read that removes the Android
	// fopen-tree extraction. Flavor-neutral (mountPak + the reader are).
	if (this->pakScriptCheck)
	{
		Orkige::ScriptRuntime& scripts = host.getScriptRuntime();
		if (!Orkige::ScriptRuntime::available())
		{
			// noscript build: scripting is honestly off - nothing to prove here
			SDL_Log("orkige_pak_script_selfcheck: scripting disabled - "
				"skipping (the in-place read is a Lua-only concern)");
			return 0;
		}
		bool ok = true;
		std::string detail;

		// (0) the DISK path must MISS: no project root, no cwd file for this
		// name - so a load can only come from the mounted pak (the in-place proof)
		if (!scripts.resolveScriptPath("scripts/pak_script.lua").empty())
		{
			ok = false;
			detail += " loose-file-present(no-in-place-proof)";
		}

		// a global table the pak script writes into (its own sandbox writes fall
		// through to this real global - the ScriptRuntime `shared`-table idiom)
		scripts.ensureGlobalTable("pak_marker");

		Orkige::optr<Orkige::GameObject> obj =
			gameObjectManager.createGameObject("pak_script_obj").lock();
		Orkige::ScriptComponent* script = nullptr;
		if (obj && obj->addComponent<Orkige::ScriptComponent>())
		{
			script = obj->getComponentPtr<Orkige::ScriptComponent>();
			script->setScriptFile("scripts/pak_script.lua");
		}
		if (!script)
		{
			SDL_Log("orkige_pak_script_selfcheck: FAILED - could not attach a "
				"ScriptComponent");
			return 1;
		}

		// tick the world: the ScriptComponent loads lazily on the first update,
		// reading its source THROUGH the reader off the mounted pak, running the
		// top-level chunk + init, then update on the second tick
		gameObjectManager.update(1.0f / 60.0f);
		gameObjectManager.update(1.0f / 60.0f);

		if (script->hasScriptError())
		{
			ok = false;
			detail += " script-error:" + script->getScriptError();
		}
		if (!script->isScriptStarted())
		{
			ok = false;
			detail += " not-started";
		}
		if (!scripts.getBool({ "pak_marker", "loaded" }, false))
		{
			ok = false;
			detail += " top-level-chunk-not-run";
		}
		if (!scripts.getBool({ "pak_marker", "inited" }, false))
		{
			ok = false;
			detail += " init-not-run";
		}
		if (scripts.getNumber({ "pak_marker", "updates" }, 0.0) < 1.0)
		{
			ok = false;
			detail += " update-not-run";
		}

		if (ok)
		{
			SDL_Log("orkige_pak_script_selfcheck: PASS - path-bound "
				"ScriptComponent 'scripts/pak_script.lua' loaded and ran from "
				"the mounted pak with NO loose file on disk (this flavor)");
			return 0;
		}
		SDL_Log("orkige_pak_script_selfcheck: FAILED -%s", detail.c_str());
		return 1;
	}

	// ORKIGE_GAMESUPPORT_SELFCHECK: the game-support pack verified end to end,
	// synchronous (no render loop needed) against the live player wiring:
	//  (1) SAVE - set typed values through the Lua `save` table (proving the
	//      binding + the player's SaveStore file wiring), flush to disk, then
	//      re-load into a FRESH store (a "restart") and read them back;
	//  (2) SCREEN SHAKE - drive ScreenShake against the live window-camera rig
	//      node: it must deflect mid-shake and restore the node to EXACTLY its
	//      rest pose when the shake runs out;
	//  (3) TIME SCALE - feed the TweenManager the loop's delta*timeScale and
	//      assert timeScale 0.5 lands a tween at HALF the progress that
	//      timeScale 1.0 reaches over the same updates.
	// The ortho aspect-fit policy is proven headlessly by the CameraFit unit
	// tests (visible world rect across 4:3..21:9).
	if (std::getenv("ORKIGE_GAMESUPPORT_SELFCHECK") != nullptr)
	{
		bool ok = true;
		std::string detail;

		// (1) save round-trip through the Lua binding + a fresh-store reload
		std::string saveDir;
		if (const char* d = std::getenv("ORKIGE_PROGRESS_DIR"))
		{
			saveDir = d;
		}
		else
		{
			saveDir = std::filesystem::temp_directory_path().string();
		}
		if (!saveDir.empty() && saveDir.back() != '/')
		{
			saveDir += '/';
		}
		std::error_code saveDirErr;
		std::filesystem::create_directories(saveDir, saveDirErr);
		const std::string saveFile = saveDir + "gamesupport_save.osave";
		std::filesystem::remove(saveFile, saveDirErr);
		saveStore.setSaveFile(saveFile);
		Orkige::ScriptComponent::ensureScriptApi();
		Orkige::ScriptRuntime::Result saveResult =
			Orkige::ScriptRuntime::getSingleton().runString(
				"save.set('coins', 42)\n"
				"save.set('player.name', 'Ada')\n"
				"save.set('unlocked', true)\n"
				"return save.flush()");
		if (!saveResult.success)
		{
			ok = false;
			detail = "save script error: " + saveResult.error;
		}
		if (ok)
		{
			// model the next launch: wipe the in-memory store and re-load
			// from disk - the values must have reached the file via flush()
			saveStore.clear();
			if (!saveStore.load() ||
				saveStore.getNumber("coins", 0.0) != 42.0 ||
				saveStore.getString("player.name", "") != "Ada" ||
				saveStore.getBool("unlocked", false) != true)
			{
				ok = false;
				detail = "save did not round-trip through a reload from disk";
			}
		}

		// (2) screen shake deflects then restores the camera EXACTLY
		if (ok)
		{
			const Orkige::Vec3 rest = cameraNode->getPosition();
			screenShake.shake(1.0f, 0.2f, 30.0f);
			screenShake.update(0.05f);	// one frame into the shake
			const Orkige::Vec3 shaken = cameraNode->getPosition();
			if ((shaken - rest).length() < 1.0e-4f)
			{
				ok = false;
				detail = "screen shake did not deflect the camera";
			}
			// run well past the 0.2s duration, then it must be back at rest
			for (int i = 0; ok && i < 8; ++i)
			{
				screenShake.update(0.05f);
			}
			const Orkige::Vec3 after = cameraNode->getPosition();
			if (ok && ((after - rest).length() > 1.0e-5f ||
				screenShake.isShaking()))
			{
				ok = false;
				detail = "screen shake did not restore the camera to rest";
			}
		}

		// (3) time scale halves a tween's progress over the same updates
		if (ok)
		{
			auto runTween = [&](float scale) -> float
			{
				timeControl.setTimeScale(scale);
				float value = 0.0f;
				const float from = 0.0f;
				const float to = 1.0f;
				tweenManager.startTween(&from, &to, 1, 1.0f,
					&Orkige::Ease::linear,
					[&value](float const* values, int) -> bool
					{
						value = values[0];
						return true;
					},
					Orkige::TweenManager::CompleteFunction(), 0.0f,
					Orkige::StringUtil::BLANK);
				// ten updates of the loop's delta*timeScale (base dt 0.1)
				for (int i = 0; i < 10; ++i)
				{
					tweenManager.update(0.1f * timeControl.getTimeScale());
				}
				tweenManager.clear();	// reap before the next run
				return value;
			};
			const float fullProgress = runTween(1.0f);
			const float halfProgress = runTween(0.5f);
			timeControl.setTimeScale(1.0f);
			if (std::abs(fullProgress - 1.0f) > 1.0e-3f ||
				std::abs(halfProgress - 0.5f) > 1.0e-3f)
			{
				ok = false;
				detail = "time scale did not halve tween progress (full=" +
					std::to_string(fullProgress) + " half=" +
					std::to_string(halfProgress) + ")";
			}
		}

		SDL_Log("orkige_player: GAMESUPPORT SELFCHECK %s%s%s",
			ok ? "PASSED" : "FAILED", ok ? "" : " - ", detail.c_str());
		return ok ? 0 : 1;
	}

	// --- ORKIGE_GAMEPLAY_SELFCHECK=1: the gameplay-conveniences pack (Lua
	// `timer` / `game` tables + music.crossFade + CameraComponent smooth
	// follow), verified end to end against the live player wiring
	// (synchronous, any scene - reuses tests/projects/tween):
	//  (1) TIMER - timer.after fires ONCE at the delay, timer.every repeats
	//      on its period, timer.cancel(handle) stops one before it fires,
	//      all ticked through the loop's TimerManager (the tween phase). The
	//      auto-cancel-on-retire is covered by the TimerManager unit tests.
	//  (2) GAME STATE - game.setState fires game.stateChanged on the event
	//      bus (an events.subscribe handler receives { old, new }) and
	//      game.getState round-trips the value.
	//  (3) MUSIC CROSSFADE - music.crossFade tweens the incoming track's own
	//      volume up to full while the outgoing track is stopped at the end,
	//      riding ONE core_tween tween (the gain curve is unit-tested).
	//  (4) CAMERA FOLLOW - a CameraComponent in ORTHOGRAPHIC + FM_WIDTH fit
	//      eases its position onto a moved target, proving follow COMPOSES
	//      with the aspect-fit policy (fit sizes the projection, follow moves
	//      the position). The fit math itself is the CameraFit unit tests.
	if (std::getenv("ORKIGE_GAMEPLAY_SELFCHECK") != nullptr)
	{
		using Orkige::ScriptRuntime;
		bool ok = true;
		std::string detail;
		Orkige::ScriptComponent::ensureScriptApi();

		// (1)+(2) schedule timers, subscribe to the state event, and cancel
		// one timer up front - all in one chunk, results published into a
		// plain global table `gp` the C++ side reads back
		ScriptRuntime::Result setup =
			ScriptRuntime::getSingleton().runString(
				"gp = { after = 0, every = 0, cancelled = 0,"
				"       stateNew = '', stateOld = '', stateGet = '' }\n"
				"timer.after(0.5, function() gp.after = gp.after + 1 end)\n"
				"gp.everyH = timer.every(0.2, function()"
				"    gp.every = gp.every + 1 end)\n"
				"local ch = timer.after(0.5, function()"
				"    gp.cancelled = gp.cancelled + 1 end)\n"
				"timer.cancel(ch)\n"
				"events.subscribe('game.stateChanged', function(e)\n"
				"    gp.stateNew = e.new; gp.stateOld = e.old end)\n"
				"return true");
		if (!setup.success)
		{
			ok = false;
			detail = "gameplay setup script error: " + setup.error;
		}

		if (ok)
		{
			// 1.2s in 0.1s steps: after fires once (at 0.5), every fires
			// each 0.2s, the cancelled one never fires
			for (int i = 0; i < 12; ++i)
			{
				timerManager.update(0.1f);
			}
			const double afterCount = ScriptRuntime::getSingleton().getNumber(
				{ "gp", "after" }, -1.0);
			const double everyCount = ScriptRuntime::getSingleton().getNumber(
				{ "gp", "every" }, -1.0);
			const double cancelledCount =
				ScriptRuntime::getSingleton().getNumber(
					{ "gp", "cancelled" }, -1.0);
			if (afterCount != 1.0 || everyCount < 5.0 ||
				cancelledCount != 0.0)
			{
				ok = false;
				detail = "timers: after=" + std::to_string(afterCount) +
					" every=" + std::to_string(everyCount) +
					" cancelled=" + std::to_string(cancelledCount) +
					" (want after=1 every>=5 cancelled=0)";
			}
			if (ok)
			{
				// cancel the repeating timer and prove it stops firing
				ScriptRuntime::getSingleton().runString(
					"timer.cancel(gp.everyH)");
				const double before =
					ScriptRuntime::getSingleton().getNumber(
						{ "gp", "every" }, -1.0);
				for (int i = 0; i < 10; ++i)
				{
					timerManager.update(0.1f);
				}
				const double after =
					ScriptRuntime::getSingleton().getNumber(
						{ "gp", "every" }, -1.0);
				if (after != before)
				{
					ok = false;
					detail = "timer.cancel did not stop the repeating timer ("
						+ std::to_string(before) + " -> " +
						std::to_string(after) + ")";
				}
			}
		}

		// (2) game state: set it, drain the bus, check the event + readback
		if (ok)
		{
			ScriptRuntime::getSingleton().runString(
				"game.setState('playing'); gp.stateGet = game.getState()");
			// deliver the queued game.stateChanged to the subscriber
			if (Orkige::GlobalEventManager::getSingletonPtr())
			{
				Orkige::GlobalEventManager::getSingleton().tick();
			}
			const Orkige::String stateNew =
				ScriptRuntime::getSingleton().getString(
					{ "gp", "stateNew" }, "?");
			const Orkige::String stateOld =
				ScriptRuntime::getSingleton().getString(
					{ "gp", "stateOld" }, "?");
			const Orkige::String stateGet =
				ScriptRuntime::getSingleton().getString(
					{ "gp", "stateGet" }, "?");
			if (stateNew != "playing" || stateOld != "" ||
				stateGet != "playing" ||
				gameState.get() != "playing")
			{
				ok = false;
				detail = "game state: event new='" + stateNew + "' old='" +
					stateOld + "' get='" + stateGet + "' (want new=playing"
					" old='' get=playing)";
			}
		}

		// (3) music crossfade: register track A, crossFade to B over 0.5s;
		// after the fade B is the sole track at full own volume and A is gone
		if (ok)
		{
			ScriptRuntime::getSingleton().runString(
				"music.play('gp_a', 'music_loop.ogg')\n"
				"music.crossFade('gp_b', 'music_loop.ogg', 0.5)");
			for (int i = 0; i < 7; ++i)	// 0.7s > the 0.5s fade
			{
				tweenManager.update(0.1f);
			}
			Orkige::MusicStreamPtr incoming =
				Orkige::SoundManager::getSingleton().getMusic("gp_b");
			Orkige::MusicStreamPtr outgoing =
				Orkige::SoundManager::getSingleton().getMusic("gp_a");
			if (!incoming || incoming->getBaseGain() < 0.99f || outgoing)
			{
				ok = false;
				detail = std::string("music.crossFade: incoming ") +
					(incoming ? "gain=" +
						std::to_string(incoming->getBaseGain()) : "MISSING") +
					", outgoing " + (outgoing ? "STILL PRESENT" : "stopped");
			}
		}

		// (4) camera follow composes with the ortho fit policy. Isolate on a
		// fresh world (clear any loaded scene camera first), then a
		// CameraComponent in ORTHOGRAPHIC + FM_WIDTH eases onto a moved
		// target - the camera position tracks while the fit sizes the
		// projection. Skipped honestly when the window has no camera.
		if (ok && host.getWindowCamera())
		{
			gameObjectManager.clear();
			optr<Orkige::GameObject> targetObj =
				gameObjectManager.createGameObject("gp_target").lock();
			optr<Orkige::GameObject> camObj =
				gameObjectManager.createGameObject("gp_cam").lock();
			if (targetObj && camObj &&
				targetObj->addComponent<Orkige::TransformComponent>() &&
				camObj->addComponent<Orkige::TransformComponent>() &&
				camObj->addComponent<Orkige::CameraComponent>())
			{
				Orkige::CameraComponent* cam =
					camObj->getComponentPtr<Orkige::CameraComponent>();
				Orkige::TransformComponent* targetTransform =
					targetObj->getComponentPtr<Orkige::TransformComponent>();
				cam->setProjectionMode(
					Orkige::CameraComponent::PM_ORTHOGRAPHIC);
				cam->setFitMode(Orkige::CameraComponent::FM_WIDTH);
				cam->setDesignWidth(20.0f);
				cam->follow("gp_target", 0.1f);
				// move the target off-origin, then ease the camera onto it
				targetTransform->setPosition(Orkige::Vec3(5.0f, 3.0f, 0.0f));
				for (int i = 0; i < 180; ++i)	// ~3s at 60fps
				{
					gameObjectManager.update(1.0f / 60.0f);
				}
				const Orkige::Vec3 camPos = cam->getCameraPosition();
				if (camPos.x < 4.5f || camPos.y < 2.5f)
				{
					ok = false;
					detail = "camera follow did not track the target (camera "
						"at " + std::to_string(camPos.x) + "," +
						std::to_string(camPos.y) + ", want ~5,3)";
				}
			}
			else
			{
				ok = false;
				detail = "camera follow: could not build the test objects";
			}
		}

		SDL_Log("orkige_player: GAMEPLAY SELFCHECK %s%s%s",
			ok ? "PASSED" : "FAILED", ok ? "" : " - ", detail.c_str());
		return ok ? 0 : 1;
	}
	return std::nullopt;
}

//---------------------------------------------------------
void PlayerSelfChecks::beforeLoop(PlayerContext& context)
{
	if (softbodyCheck)
	{
		// MEASURED per-frame deform cost: build N representative blobs and
		// time a full deform tick (spring integrate + skin + write) each, so
		// the mobile budget is a real number. Pure core, allocation-free per
		// frame - no renderer involved.
		const int benchBlobs = 64;
		std::vector<Orkige::VectorTessellator::Region> benchRegions;
		{
			Orkige::VectorTessellator::Region region;
			region.fill = Orkige::VectorTessellator::Colour(1, 1, 1, 1);
			const int n = 24;
			for (int i = 0; i < n; ++i)
			{
				const float a = 2.0f * 3.14159265f * i / n;
				const float r = 1.0f + 0.12f * std::sin(a * 3.0f);
				region.outer.push_back(Orkige::VectorTessellator::Point(
					r * std::cos(a), r * std::sin(a)));
			}
			benchRegions.push_back(region);
		}
		Orkige::VectorTessellator::Mesh benchMesh;
		Orkige::VectorTessellator::build(benchRegions, 0.02f, benchMesh);
		std::vector<Orkige::SoftBodyDeform> benchDeformers(benchBlobs);
		for (int b = 0; b < benchBlobs; ++b)
		{
			benchDeformers[b].build(benchRegions, benchMesh.positions,
				Orkige::SoftBodyDeform::Params());
			benchDeformers[b].applyImpulse(0.3f, -1.0f, 2.0f);
			benchDeformers[b].setBodyVelocity(1.5f, -2.0f);
		}
		std::vector<Orkige::VectorTessellator::Point> benchScratch;
		const int benchFrames = 120;
		const auto benchStart = std::chrono::high_resolution_clock::now();
		for (int f = 0; f < benchFrames; ++f)
		{
			for (int b = 0; b < benchBlobs; ++b)
			{
				benchDeformers[b].update(1.0f / 60.0f);
				benchDeformers[b].writePositions(benchScratch);
			}
		}
		const auto benchEnd = std::chrono::high_resolution_clock::now();
		const double totalUs = std::chrono::duration_cast<
			std::chrono::nanoseconds>(benchEnd - benchStart).count() / 1000.0;
		const double perBlobFrameUs =
			totalUs / (benchFrames * benchBlobs);
		SDL_Log("orkige_player: SOFTBODY deform budget - %.2f us/blob/frame "
			"(%zu verts, %zu control points; %d blobs x %d frames = %.0f us "
			"total); a 60fps frame fits ~%.0f such blobs in 1 ms",
			perBlobFrameUs, benchMesh.positions.size(),
			benchDeformers[0].controlPointCount(), benchBlobs, benchFrames,
			totalUs, perBlobFrameUs > 0.0 ? 1000.0 / perBlobFrameUs : 0.0);
	}

	if (vectorAnimCheck)
	{
		// MEASURED per-frame cost: parse the hero rig, build the evaluator
		// and time a full runtime tick (advance a clip + compose the pose +
		// tessellate it into the upload buffer) over N frames, so the mobile
		// budget is a real number. Pure core, no renderer involved.
		Orkige::String text;
		if (Orkige::RenderSystem::get()->readResourceText("hero.oanim", text))
		{
			Orkige::VectorAnimAsset::Document document;
			Orkige::VectorAnimEval eval;
			if (Orkige::VectorAnimAsset::parse(text, document) &&
				eval.build(document))
			{
				std::vector<Orkige::VectorTessellator::Region> benchRegions;
				Orkige::VectorTessellator::Mesh benchMesh;
				const int benchFrames = 240;
				const auto benchStart =
					std::chrono::high_resolution_clock::now();
				for (int f = 0; f < benchFrames; ++f)
				{
					eval.update(1.0f / 60.0f);
					eval.writeRegions(benchRegions);
					Orkige::VectorTessellator::build(benchRegions, 0.02f,
						benchMesh);
				}
				const auto benchEnd =
					std::chrono::high_resolution_clock::now();
				const double totalUs = std::chrono::duration_cast<
					std::chrono::nanoseconds>(benchEnd - benchStart)
					.count() / 1000.0;
				const double perFrameUs = totalUs / benchFrames;
				SDL_Log("orkige_player: VECTORANIM budget - %.2f us/char/frame "
					"(evaluate + compose + tessellate; %zu layers, %zu shapes, "
					"%zu verts; %d frames = %.0f us total); a 60fps frame fits "
					"~%.0f such characters in 1 ms", perFrameUs,
					eval.layerCount(), eval.shapeCount(),
					benchMesh.positions.size(), benchFrames, totalUs,
					perFrameUs > 0.0 ? 1000.0 / perFrameUs : 0.0);
			}
		}
	}

	if (perfCheck)
	{
		// the check must also hold on a Release tree, where the scope
		// machinery boots disarmed - arm it like MSG_PROFILE would
		Orkige::ProfileManager::setEnabled(true);
	}

}

//---------------------------------------------------------
void PlayerSelfChecks::perFrame(PlayerContext& context)
{
	int& exitCode = context.exitCode;
	Orkige::BenchmarkRecorder& benchmarkRecorder = context.benchmarkRecorder;
	Orkige::RenderSystem* const render = context.render;
	Orkige::GameObjectManager& gameObjectManager = *context.gameObjectManagerPtr;
	Orkige::optr<Orkige::RenderNode>& cameraNode = context.cameraNode;
	Orkige::InputManager& inputManager = *context.inputManager;
	Orkige::SoundManager& soundManager = *context.soundManager;
	Orkige::PhysicsWorld& physicsWorld = *context.physicsWorld;
	Orkige::LevelManager& levelManager = *context.levelManager;
	Orkige::ScreenFade& screenFade = *context.screenFade;
	Orkige::SaveStore& saveStore = *context.saveStore;
	Orkige::AppLifecycle& lifecycle = *context.lifecycle;
	bool& running = context.running;
	unsigned long& frameCount = context.frameCount;

	// the script's published state (shared.jumper.<key>) - the honest
	// outside view of the Lua game; missing values read as the fallback
	auto jumperStat = [](const char* key, double fallback) -> double
	{
		return Orkige::ScriptRuntime::getSingleton().getNumber(
			{"shared", "jumper", key}, fallback);
	};
	auto jumperGrounded = []() -> bool
	{
		return Orkige::ScriptRuntime::getSingleton().getBool(
			{"shared", "jumper", "grounded"}, false);
	};
	// the game-flow state game.lua publishes (shared.game.<key>)
	auto jumperGameState = []() -> std::string
	{
		return Orkige::ScriptRuntime::getSingleton().getString(
			{"shared", "game", "state"}, "");
	};
	auto jumperGameStat = [](const char* key) -> double
	{
		return Orkige::ScriptRuntime::getSingleton().getNumber(
			{"shared", "game", key}, -1.0);
	};
	auto jumperWidgetExists = [](const char* id) -> bool
	{
		Orkige::GuiManager* ui =
			Orkige::GuiManager::getSingletonPtr();
		return ui && ui->widgetExists(id);
	};
	auto jumperWidgetVisible = [&jumperWidgetExists](const char* id) -> bool
	{
		if (!jumperWidgetExists(id))
		{
			return false;
		}
		optr<Orkige::GuiWidget> widget =
			Orkige::GuiManager::getSingleton().getWidget(id).lock();
		return widget && widget->getLayer()->isVisible();
	};
	auto jumperHudProgress = [&jumperWidgetExists]() -> float
	{
		if (!jumperWidgetExists("hud.progress"))
		{
			return -1.0f;
		}
		optr<Orkige::GuiProgressBar> progressBar =
			Orkige::GuiManager::getSingleton()
				.getWidgetAs<Orkige::GuiProgressBar>("hud.progress")
				.lock();
		return progressBar ? progressBar->getProgress() : -1.0f;
	};
	// player object access through the world - what the script drives
	auto jumperPlayerTransform = [&gameObjectManager]()
		-> Orkige::TransformComponent*
	{
		optr<Orkige::GameObject> player =
			gameObjectManager.getGameObject("Player").lock();
		if (!player ||
			!player->hasComponent<Orkige::TransformComponent>())
		{
			return nullptr;
		}
		return player->getComponentPtr<Orkige::TransformComponent>();
	};
	// teleport the player body between check phases (the same pose reset
	// the jumper sample's selfcheck uses)
	auto jumperTeleport = [&gameObjectManager, &physicsWorld,
		&jumperPlayerTransform](Orkige::Vec3 const& position)
	{
		optr<Orkige::GameObject> player =
			gameObjectManager.getGameObject("Player").lock();
		if (!player || !player->hasComponent<Orkige::RigidBodyComponent>())
		{
			return;
		}
		Orkige::RigidBodyComponent* body =
			player->getComponentPtr<Orkige::RigidBodyComponent>();
		if (!body->hasBody())
		{
			return;
		}
		physicsWorld.setBodyTransform(body->getBodyId(), position,
			Orkige::Quat::IDENTITY);
		body->setLinearVelocity(Orkige::Vec3::ZERO);
		body->setAngularVelocity(Orkige::Vec3::ZERO);
		jumperPlayerTransform()->setPosition(position);
	};
	auto jumperFail = [&](std::string const& what)
	{
		Orkige::TransformComponent* transform = jumperPlayerTransform();
		const Orkige::Vec3 position = transform ?
			transform->getPosition() : Orkige::Vec3::ZERO;
		SDL_Log("orkige_player: JUMPER-LUA SELFCHECK FAILED - %s "
			"(pos %.2f/%.2f/%.2f grounded=%d respawns=%.0f wins=%.0f)",
			what.c_str(), position.x, position.y, position.z,
			static_cast<int>(jumperGrounded()),
			jumperStat("respawns", -1.0), jumperStat("wins", -1.0));
		jumperCheckFailed = true;
	};
	auto rollerStat = [](const char* key, double fallback) -> double
	{
		return Orkige::ScriptRuntime::getSingleton().getNumber(
			{"shared", "roller", key}, fallback);
	};
	auto rollerFlag = [](const char* key) -> bool
	{
		return Orkige::ScriptRuntime::getSingleton().getBool(
			{"shared", "roller", key}, false);
	};
	auto rollerMode = []() -> std::string
	{
		return Orkige::ScriptRuntime::getSingleton().getString(
			{"shared", "roller", "mode"}, "");
	};
	auto rollerTransform = [&gameObjectManager](const char* id)
		-> Orkige::TransformComponent*
	{
		optr<Orkige::GameObject> gameObject =
			gameObjectManager.getGameObject(id).lock();
		if (!gameObject ||
			!gameObject->hasComponent<Orkige::TransformComponent>())
		{
			return nullptr;
		}
		return gameObject->getComponentPtr<Orkige::TransformComponent>();
	};
	// flavor-neutral, like the jumper
	// lambdas above
	auto rollerWidgetExists = [](const char* id) -> bool
	{
		Orkige::GuiManager* ui =
			Orkige::GuiManager::getSingletonPtr();
		return ui && ui->widgetExists(id);
	};
	auto rollerWidgetVisible = [&rollerWidgetExists](const char* id) -> bool
	{
		if (!rollerWidgetExists(id))
		{
			return false;
		}
		optr<Orkige::GuiWidget> widget =
			Orkige::GuiManager::getSingleton().getWidget(id).lock();
		return widget && widget->getLayer()->isVisible();
	};
	//! is the cursor sprite (the move-mode highlight) currently showing
	auto rollerCursorVisible = [&gameObjectManager]() -> bool
	{
		optr<Orkige::GameObject> cursor =
			gameObjectManager.getGameObject("Cursor").lock();
		if (!cursor || !cursor->hasComponent<Orkige::SpriteComponent>())
		{
			return false;
		}
		return cursor->getComponentPtr<Orkige::SpriteComponent>()
			->isSpriteVisible();
	};
	//! probe the physics world for collision geometry at (x, y): a ray
	//! along +z through the tile plane - the honest "did the BODY really
	//! move" check for the tile slide
	auto rollerProbeHit = [&physicsWorld](float x, float y) -> bool
	{
		Orkige::Vec3 hitPosition;
		Orkige::PhysicsWorld::BodyId hitBody;
		return physicsWorld.castRay(Orkige::Vec3(x, y, -3.0f),
			Orkige::Vec3(0.0f, 0.0f, 1.0f), 6.0f, hitPosition, hitBody);
	};
	auto rollerScreenshot = [&](const char* name)
	{
		if (rollerShotDir.empty())
		{
			return;
		}
		const std::string path = rollerShotDir + "/" + name;
		render->saveWindowContents(path);
		SDL_Log("orkige_player: roller selfcheck - screenshot %s",
			path.c_str());
	};
	auto rollerFail = [&](std::string const& what)
	{
		SDL_Log("orkige_player: ROLLER SELFCHECK FAILED - %s "
			"(ball %.2f/%.2f mode '%s' slides=%.0f wins=%.0f "
			"respawns=%.0f)", what.c_str(),
			rollerStat("x", -999.0), rollerStat("y", -999.0),
			rollerMode().c_str(), rollerStat("slides", -1.0),
			rollerStat("wins", -1.0), rollerStat("respawns", -1.0));
		rollerCheckFailed = true;
	};
	auto softbodyShape = [&gameObjectManager](const char* id)
		-> Orkige::VectorShapeComponent*
	{
		optr<Orkige::GameObject> gameObject =
			gameObjectManager.getGameObject(id).lock();
		if (!gameObject ||
			!gameObject->hasComponent<Orkige::VectorShapeComponent>())
		{
			return nullptr;
		}
		return gameObject->getComponentPtr<Orkige::VectorShapeComponent>();
	};
	auto softbodyFail = [&](std::string const& what)
	{
		SDL_Log("orkige_player: SOFTBODY SELFCHECK FAILED - %s "
			"(squash peak %.3f, wobble peak %.4f, morph peak %.4f)",
			what.c_str(), softbodyPeakSquash, softbodyPeakWobble,
			softbodyMorphPeak);
		softbodyCheckFailed = true;
	};
	auto vectorAnimComp = [&gameObjectManager]()
		-> Orkige::VectorAnimationComponent*
	{
		optr<Orkige::GameObject> gameObject =
			gameObjectManager.getGameObject("Hero").lock();
		if (!gameObject ||
			!gameObject->hasComponent<Orkige::VectorAnimationComponent>())
		{
			return nullptr;
		}
		return gameObject->getComponentPtr<Orkige::VectorAnimationComponent>();
	};
	auto vectorAnimEnded = []() -> double
	{
		return Orkige::ScriptRuntime::getSingleton().getNumber(
			{"shared", "heroanim", "ended"}, 0.0);
	};
	auto vectorAnimEndedObject = []() -> Orkige::String
	{
		return Orkige::ScriptRuntime::getSingleton().getString(
			{"shared", "heroanim", "lastObject"}, "");
	};
	auto vectorAnimFail = [&](std::string const& what)
	{
		SDL_Log("orkige_player: VECTORANIM SELFCHECK FAILED - %s "
			"(pose min %.3f, max %.3f)", what.c_str(),
			vectorAnimPoseMin, vectorAnimPoseMax);
		vectorAnimCheckFailed = true;
	};
	auto rollerProgFail = [&](std::string const& what)
	{
		SDL_Log("orkige_player: ROLLER PROGRESSION SELFCHECK FAILED - %s "
			"(levelIndex=%.0f mode '%s' slides=%.0f wins=%.0f saved=%d)",
			what.c_str(), rollerStat("levelIndex", -1.0),
			rollerMode().c_str(), rollerStat("slides", -1.0),
			rollerStat("wins", -1.0),
			static_cast<int>(Orkige::ScriptRuntime::getSingleton().getBool(
				{"shared", "roller", "saved"}, false)));
		rollerProgCheckFailed = true;
	};
	auto tweenStat = [](const char* key, double fallback) -> double
	{
		return Orkige::ScriptRuntime::getSingleton().getNumber(
			{"shared", "tween", key}, fallback);
	};
	auto tweenFlag = [](const char* key) -> bool
	{
		return Orkige::ScriptRuntime::getSingleton().getBool(
			{"shared", "tween", key}, false);
	};
	auto tweenFail = [&](std::string const& what)
	{
		SDL_Log("orkige_player: TWEEN SELFCHECK FAILED - %s "
			"(updates=%.0f completes=%.0f cancelUpdates=%.0f "
			"delayFirstAt=%.3f script='%s')", what.c_str(),
			tweenStat("updates", -1.0), tweenStat("completes", -1.0),
			tweenStat("cancelUpdates", -1.0),
			tweenStat("delayFirstAt", -1.0),
			Orkige::ScriptRuntime::getSingleton().getString(
				{"shared", "tween", "failed"}, "").c_str());
		tweenCheckFailed = true;
	};
	auto hotreloadNumber = [](const char* key, double fallback) -> double
	{
		return Orkige::ScriptRuntime::getSingleton().getNumber(
			{"shared", "hotreload", key}, fallback);
	};
	auto hotreloadProbeScript = [&gameObjectManager]()
		-> Orkige::ScriptComponent*
	{
		optr<Orkige::GameObject> probe =
			gameObjectManager.getGameObject("Probe").lock();
		if (!probe || !probe->hasComponent<Orkige::ScriptComponent>())
		{
			return nullptr;
		}
		return probe->getComponentPtr<Orkige::ScriptComponent>();
	};
	auto hotreloadProbeTransform = [&gameObjectManager]()
		-> Orkige::TransformComponent*
	{
		optr<Orkige::GameObject> probe =
			gameObjectManager.getGameObject("Probe").lock();
		if (!probe || !probe->hasComponent<Orkige::TransformComponent>())
		{
			return nullptr;
		}
		return probe->getComponentPtr<Orkige::TransformComponent>();
	};
	auto hotreloadWriteScript = [&](
		std::string const& source) -> bool
	{
		std::ofstream file(hotreloadScriptPath,
			std::ios::binary | std::ios::trunc);
		file << source;
		return file.good();
	};
	auto hotreloadFail = [&](std::string const& what)
	{
		SDL_Log("orkige_player: HOTRELOAD SELFCHECK FAILED - %s "
			"(value=%.0f inits=%.0f ticks=%.0f)", what.c_str(),
			hotreloadNumber("value", -1.0), hotreloadNumber("inits", -1.0),
			hotreloadNumber("ticks", -1.0));
		hotreloadCheckFailed = true;
	};
	auto scriptPropNumber = [](const char* key, double fallback) -> double
	{
		return Orkige::ScriptRuntime::getSingleton().getNumber(
			{"shared", "scriptprop", key}, fallback);
	};
	auto moverScript = [&gameObjectManager]() -> Orkige::ScriptComponent*
	{
		optr<Orkige::GameObject> mover =
			gameObjectManager.getGameObject("Mover").lock();
		if (!mover || !mover->hasComponent<Orkige::ScriptComponent>())
		{
			return nullptr;
		}
		return mover->getComponentPtr<Orkige::ScriptComponent>();
	};
	auto scriptPropFail = [&](std::string const& what)
	{
		SDL_Log("orkige_player: SCRIPTPROP SELFCHECK FAILED - %s "
			"(injected=%.3f x=%.3f elapsed=%.3f)", what.c_str(),
			scriptPropNumber("injectedSpeed", -1.0),
			scriptPropNumber("x", -999.0),
			scriptPropNumber("elapsed", -1.0));
		scriptPropCheckFailed = true;
	};
	auto integContactNum = [](const char* key, double fallback) -> double
	{
		return Orkige::ScriptRuntime::getSingleton().getNumber(
			{"shared", "integration", key}, fallback);
	};
	auto integContactStr = [](const char* key) -> std::string
	{
		return Orkige::ScriptRuntime::getSingleton().getString(
			{"shared", "integration", key}, "");
	};
	auto integContactFail = [&](std::string const& what)
	{
		SDL_Log("orkige_player: INTEGRATION CONTACT SELFCHECK FAILED - %s "
			"(found=%.0f foundGoal='%s' input=%.0f contact=%.0f "
			"contactOther='%s')", what.c_str(),
			integContactNum("found", -1.0),
			integContactStr("foundGoal").c_str(),
			integContactNum("input", -1.0),
			integContactNum("contact", -1.0),
			integContactStr("contactOther").c_str());
		integContactFailed = true;
	};
	auto integLevelNum = [](const char* key, double fallback) -> double
	{
		return Orkige::ScriptRuntime::getSingleton().getNumber(
			{"shared", "integ2", key}, fallback);
	};
	auto integFxLiveCount = [&gameObjectManager]() -> int
	{
		optr<Orkige::GameObject> fx =
			gameObjectManager.getGameObject("Fx").lock();
		if (!fx || !fx->hasComponent<Orkige::ParticleComponent>())
		{
			return -1;
		}
		return fx->getComponentPtr<Orkige::ParticleComponent>()
			->getLiveCount();
	};
	auto integLevelFail = [&](std::string const& what)
	{
		SDL_Log("orkige_player: INTEGRATION LEVELSWITCH SELFCHECK FAILED - "
			"%s (aliveA=%.0f moverX=%.3f switched=%.0f levelBBooted=%.0f "
			"carrySeen=%.0f levelBTicks=%.0f)", what.c_str(),
			integLevelNum("aliveA", -1.0), integLevelNum("moverX", -999.0),
			integLevelNum("switched", -1.0),
			integLevelNum("levelBBooted", -1.0),
			integLevelNum("carrySeen", -1.0),
			integLevelNum("levelBTicks", -1.0));
		integLevelFailed = true;
	};

	// --- jumper-lua selfcheck script (see the block above the loop) --
	if (jumperLuaCheck && !jumperCheckFailed &&
		jumperPhase == JumperCheckPhase::Script)
	{
		if (frameCount == 5)
		{
			optr<Orkige::GameObject> player =
				gameObjectManager.getGameObject("Player").lock();
			Orkige::ScriptComponent* script = (player &&
				player->hasComponent<Orkige::ScriptComponent>()) ?
				player->getComponentPtr<Orkige::ScriptComponent>() :
				nullptr;
			if (!script)
			{
				jumperFail("no Player object with a ScriptComponent "
					"in the scene");
			}
			else if (script->hasScriptError())
			{
				jumperFail("script error: " + script->getScriptError());
			}
			else if (!script->isScriptStarted())
			{
				jumperFail("script never loaded/started");
			}
			else if (jumperStat("respawns", -1.0) < 0.0)
			{
				jumperFail("script did not publish shared.jumper");
			}
			// the UI game.lua booted from Lua: every widget of the
			// three screens exists, the game sits on the visible
			// title screen and the HUD layer is still hidden
			// (UI-gated: skipped on the next flavor - HUD-less)
			else if (uiChecksEnabled && (!jumperWidgetExists("title.name") ||
				!jumperWidgetExists("title.prompt") ||
				!jumperWidgetExists("title.start") ||
				!jumperWidgetExists("hud.progress") ||
				!jumperWidgetExists("hud.wins") ||
				!jumperWidgetExists("hud.hint") ||
				!jumperWidgetExists("win.banner") ||
				!jumperWidgetExists("win.again")))
			{
				jumperFail("the Lua-booted gui widgets are "
					"missing");
			}
			else if (jumperGameState() != "title")
			{
				jumperFail("game did not start on the title screen "
					"(state '" + jumperGameState() + "')");
			}
			else if (uiChecksEnabled && (!jumperWidgetVisible("title.name") ||
				jumperWidgetVisible("hud.progress") ||
				jumperWidgetVisible("win.banner")))
			{
				jumperFail("title-screen layer visibility is wrong");
			}
			else
			{
				SDL_Log("orkige_player: jumper-lua selfcheck - "
					"'%s' loaded, %s, title state reached",
					script->getScriptFile().c_str(),
					uiChecksEnabled ? "UI up + title screen showing"
						: "HUD-less flavor (UI checks skipped)");
			}
		}
		// UI batching property (the gui perf contract, the
		// UiRenderer design rule): the WHOLE HUD - every widget of
		// the title/hud/win groups - is ONE draw batch (one screen
		// = one atlas = one DrawLayer2D batch). Hiding all views
		// for a frame must drop the batch count by exactly the
		// SCREEN count (1 here), never by the widget count (8+).
		if (uiChecksEnabled && frameCount == 6)
		{
			jumperBatchesWithUi = render->getFrameStats().batchCount;
			Orkige::GuiManager::getSingleton().hideAllViews();
		}
		if (uiChecksEnabled && frameCount == 8)
		{
			const size_t batchesWithoutUi =
				render->getFrameStats().batchCount;
			Orkige::GuiManager::getSingleton().showAllViews();
			if (jumperBatchesWithUi != batchesWithoutUi + 1)
			{
				jumperFail("UI batch property broken: the whole HUD "
					"must cost exactly ONE draw batch (with UI " +
					std::to_string(jumperBatchesWithUi) +
					" batches, without " +
					std::to_string(batchesWithoutUi) + ")");
			}
			else
			{
				SDL_Log("orkige_player: jumper-lua selfcheck - the "
					"whole HUD costs one draw batch (%zu -> %zu "
					"with all views hidden)",
					jumperBatchesWithUi, batchesWithoutUi);
			}
		}
		// starting the game: with UI, a synthetic mouse click on the
		// START button (center published by game.lua): move -> hover,
		// press -> down, release -> hit. HUD-less (next flavor),
		// game.lua only listens for ENTER - push that instead (the
		// click path stays classic-verified).
		if (frameCount == 10)
		{
			if (uiChecksEnabled)
			{
				pushMouseMove(
					static_cast<float>(jumperGameStat("startButtonX")),
					static_cast<float>(jumperGameStat("startButtonY")));
			}
			else
			{
				pushKeyEvent(SDL_SCANCODE_RETURN, SDLK_RETURN, true);
			}
		}
		if (frameCount == 12)
		{
			if (uiChecksEnabled)
			{
				pushMouseButton(
					static_cast<float>(jumperGameStat("startButtonX")),
					static_cast<float>(jumperGameStat("startButtonY")),
					true);
			}
			else
			{
				pushKeyEvent(SDL_SCANCODE_RETURN, SDLK_RETURN, false);
			}
		}
		if (frameCount == 14 && uiChecksEnabled)
		{
			pushMouseButton(
				static_cast<float>(jumperGameStat("startButtonX")),
				static_cast<float>(jumperGameStat("startButtonY")),
				false);
		}
		if (frameCount == 20)
		{
			if (jumperGameState() != "playing")
			{
				jumperFail("starting the game did not switch to "
					"playing (state '" + jumperGameState() + "')");
			}
			else if (uiChecksEnabled && (jumperWidgetVisible("title.name") ||
				!jumperWidgetVisible("hud.progress")))
			{
				jumperFail("start click did not swap title for HUD");
			}
			else
			{
				SDL_Log("orkige_player: jumper-lua selfcheck - game "
					"started (%s)", uiChecksEnabled
						? "START button clicked via synthetic mouse "
						"events, HUD up"
						: "ENTER on the HUD-less flavor");
			}
		}
		if (frameCount == 30)
		{
			jumperStartX = jumperPlayerTransform()->getPosition().x;
			pushKeyEvent(SDL_SCANCODE_RIGHT, SDLK_RIGHT, true);
			// CONDITION-DRIVEN pacing (the tilt-selfcheck precedent):
			// the script moves by dt, so distance tracks WALL TIME, not
			// frames - a fixed frame count walked the player off the
			// platform edge on slow hosts. Hold RIGHT until a distance
			// safely ON the first platform, then release.
			jumperMoveDeadline = frameCount + 240;
		}
		if (jumperMoveDeadline != 0 && frameCount > 30)
		{
			const float movedX =
				jumperPlayerTransform()->getPosition().x - jumperStartX;
			if (movedX >= 1.0f || frameCount >= jumperMoveDeadline)
			{
				pushKeyEvent(SDL_SCANCODE_RIGHT, SDLK_RIGHT, false);
				jumperMoveDeadline = 0;
				jumperMovedX = movedX;
				SDL_Log("orkige_player: jumper-lua selfcheck - moved "
					"+x %.3f m under RIGHT (HUD progress %.0f%%)",
					jumperMovedX, jumperHudProgress());
				if (jumperMovedX < 0.6f)
				{
					jumperFail("player did not move right under "
						"scripted input");
				}
				else if (uiChecksEnabled && jumperHudProgress() <= 0.0f)
				{
					jumperFail("HUD progress bar did not advance with x");
				}
				else
				{
					// settle, then wait for the ground contact - again by
					// condition, not by frame ordinal
					jumperGroundedDeadline = frameCount + 240;
				}
			}
		}
		if (jumperGroundedDeadline != 0)
		{
			if (jumperGrounded())
			{
				jumperGroundedDeadline = 0;
				jumperJumpStartY = jumperPlayerTransform()
					->getPosition().y;
				jumperJumpFrame = frameCount;
				jumperMaxRise = 0.0f;
				pushKeyEvent(SDL_SCANCODE_SPACE, SDLK_SPACE, true);
				jumperPhase = JumperCheckPhase::WaitLanding;
				jumperPhaseDeadline = frameCount + 240;
			}
			else if (frameCount >= jumperGroundedDeadline)
			{
				jumperGroundedDeadline = 0;
				jumperFail("script does not report grounded before "
					"the jump");
			}
		}
	}
	else if (jumperLuaCheck && !jumperCheckFailed &&
		jumperPhase == JumperCheckPhase::WaitLanding)
	{
		jumperMaxRise = std::max(jumperMaxRise,
			jumperPlayerTransform()->getPosition().y -
			jumperJumpStartY);
		if (frameCount == jumperJumpFrame + 5)
		{
			pushKeyEvent(SDL_SCANCODE_SPACE, SDLK_SPACE, false);
		}
		// give take-off a few frames before accepting "grounded"
		if (frameCount > jumperJumpFrame + 10 && jumperGrounded())
		{
			SDL_Log("orkige_player: jumper-lua selfcheck - jump rise "
				"%.3f m, landed %lu frames after take-off",
				jumperMaxRise, frameCount - jumperJumpFrame);
			if (jumperMaxRise < 0.8f)
			{
				jumperFail("scripted jump did not raise the player");
			}
			else
			{
				// drop into the first gap: the script's kill plane
				// must respawn the player at the start
				jumperBaseRespawns = jumperStat("respawns", 0.0);
				jumperTeleport(Orkige::Vec3(2.75f, 1.5f, 0.0f));
				jumperPhase = JumperCheckPhase::WaitRespawn;
				jumperPhaseDeadline = frameCount + 300;
			}
		}
		else if (frameCount >= jumperPhaseDeadline)
		{
			jumperFail("player never landed after the scripted jump");
		}
	}
	else if (jumperLuaCheck && !jumperCheckFailed &&
		jumperPhase == JumperCheckPhase::WaitRespawn)
	{
		if (jumperStat("respawns", 0.0) > jumperBaseRespawns)
		{
			const float spawnDistance = jumperPlayerTransform()
				->getPosition().distance(
					Orkige::Vec3(0.0f, 1.0f, 0.0f));
			SDL_Log("orkige_player: jumper-lua selfcheck - kill "
				"plane respawned the player %.3f m from the spawn",
				spawnDistance);
			if (spawnDistance > 1.0f)
			{
				jumperFail("respawn did not return to the start");
			}
			else
			{
				// walk-in test of the win path: drop just before the
				// buddy on the goal platform
				jumperBaseWins = jumperStat("wins", 0.0);
				jumperTeleport(Orkige::Vec3(36.0f, 3.0f, 0.0f));
				jumperPhase = JumperCheckPhase::WaitWin;
				jumperPhaseDeadline = frameCount + 300;
			}
		}
		else if (frameCount >= jumperPhaseDeadline)
		{
			jumperFail("falling off never triggered the script's "
				"respawn");
		}
	}
	else if (jumperLuaCheck && !jumperCheckFailed &&
		jumperPhase == JumperCheckPhase::WaitWin)
	{
		if (jumperStat("wins", 0.0) > jumperBaseWins)
		{
			SDL_Log("orkige_player: jumper-lua selfcheck - the goal "
				"fired the script's win (wins=%.0f) - waiting for "
				"the win screen", jumperStat("wins", -1.0));
			jumperPhase = JumperCheckPhase::WaitWinUi;
			jumperPhaseDeadline = frameCount + 120;
		}
		else if (frameCount >= jumperPhaseDeadline)
		{
			jumperFail("reaching the goal never triggered the "
				"script's win");
		}
	}
	else if (jumperLuaCheck && !jumperCheckFailed &&
		jumperPhase == JumperCheckPhase::WaitWinUi)
	{
		// the win overlay: game.lua switched to "win" and showed the
		// banner layer - then ENTER restarts the game (banner
		// visibility is UI-gated; the state machine runs HUD-less too)
		if (jumperGameState() == "win" &&
			(!uiChecksEnabled || jumperWidgetVisible("win.banner")))
		{
			SDL_Log("orkige_player: jumper-lua selfcheck - win "
				"screen up, pressing ENTER to restart");
			pushKeyEvent(SDL_SCANCODE_RETURN, SDLK_RETURN, true);
			jumperEnterFrame = frameCount;
			jumperPhase = JumperCheckPhase::WaitRestart;
			jumperPhaseDeadline = frameCount + 120;
		}
		else if (frameCount >= jumperPhaseDeadline)
		{
			jumperFail("the win never showed the win screen (state '" +
				jumperGameState() + "')");
		}
	}
	else if (jumperLuaCheck && !jumperCheckFailed &&
		jumperPhase == JumperCheckPhase::WaitRestart)
	{
		if (frameCount == jumperEnterFrame + 5)
		{
			pushKeyEvent(SDL_SCANCODE_RETURN, SDLK_RETURN, false);
		}
		if (jumperGameState() == "playing" &&
			(!uiChecksEnabled || !jumperWidgetVisible("win.banner")))
		{
			// camera-roll regression: the Lua follow camera lookAt's
			// the camera rig node every frame of this session - the
			// rig's fixed yaw axis must have kept it roll-free (the
			// old shortest-arc lookAt slowly tilted the horizon)
			const float cameraRollDegrees = std::abs(
				cameraNode->getOrientation()
					.getRoll().valueDegrees());
			SDL_Log("orkige_player: jumper-lua selfcheck - camera "
				"roll %.4f deg after the play session",
				cameraRollDegrees);
			if (cameraRollDegrees > 0.1f)
			{
				jumperFail("scripted follow camera accumulated roll");
			}
			else
			{
				SDL_Log("orkige_player: jumper-lua selfcheck complete "
					"- %s, movement (+%.2f m)%s, jump (+%.2f m), "
					"kill-plane respawn, the goal, the win flow, "
					"the ENTER restart and the roll-free camera all "
					"verified (respawns=%.0f wins=%.0f)",
					uiChecksEnabled ? "script+UI boot, START click"
						: "script boot (HUD-less flavor)",
					jumperMovedX,
					uiChecksEnabled ? " with HUD progress" : "",
					jumperMaxRise,
					jumperStat("respawns", -1.0),
					jumperStat("wins", -1.0));
				jumperPhase = JumperCheckPhase::Done;
				running = false;
			}
		}
		else if (frameCount >= jumperPhaseDeadline)
		{
			jumperFail("ENTER on the win screen did not restart the "
				"game (state '" + jumperGameState() + "')");
		}
	}
	if (jumperLuaCheck && jumperCheckFailed)
	{
		exitCode = 1;
		running = false;
	}

	// --- roller selfcheck script (see the block above the loop) -----
	if (rollerCheck && !rollerCheckFailed &&
		rollerPhase == RollerCheckPhase::Boot)
	{
		if (frameCount == 5)
		{
			// boot: both scripts up, state published, HUD widgets
			// exist, mode "play" with a running simulation
			optr<Orkige::GameObject> ball =
				gameObjectManager.getGameObject("Ball").lock();
			// the Ball carries TWO script component kinds ("ball" +
			// "ball_spin"); reach them by name-agnostic collect -
			// getComponentPtr<ScriptComponent>() would find NEITHER (they
			// are keyed by script name, not the class)
			Orkige::ScriptComponent* ballScript = nullptr;
			Orkige::ScriptComponent* spinScript = nullptr;
			if (ball)
			{
				for (Orkige::ScriptComponent* s :
					Orkige::ScriptComponent::collectFrom(*ball))
				{
					if (s->getComponentName() == "ball")
					{
						ballScript = s;
					}
					else if (s->getComponentName() == "ball_spin")
					{
						spinScript = s;
					}
				}
			}
			if (!ballScript || !spinScript)
			{
				rollerFail("the Ball must carry BOTH the 'ball' and "
					"'ball_spin' script component kinds");
			}
			else if (ballScript->hasScriptError())
			{
				rollerFail("ball script error: " +
					ballScript->getScriptError());
			}
			else if (spinScript->hasScriptError())
			{
				rollerFail("ball_spin script error: " +
					spinScript->getScriptError());
			}
			else if (!rollerFlag("gameReady") ||
				!rollerFlag("ballReady"))
			{
				rollerFail("scripts did not publish shared.roller");
			}
			else if (!rollerFlag("spinReady"))
			{
				rollerFail("the second script kind (ball_spin) on the "
					"Ball did not start");
			}
			else if (rollerStat("blinkRate", -1.0) != 2.0)
			{
				rollerFail("the ball_spin blinkRate scene override (2.0) "
					"did not reach self.blinkRate (declared-property "
					"override on a script kind)");
			}
			else if (!ball->hasComponent<Orkige::SpriteComponent>() ||
				!ball->getComponentPtr<Orkige::SpriteComponent>()
					->hasSprite())
			{
				rollerFail("the Ball has no loaded SpriteComponent");
			}
			else if (render->getWindowCamera()->getProjectionType() !=
				Orkige::RenderCamera::PT_ORTHOGRAPHIC)
			{
				rollerFail("ball.lua did not switch the camera to "
					"orthographic projection");
			}
			// UI-gated: skipped on the HUD-less next flavor
			else if (uiChecksEnabled && (!rollerWidgetExists("hud.mode") ||
				!rollerWidgetExists("hud.wins") ||
				!rollerWidgetExists("hud.hint") ||
				!rollerWidgetExists("warn.label") ||
				!rollerWidgetExists("win.banner")))
			{
				rollerFail("the Lua-booted HUD widgets are missing");
			}
			else if (rollerMode() != "play" ||
				physicsWorld.isPaused() || rollerCursorVisible())
			{
				rollerFail("did not boot into running play mode");
			}
			else
			{
				SDL_Log("orkige_player: roller selfcheck - scripts "
					"up, ortho camera, sprites loaded%s",
					uiChecksEnabled ? ", HUD showing"
						: " (HUD-less flavor, UI checks skipped)");
				rollerPhase = RollerCheckPhase::ActiveGate;
				rollerStepFrame = 0;
			}
		}
	}
	// active-state gate: deactivating the WHOLE TileC subtree (one
	// setActive on the parent) must stop its rendering (triangle
	// count) and take its wall bodies out of the simulation;
	// deactivating the Ball must gate its script ticks -
	// reactivation restores everything (frame-scripted)
	else if (rollerCheck && !rollerCheckFailed &&
		rollerPhase == RollerCheckPhase::ActiveGate)
	{
		if (rollerStepFrame == 0)
		{
			rollerStepFrame = frameCount;
		}
		const unsigned long step = frameCount - rollerStepFrame;
		// TileC sits at slot 2 (-3, 3): its bottom wall's world
		// pose is x=-3, y=0.25 (same probe math as the slide)
		if (step == 5)
		{
			optr<Orkige::GameObject> tileC =
				gameObjectManager.getGameObject("TileC").lock();
			optr<Orkige::GameObject> ball =
				gameObjectManager.getGameObject("Ball").lock();
			if (!tileC || !ball)
			{
				rollerFail("no TileC/Ball objects in the scene");
			}
			else if (gameObjectManager.getChildren("TileC").empty())
			{
				rollerFail("TileC has no children - the scene "
					"lost its tile groups");
			}
			else if (!rollerProbeHit(-3.0f, 0.25f))
			{
				rollerFail("TileC's bottom wall body is missing "
					"before the active-state gate");
			}
			else
			{
				rollerTrianglesActive =
					render->getFrameStats().triangleCount;
				tileC->setActive(false);
				ball->setActive(false);
			}
		}
		if (step == 10)
		{
			// sampled AFTER the deactivation settled: any later
			// growth means the gated script still ticks
			rollerBallUpdatesGated =
				rollerStat("ballUpdates", -1.0);
		}
		if (step == 25)
		{
			optr<Orkige::GameObject> wall = gameObjectManager
				.getGameObject("TileC/WallBottom").lock();
			if (rollerTrianglesActive == 0 ||
				render->getFrameStats().triangleCount >=
					rollerTrianglesActive)
			{
				rollerFail("deactivating the TileC subtree + ball "
					"did not reduce the triangle count (" +
					std::to_string(render->getFrameStats()
						.triangleCount) + " of " +
					std::to_string(rollerTrianglesActive) + ")");
			}
			else if (rollerProbeHit(-3.0f, 0.25f))
			{
				rollerFail("the deactivated tile's wall body "
					"still collides");
			}
			else if (!wall || wall->isActiveInHierarchy() ||
				!wall->isActiveSelf())
			{
				rollerFail("the tile children did not inherit "
					"the parent's inactive state");
			}
			else if (rollerStat("ballUpdates", -1.0) !=
				rollerBallUpdatesGated)
			{
				rollerFail("the deactivated ball's script still "
					"ticks");
			}
			else
			{
				gameObjectManager.getGameObject("TileC").lock()
					->setActive(true);
				gameObjectManager.getGameObject("Ball").lock()
					->setActive(true);
			}
		}
		if (step == 40)
		{
			if (render->getFrameStats().triangleCount <
				rollerTrianglesActive)
			{
				rollerFail("reactivation did not restore the "
					"rendered triangles");
			}
			else if (!rollerProbeHit(-3.0f, 0.25f))
			{
				rollerFail("reactivation did not restore the "
					"tile's wall body");
			}
			else if (rollerStat("ballUpdates", -1.0) <=
				rollerBallUpdatesGated)
			{
				rollerFail("reactivation did not resume the "
					"ball's script ticks");
			}
			else
			{
				SDL_Log("orkige_player: roller selfcheck - "
					"active-state gate OK (subtree hidden, body "
					"out of the sim, script gated; all restored)");
				rollerPhase = RollerCheckPhase::TiltRoll;
				rollerStepFrame = 0;
			}
		}
	}
	// tilt roll: hold LEFT until the (wall-clock paced) simulated
	// tilt built up AND the ball visibly rolled -x
	else if (rollerCheck && !rollerCheckFailed &&
		rollerPhase == RollerCheckPhase::TiltRoll)
	{
		if (rollerStepFrame == 0)
		{
			rollerStepFrame = frameCount;
			rollerStartX = static_cast<float>(rollerStat("x", 0.0));
			pushKeyEvent(SDL_SCANCODE_LEFT, SDLK_LEFT, true);
		}
		rollerMovedX = static_cast<float>(rollerStat("x", 0.0)) -
			rollerStartX;
		const Orkige::Vec3 tilt = inputManager.getTilt();
		if (tilt.x <= -0.35f && rollerMovedX <= -0.4f)
		{
			pushKeyEvent(SDL_SCANCODE_LEFT, SDLK_LEFT, false);
			SDL_Log("orkige_player: roller selfcheck - tilt %.2f/%.2f "
				"rolled the ball %.3f m left (%lu frames of LEFT)",
				tilt.x, tilt.y, rollerMovedX,
				frameCount - rollerStepFrame);
			rollerScreenshot("roller_play.png");
			rollerPhase = RollerCheckPhase::MoveWorld;
			rollerStepFrame = 0;
		}
		else if (frameCount >= rollerStepFrame + 1800)
		{
			rollerFail("holding LEFT never tilted gravity/rolled the "
				"ball (tilt " + std::to_string(tilt.x) + ", moved " +
				std::to_string(rollerMovedX) + ")");
		}
	}
	// move-world mode: TAB pauses, probes verify the tile slide
	// moves sprite AND body, TAB resumes (frame-scripted relative
	// to the phase anchor - no tilt dependency in here)
	else if (rollerCheck && !rollerCheckFailed &&
		rollerPhase == RollerCheckPhase::MoveWorld)
	{
		if (rollerStepFrame == 0)
		{
			rollerStepFrame = frameCount;
		}
		const unsigned long step = frameCount - rollerStepFrame;
		// TAB: move-world mode - physics pauses, the cursor shows
		if (step == 5)
		{
			pushKeyEvent(SDL_SCANCODE_TAB, SDLK_TAB, true);
		}
		if (step == 7)
		{
			pushKeyEvent(SDL_SCANCODE_TAB, SDLK_TAB, false);
		}
		if (step == 15)
		{
			Orkige::TransformComponent* tileB =
				rollerTransform("TileB/Frame");
			if (rollerMode() != "move" || !physicsWorld.isPaused())
			{
				rollerFail("TAB did not pause into move-world mode");
			}
			else if (!rollerCursorVisible())
			{
				rollerFail("move mode did not show the slot cursor");
			}
			else if (!tileB)
			{
				rollerFail("no TileB/Frame object in the scene");
			}
			else if (rollerProbeHit(3.0f, -5.75f))
			{
				rollerFail("the empty slot already has collision "
					"geometry before the slide");
			}
			else if (!rollerProbeHit(3.0f, 0.25f))
			{
				rollerFail("tile B's bottom wall body is missing at "
					"its starting location");
			}
			else
			{
				// the frame sprite is a CHILD of the TileB group:
				// track its WORLD pose (what the player sees) and
				// its LOCAL pose (which must NOT change - the slide
				// moves the PARENT)
				rollerTileBStartY = tileB->getWorldPosition().y;
				rollerTileBFrameLocalY = tileB->getPosition().y;
				Orkige::TransformComponent* goal =
					rollerTransform("Goal");
				rollerGoalStartY = goal ?
					goal->getWorldPosition().y : 0.0f;
				SDL_Log("orkige_player: roller selfcheck - move "
					"mode up (paused, cursor on), tile B at y=%.2f",
					rollerTileBStartY);
				rollerScreenshot("roller_move_mode.png");
			}
		}
		// DOWN: slide tile B (above the empty slot) down into it
		if (step == 20)
		{
			pushKeyEvent(SDL_SCANCODE_DOWN, SDLK_DOWN, true);
		}
		if (step == 22)
		{
			pushKeyEvent(SDL_SCANCODE_DOWN, SDLK_DOWN, false);
		}
		if (step == 32)
		{
			Orkige::TransformComponent* tileBGroup =
				rollerTransform("TileB");
			Orkige::TransformComponent* tileB =
				rollerTransform("TileB/Frame");
			Orkige::TransformComponent* goal =
				rollerTransform("Goal");
			optr<Orkige::GameObject> frameObject =
				gameObjectManager.getGameObject("TileB/Frame").lock();
			const float tileBMovedY = tileB ?
				tileB->getWorldPosition().y - rollerTileBStartY : 0.0f;
			if (rollerStat("slides", 0.0) < 1.0)
			{
				rollerFail("DOWN in move mode did not slide a tile");
			}
			else if (std::abs(tileBMovedY + 6.0f) > 0.01f)
			{
				rollerFail("tile B's frame sprite did not move one "
					"slot down (world)");
			}
			// the slide must have happened VIA THE PARENT: the group
			// object moved one slot, the child's LOCAL pose is
			// untouched
			else if (!frameObject ||
				frameObject->getParentId() != "TileB")
			{
				rollerFail("TileB/Frame is not a child of the TileB "
					"group");
			}
			else if (!tileBGroup || std::abs(tileBGroup
				->getWorldPosition().y + 3.0f) > 0.01f)
			{
				rollerFail("the TileB group parent did not move to "
					"the empty slot");
			}
			else if (!tileB || std::abs(tileB->getPosition().y -
				rollerTileBFrameLocalY) > 0.001f)
			{
				rollerFail("the slide changed the child's LOCAL "
					"transform - it did not move via the parent");
			}
			else if (!goal || std::abs(goal->getWorldPosition().y -
				(rollerGoalStartY - 6.0f)) > 0.01f)
			{
				rollerFail("the goal star did not ride its tile");
			}
			else if (!rollerProbeHit(3.0f, -5.75f))
			{
				rollerFail("tile B's wall BODY does not collide at "
					"the new location (sprite moved, body did not)");
			}
			else if (rollerProbeHit(3.0f, 0.25f))
			{
				rollerFail("tile B's wall body still collides at "
					"the OLD location");
			}
			else
			{
				SDL_Log("orkige_player: roller selfcheck - tile B "
					"slid %.2f in y VIA ITS PARENT while paused; "
					"wall body probes old=free new=hit, goal rode "
					"along", tileBMovedY);
			}
		}
		// TAB: back to play; then roll right into the goal tile
		if (step == 35)
		{
			pushKeyEvent(SDL_SCANCODE_TAB, SDLK_TAB, true);
		}
		if (step == 37)
		{
			pushKeyEvent(SDL_SCANCODE_TAB, SDLK_TAB, false);
		}
		if (step == 45)
		{
			if (rollerMode() != "play" || physicsWorld.isPaused() ||
				rollerCursorVisible())
			{
				rollerFail("TAB did not resume play mode");
			}
		}
		if (step == 50)
		{
			pushKeyEvent(SDL_SCANCODE_RIGHT, SDLK_RIGHT, true);
			rollerRollFrame = frameCount;
			rollerPhase = RollerCheckPhase::WaitWin;
			// fat deadline: the tilt swing is wall-clock paced while
			// headless sim frames run faster than real time
			rollerPhaseDeadline = frameCount + 3600;
		}
	}
	else if (rollerCheck && !rollerCheckFailed &&
		rollerPhase == RollerCheckPhase::WaitWin)
	{
		if (rollerStat("wins", 0.0) >= 1.0)
		{
			pushKeyEvent(SDL_SCANCODE_RIGHT, SDLK_RIGHT, false);
			SDL_Log("orkige_player: roller selfcheck - rolled into "
				"the goal %lu frames after tilting right (wins=%.0f "
				"respawns=%.0f)", frameCount - rollerRollFrame,
				rollerStat("wins", -1.0), rollerStat("respawns", -1.0));
			rollerPhase = RollerCheckPhase::WaitWinUi;
			rollerPhaseDeadline = frameCount + 90;
		}
		else if (frameCount >= rollerPhaseDeadline)
		{
			rollerFail("rolling right never reached the goal in the "
				"slid tile");
		}
	}
	else if (rollerCheck && !rollerCheckFailed &&
		rollerPhase == RollerCheckPhase::WaitWinUi)
	{
		if (!uiChecksEnabled || rollerWidgetVisible("win.banner"))
		{
			SDL_Log("orkige_player: roller selfcheck complete - "
				"tilt roll (%.2f m), move mode (pause+cursor), tile "
				"slide (sprite+body+goal), resume and the win path "
				"all verified%s", rollerMovedX, uiChecksEnabled
					? "" : " (win banner check skipped - HUD-less)");
			rollerPhase = RollerCheckPhase::Done;
			running = false;
		}
		else if (frameCount >= rollerPhaseDeadline)
		{
			rollerFail("the win never showed the win banner");
		}
	}
	if (rollerCheck && rollerCheckFailed)
	{
		exitCode = 1;
		running = false;
	}

	// --- softbody selfcheck (see the block above the loop) ----------
	if (softbodyCheck && !softbodyCheckFailed)
	{
		Orkige::VectorShapeComponent* fall = softbodyShape("FallBlob");
		Orkige::VectorShapeComponent* morph = softbodyShape("MorphBlob");
		// the Lua-driven morph runs from boot; track its deformation
		// throughout so the Morph phase can confirm it
		if (morph)
		{
			softbodyMorphPeak = std::max(softbodyMorphPeak,
				morph->getDeformDisplacement());
		}
		if (softbodyPhase == SoftBodyPhase::Fall)
		{
			if (frameCount == 5)
			{
				if (!fall)
				{
					softbodyFail("no FallBlob with a "
						"VectorShapeComponent");
				}
				else if (!fall->isSoftBodyEnabled())
				{
					softbodyFail("FallBlob is not a soft body");
				}
				else if (fall->getControlPointCount() == 0)
				{
					softbodyFail("FallBlob deformer built no control "
						"points");
				}
				else
				{
					softbodyPhaseDeadline = frameCount + 1500;
				}
			}
			else if (frameCount > 5 && fall)
			{
				const float squash = std::fabs(fall->getSquash());
				const float wobble = fall->getDeformDisplacement();
				softbodyPeakSquash =
					std::max(softbodyPeakSquash, squash);
				softbodyPeakWobble =
					std::max(softbodyPeakWobble, wobble);
				if (squash > 0.02f)
				{
					softbodySawSquash = true;
				}
				if (wobble > 0.01f)
				{
					softbodySawWobble = true;
				}
				if (softbodySawSquash && softbodySawWobble)
				{
					SDL_Log("orkige_player: softbody selfcheck - "
						"FallBlob landed and deformed (squash %.3f, "
						"wobble %.4f)", softbodyPeakSquash,
						softbodyPeakWobble);
					softbodyPhase = SoftBodyPhase::Settle;
					softbodyPhaseDeadline = frameCount + 1500;
				}
				else if (frameCount >= softbodyPhaseDeadline)
				{
					softbodyFail("the falling blob never "
						"squashed+wobbled on landing");
				}
			}
		}
		else if (softbodyPhase == SoftBodyPhase::Settle)
		{
			if (fall && !fall->isDeforming() &&
				fall->getDeformDisplacement() < 1.0e-3f)
			{
				SDL_Log("orkige_player: softbody selfcheck - FallBlob "
					"wobble decayed back to the rest pose");
				softbodyPhase = SoftBodyPhase::Morph;
				softbodyPhaseDeadline = frameCount + 900;
			}
			else if (frameCount >= softbodyPhaseDeadline)
			{
				softbodyFail("the blob's wobble never decayed back to "
					"rest");
			}
		}
		else if (softbodyPhase == SoftBodyPhase::Morph)
		{
			if (morph && morph->getMorphTargetCount() > 0 &&
				softbodyMorphPeak > 0.01f)
			{
				SDL_Log("orkige_player: softbody selfcheck complete - "
					"physics squash+wobble, exact return to rest, and "
					"the Lua-driven morph (peak %.4f, %zu target(s)) all "
					"verified", softbodyMorphPeak,
					morph->getMorphTargetCount());
				softbodyPhase = SoftBodyPhase::Done;
				running = false;
			}
			else if (frameCount >= softbodyPhaseDeadline)
			{
				softbodyFail("the Lua-driven morph never deformed the "
					"MorphBlob");
			}
		}
	}
	if (softbodyCheck && softbodyCheckFailed)
	{
		exitCode = 1;
		running = false;
	}

	// --- vectoranim selfcheck (see the block above the loop) ---------
	if (vectorAnimCheck && !vectorAnimCheckFailed)
	{
		Orkige::VectorAnimationComponent* hero = vectorAnimComp();
		// track the composed pose signature throughout so the Idle phase
		// can confirm the pose actually moves as the clip advances
		if (hero && hero->hasAnimation())
		{
			const float signature = hero->getPoseSignature();
			if (!vectorAnimPoseSeeded)
			{
				vectorAnimPoseMin = vectorAnimPoseMax = signature;
				vectorAnimPoseSeeded = true;
			}
			vectorAnimPoseMin = std::min(vectorAnimPoseMin, signature);
			vectorAnimPoseMax = std::max(vectorAnimPoseMax, signature);
		}
		if (vectorAnimPhase == VectorAnimPhase::Boot)
		{
			if (frameCount == 5)
			{
				if (!hero || !hero->hasAnimation())
				{
					vectorAnimFail("no Hero with a built "
						"VectorAnimationComponent");
				}
				else if (hero->getClipCount() < 2)
				{
					vectorAnimFail("the hero rig is missing its idle/hop "
						"clips");
				}
				else if (hero->currentClip() != "idle")
				{
					vectorAnimFail("the hero did not boot on the idle "
						"clip");
				}
				else if (hero->play("no_such_clip") ||
					hero->setClip("no_such_clip", 0.0f))
				{
					// an unknown name must refuse (and warn once - the
					// second call exercises the warn-once dedup)
					vectorAnimFail("an unknown clip name was accepted");
				}
				else if (hero->currentClip() != "idle")
				{
					vectorAnimFail("a refused clip name still moved "
						"the current clip");
				}
				else
				{
					vectorAnimPhase = VectorAnimPhase::Idle;
					vectorAnimDeadline = frameCount + 600;
				}
			}
		}
		else if (vectorAnimPhase == VectorAnimPhase::Idle)
		{
			// the idle clip advances: the pose signature spreads as the
			// body bobs (the vertices move each tick)
			if (vectorAnimPoseMax - vectorAnimPoseMin > 1.0e-3f)
			{
				SDL_Log("orkige_player: vectoranim selfcheck - the idle "
					"clip advances (pose signature moved %.4f)",
					vectorAnimPoseMax - vectorAnimPoseMin);
				vectorAnimPhase = VectorAnimPhase::Hop;
				vectorAnimDeadline = frameCount + 900;
			}
			else if (frameCount >= vectorAnimDeadline)
			{
				vectorAnimFail("the idle clip never moved the pose");
			}
		}
		else if (vectorAnimPhase == VectorAnimPhase::Hop)
		{
			// the script crossFades to the one-shot hop after a beat
			if (hero && hero->currentClip() == "hop")
			{
				SDL_Log("orkige_player: vectoranim selfcheck - the "
					"scripted crossFade switched to the hop clip");
				vectorAnimPhase = VectorAnimPhase::Ended;
				vectorAnimDeadline = frameCount + 900;
			}
			else if (frameCount >= vectorAnimDeadline)
			{
				vectorAnimFail("the scripted crossFade to hop never "
					"took");
			}
		}
		else if (vectorAnimPhase == VectorAnimPhase::Ended)
		{
			// the one-shot hop completed: the ended event reached the
			// script AND the component reports the clip finished
			if (hero && hero->isAtEnd() && vectorAnimEnded() >= 1.0)
			{
				// the bus payload names the rig's owner (e.object)
				Orkige::GameObject* owner = hero->getComponentOwner();
				if (!owner ||
					vectorAnimEndedObject() != owner->getObjectID())
				{
					vectorAnimFail("the ended event did not carry the "
						"owner object id");
				}
				else
				{
					SDL_Log("orkige_player: vectoranim selfcheck "
						"complete - idle advance, crossFade to hop, and "
						"the one-shot ended event delivered into the "
						"script (%.0f, object id carried) all verified",
						vectorAnimEnded());
					vectorAnimPhase = VectorAnimPhase::Done;
					running = false;
				}
			}
			else if (frameCount >= vectorAnimDeadline)
			{
				vectorAnimFail("the one-shot hop never fired its ended "
					"event into the script");
			}
		}
	}
	if (vectorAnimCheck && vectorAnimCheckFailed)
	{
		exitCode = 1;
		running = false;
	}

	// --- TEXTURED cutout parts on a vector rig (see the ORKIGE_CUTOUT_
	// SELFCHECK block in PlayerSelfChecks.h) ---------------------------
	if (cutoutCheck && !cutoutCheckFailed && cutoutPhase != CutoutPhase::Done)
	{
		auto cutoutComp = [&gameObjectManager]()
			-> Orkige::VectorAnimationComponent*
		{
			optr<Orkige::GameObject> gameObject =
				gameObjectManager.getGameObject("Cutout").lock();
			if (!gameObject ||
				!gameObject->hasComponent<Orkige::VectorAnimationComponent>())
			{
				return nullptr;
			}
			return gameObject->
				getComponentPtr<Orkige::VectorAnimationComponent>();
		};
		auto cutoutFail = [&](std::string const& what)
		{
			SDL_Log("orkige_player: CUTOUT SELFCHECK FAILED - %s "
				"(pose min %.3f, max %.3f)", what.c_str(),
				cutoutPoseMin, cutoutPoseMax);
			cutoutCheckFailed = true;
		};
		Orkige::VectorAnimationComponent* rig = cutoutComp();
		if (rig && rig->hasAnimation())
		{
			const float signature = rig->getPoseSignature();
			if (!cutoutPoseSeeded)
			{
				cutoutPoseMin = cutoutPoseMax = signature;
				cutoutPoseSeeded = true;
			}
			cutoutPoseMin = std::min(cutoutPoseMin, signature);
			cutoutPoseMax = std::max(cutoutPoseMax, signature);
		}
		if (cutoutPhase == CutoutPhase::Boot)
		{
			if (frameCount == 5)
			{
				if (!rig || !rig->hasAnimation())
				{
					cutoutFail("no Cutout with a built "
						"VectorAnimationComponent");
				}
				else if (rig->getTexturedRunCount() < 3 ||
					rig->getRunCount() < 4)
				{
					// the mixed rig must split into per-texture draw runs
					// (one flat shadow run + body/head/arm textures)
					cutoutFail("the rig did not split into the expected "
						"draw runs (textured "
						+ std::to_string(rig->getTexturedRunCount())
						+ " of " + std::to_string(rig->getRunCount())
						+ ", want >= 3 of >= 4)");
				}
				else if (rig->currentClip() != "wave")
				{
					cutoutFail("the rig did not boot on the wave clip");
				}
				else
				{
					SDL_Log("orkige_player: cutout selfcheck - rig built "
						"(%zu runs, %zu textured, %zu triangles)",
						rig->getRunCount(), rig->getTexturedRunCount(),
						rig->getTriangleCount());
					cutoutPhase = CutoutPhase::Motion;
					cutoutDeadline = frameCount + 600;
				}
			}
		}
		else if (cutoutPhase == CutoutPhase::Motion)
		{
			// the wave clip advances: the textured quads move through the
			// dynamic per-section upload (the pose signature spreads)
			if (cutoutPoseMax - cutoutPoseMin > 1.0e-3f)
			{
				SDL_Log("orkige_player: cutout selfcheck - the wave clip "
					"advances (pose signature moved %.4f)",
					cutoutPoseMax - cutoutPoseMin);
				cutoutPhase = CutoutPhase::Shot;
				// let the wave settle a beat so the screenshot is
				// unmistakably mid-clip
				cutoutShotFrame = frameCount + 30;
				cutoutDeadline = frameCount + 600;
			}
			else if (frameCount >= cutoutDeadline)
			{
				cutoutFail("the wave clip never moved the pose");
			}
		}
		else if (cutoutPhase == CutoutPhase::Shot)
		{
			if (frameCount >= cutoutShotFrame)
			{
				if (!cutoutShotDir.empty())
				{
					const std::string path =
						cutoutShotDir + "/cutout_play.png";
					render->saveWindowContents(path);
					SDL_Log("orkige_player: cutout selfcheck - "
						"screenshot %s", path.c_str());
				}
				SDL_Log("orkige_player: cutout selfcheck complete - run "
					"split, animated textured pose and screenshot all "
					"verified");
				cutoutPhase = CutoutPhase::Done;
				running = false;
			}
		}
	}
	if (cutoutCheck && cutoutCheckFailed)
	{
		exitCode = 1;
		running = false;
	}

	// --- 3D SKELETAL character animation (see the ORKIGE_CHARACTER_RIG_
	// SELFCHECK block in PlayerSelfChecks.h) ---------------------------
	if (characterRigCheck && !characterRigCheckFailed &&
		characterRigPhase != CharacterRigPhase::Done)
	{
		auto rigObject = [&gameObjectManager]() -> optr<Orkige::GameObject>
		{
			return gameObjectManager.getGameObject("Mannequin").lock();
		};
		auto animComp = [&]() -> Orkige::AnimationComponent*
		{
			optr<Orkige::GameObject> go = rigObject();
			if (!go || !go->hasComponent<Orkige::AnimationComponent>())
			{
				return nullptr;
			}
			return go->getComponentPtr<Orkige::AnimationComponent>();
		};
		auto rigMesh = [&]() -> optr<Orkige::MeshInstance>
		{
			optr<Orkige::GameObject> go = rigObject();
			if (!go || !go->hasComponent<Orkige::ModelComponent>())
			{
				return optr<Orkige::MeshInstance>();
			}
			Orkige::ModelComponent* model =
				go->getComponentPtr<Orkige::ModelComponent>();
			return model ? model->getMeshInstance()
				: optr<Orkige::MeshInstance>();
		};
		// the skeleton-driven LOCAL bounds signature: as a limb swings, the
		// skinned vertices move and the animated bounds spread (the bone-driven
		// deformation proof, no pixel readback needed)
		auto boundsSignature = [&]() -> float
		{
			optr<Orkige::MeshInstance> mesh = rigMesh();
			if (!mesh)
			{
				return 0.0f;
			}
			Orkige::AABB bounds = mesh->getLocalBounds();
			Orkige::Vec3 mn = bounds.getMinimum();
			Orkige::Vec3 mx = bounds.getMaximum();
			return std::abs(mn.x) + std::abs(mn.y) + std::abs(mn.z) +
				std::abs(mx.x) + std::abs(mx.y) + std::abs(mx.z);
		};
		auto clipEnabled = [&](const char* name) -> bool
		{
			optr<Orkige::MeshInstance> mesh = rigMesh();
			if (!mesh)
			{
				return false;
			}
			Orkige::StringVector enabled = mesh->getEnabledAnimations();
			return std::find(enabled.begin(), enabled.end(),
				Orkige::String(name)) != enabled.end();
		};
		auto characterFail = [&](std::string const& what)
		{
			SDL_Log("orkige_player: CHARACTER RIG SELFCHECK FAILED - %s "
				"(bounds spread %.4f)", what.c_str(),
				characterRigBoundsMax - characterRigBoundsMin);
			characterRigCheckFailed = true;
		};
		Orkige::AnimationComponent* anim = animComp();

		// bone-attachment leg (runs every frame once the marker exists): the
		// BoneMarker follows the mannequin's armR bone, so its world pose must
		// MOVE with the swinging arm and MATCH the facade bone pose plus offset
		if (characterMarkerCreated)
		{
			optr<Orkige::GameObject> marker =
				gameObjectManager.getGameObject("BoneMarker").lock();
			optr<Orkige::MeshInstance> mesh = rigMesh();
			if (marker &&
				marker->hasComponent<Orkige::TransformComponent>() && mesh)
			{
				// the marker is a ROOT object, so its local == world pose
				Orkige::TransformComponent* markerTransform =
					marker->getComponentPtr<Orkige::TransformComponent>();
				const Orkige::Vec3 markerPos = markerTransform->getPosition();
				const float markerSig = std::abs(markerPos.x) +
					std::abs(markerPos.y) + std::abs(markerPos.z);
				if (!characterMarkerSeeded)
				{
					characterMarkerMin = characterMarkerMax = markerSig;
					characterMarkerSeeded = true;
				}
				characterMarkerMin = std::min(characterMarkerMin, markerSig);
				characterMarkerMax = std::max(characterMarkerMax, markerSig);
				// correctness: the marker sits at the facade bone pose plus the
				// bone-local offset (the same math the component runs)
				Orkige::Vec3 bonePos, boneScale;
				Orkige::Quat boneRot;
				if (mesh->getBoneWorldTransform("armR", bonePos, boneRot,
					boneScale))
				{
					const Orkige::Vec3 expected =
						bonePos + boneRot * Orkige::Vec3(0.0f, -0.5f, 0.0f);
					// the marker sits ON the armR bone (offset down the arm),
					// not at the origin - the wiring-correctness tolerance is
					// generous enough to absorb the render-vs-update one-frame
					// pose lag on the derived-cache flavor (a mis-wired marker
					// misses by ~1 world unit, this catches it)
					if ((expected - markerPos).length() < 0.1f)
					{
						characterMarkerMatched = true;
					}
				}
			}
		}

		if (characterRigPhase == CharacterRigPhase::Boot)
		{
			if (frameCount == 5)
			{
				if (!anim)
				{
					characterFail("no Mannequin with an AnimationComponent");
				}
				else if (!anim->hasAnimations())
				{
					// the honest static-import skip: a flavor that bakes glTF
					// transforms (dropping the skeleton) carries no clips -
					// both shipping flavors import the rig whole today, so
					// this guards a hypothetical future flavor
					SDL_Log("orkige_player: character rig selfcheck SKIPPED - "
						"the rig loaded with NO animations: this flavor imports "
						"glTF statically (skeleton + clips dropped at import), "
						"so 3D skeletal playback is not available on it");
					characterRigSkipped = true;
					characterRigPhase = CharacterRigPhase::Done;
					running = false;
				}
				else
				{
					Orkige::StringList const& names =
						anim->getAvailableAnimations();
					const bool hasWalk = std::find(names.begin(), names.end(),
						Orkige::String("walk")) != names.end();
					const bool hasIdle = std::find(names.begin(), names.end(),
						Orkige::String("idle")) != names.end();
					if (!hasWalk || !hasIdle)
					{
						characterFail("the rig is missing its walk/idle clips");
					}
					else if (!anim->playAnimation("walk", true))
					{
						characterFail("could not play the walk clip");
					}
					else
					{
						const float sig = boundsSignature();
						characterRigBoundsMin = characterRigBoundsMax = sig;
						characterRigBoundsSeeded = true;
						// spawn a bone-follower marker on the mannequin's right
						// arm (a hand marker: offset down the arm) - it must track
						// the swinging arm across the animated phases (both flavors)
						if (optr<Orkige::GameObject> marker =
							gameObjectManager.createGameObject("BoneMarker").lock())
						{
							marker->addComponent<Orkige::BoneAttachComponent>();
							if (Orkige::BoneAttachComponent* attach =
								marker->getComponentPtr<Orkige::BoneAttachComponent>())
							{
								attach->setTarget("Mannequin");
								attach->setBone("armR");
								attach->setOffsetXYZ(0.0f, -0.5f, 0.0f);
								characterMarkerCreated = true;
							}
						}
						if (!characterMarkerCreated)
						{
							characterFail("could not create the bone-attach "
								"marker");
						}
						characterRigPhase = CharacterRigPhase::Walk;
						characterRigDeadline = frameCount + 600;
					}
				}
			}
		}
		else if (characterRigPhase == CharacterRigPhase::Walk)
		{
			const float sig = boundsSignature();
			characterRigBoundsMin = std::min(characterRigBoundsMin, sig);
			characterRigBoundsMax = std::max(characterRigBoundsMax, sig);
			// pose-SHAPE sample: once the walk clip has advanced to an
			// INTERPOLATED mid-clip frame (between the 0.0/0.5/1.0 s keys - the
			// region where a translation collapse would leak the bind pose into
			// the track), read the four limb bones through the facade and assert
			// the figure keeps its SHAPE. The motion-only bounds spread below
			// still passes when every bone has fused to the skeleton root; this
			// leg is the one that fails on that collapse. The 0.25 s-wide window
			// is wider than the largest clamped frame step, so a frame lands in it.
			if (!characterRigPoseChecked)
			{
				optr<Orkige::MeshInstance> mesh = rigMesh();
				const float walkTime =
					mesh ? mesh->getAnimationTime("walk") : 0.0f;
				if (mesh && walkTime >= 0.2f && walkTime <= 0.45f)
				{
					Orkige::Vec3 legLPos, legRPos, armLPos, armRPos, boneScale;
					Orkige::Quat boneRot;
					const bool read =
						mesh->getBoneWorldTransform("legL", legLPos, boneRot,
							boneScale) &&
						mesh->getBoneWorldTransform("legR", legRPos, boneRot,
							boneScale) &&
						mesh->getBoneWorldTransform("armL", armLPos, boneRot,
							boneScale) &&
						mesh->getBoneWorldTransform("armR", armRPos, boneRot,
							boneScale);
					if (!read)
					{
						characterFail("could not read the limb bone poses for the "
							"pose-shape sample");
					}
					else
					{
						// (a) LEG SEPARATION: the legs swing about X (forward/back),
						// so their world-X gap holds near the 0.44 bind separation;
						// a collapse drops both toward the root and the gap to ~0
						const float legSep = std::abs(legLPos.x - legRPos.x);
						// (b) ARM HEIGHT: the arms stay ABOVE the legs (the figure
						// reads upright) - a cheap whole-body shape invariant
						const float armMinY = std::min(armLPos.y, armRPos.y);
						const float legMaxY = std::max(legLPos.y, legRPos.y);
						characterRigLegSeparation = legSep;
						characterRigArmMinY = armMinY;
						characterRigLegMaxY = legMaxY;
						const float bindSep = 0.44f;
						if (legSep < 0.5f * bindSep || legSep > 2.0f * bindSep)
						{
							SDL_Log("orkige_player: character rig selfcheck - the "
								"walk pose COLLAPSED (leg world-X separation %.3f "
								"left the [%.3f, %.3f] band around the %.3f bind gap)",
								legSep, 0.5f * bindSep, 2.0f * bindSep, bindSep);
							characterFail("the legs collapsed toward the skeleton "
								"root (leg separation out of band)");
						}
						else if (armMinY <= legMaxY)
						{
							SDL_Log("orkige_player: character rig selfcheck - the "
								"walk pose lost its shape (arms Y %.3f are not above "
								"legs Y %.3f)", armMinY, legMaxY);
							characterFail("the arm bones are not above the leg "
								"bones (figure lost its upright shape)");
						}
						else
						{
							SDL_Log("orkige_player: character rig selfcheck - the "
								"walk pose keeps its shape at %.3f s (leg world-X "
								"separation %.3f in band around the %.3f bind gap; "
								"arms Y %.3f above legs Y %.3f)", walkTime, legSep,
								bindSep, armMinY, legMaxY);
							characterRigPoseChecked = true;
						}
					}
				}
			}
			// the threshold proves MOTION, not magnitude: on a flavor whose
			// animated bounds arm before the first sample (next), the spread
			// is the pure animation amplitude (~0.05 for the walk bob), while
			// classic's first crossing rides a larger bind-box transition
			if (characterRigPoseChecked &&
				characterRigBoundsMax - characterRigBoundsMin > 0.03f)
			{
				SDL_Log("orkige_player: character rig selfcheck - the walk clip "
					"moves bone-driven vertices (skeletal bounds spread %.3f)",
					characterRigBoundsMax - characterRigBoundsMin);
				if (anim && anim->crossFadeTo("idle", 0.4f))
				{
					characterRigPhase = CharacterRigPhase::Blend;
					characterRigDeadline = frameCount + 600;
				}
				else
				{
					characterFail("crossFadeTo(idle) was refused");
				}
			}
			else if (frameCount >= characterRigDeadline)
			{
				characterFail(characterRigPoseChecked
					? "the walk clip never moved the skeletal bounds"
					: "the walk clip never reached the mid-clip pose-shape sample");
			}
		}
		else if (characterRigPhase == CharacterRigPhase::Blend)
		{
			if (anim && anim->isCrossFading())
			{
				characterRigSawBlending = true;
				if (clipEnabled("walk") && clipEnabled("idle"))
				{
					// both clips run together mid-blend = a real weighted
					// crossfade, not a hard cut
					characterRigSawBothEnabled = true;
				}
			}
			else if (anim)
			{
				// the blend completed - the outgoing clip must be gone and idle
				// the sole survivor, and the blend must actually have happened
				if (!characterRigSawBlending || !characterRigSawBothEnabled)
				{
					characterFail("the crossfade never blended both clips");
				}
				else if (clipEnabled("walk"))
				{
					characterFail("the walk clip was not dropped after the "
						"crossfade");
				}
				else if (!clipEnabled("idle"))
				{
					characterFail("idle is not playing after the crossfade");
				}
				else
				{
					SDL_Log("orkige_player: character rig selfcheck - the "
						"crossfade blended walk->idle (both clips ran together, "
						"then walk dropped)");
					const float sig = boundsSignature();
					characterRigIdleBoundsMin = characterRigIdleBoundsMax = sig;
					characterRigIdleSeeded = true;
					characterRigPhase = CharacterRigPhase::Idle;
					characterRigDeadline = frameCount + 600;
				}
			}
			if (!characterRigCheckFailed &&
				characterRigPhase == CharacterRigPhase::Blend &&
				frameCount >= characterRigDeadline)
			{
				characterFail("the crossfade never completed");
			}
		}
		else if (characterRigPhase == CharacterRigPhase::Idle)
		{
			const float sig = boundsSignature();
			characterRigIdleBoundsMin = std::min(characterRigIdleBoundsMin, sig);
			characterRigIdleBoundsMax = std::max(characterRigIdleBoundsMax, sig);
			// motion-not-magnitude again: idle's sway saturates near 0.02 on
			// the flavor with exact animated-amplitude bounds (next)
			if (characterRigIdleBoundsMax - characterRigIdleBoundsMin > 0.01f)
			{
				// the bone-attachment leg must have proven out by now: the
				// marker tracked the facade bone pose AND swept with the arm
				if (!characterMarkerMatched)
				{
					characterFail("the bone-attach marker never matched the "
						"armR bone pose");
				}
				else if (characterMarkerMax - characterMarkerMin < 0.02f)
				{
					characterFail("the bone-attach marker never moved with the "
						"animated arm");
				}
				else
				{
					SDL_Log("orkige_player: character rig selfcheck - walk moved "
						"skeletal bounds (spread %.3f), a weighted crossfade "
						"blended to idle, idle's sway keeps moving the bounds "
						"(spread %.3f), and the armR-attached marker tracked the "
						"animated bone (marker spread %.3f, pose matched); now "
						"testing playback-state resume",
						characterRigBoundsMax - characterRigBoundsMin,
						characterRigIdleBoundsMax - characterRigIdleBoundsMin,
						characterMarkerMax - characterMarkerMin);
					characterRigPhase = CharacterRigPhase::Resume;
					characterRigDeadline = frameCount + 300;
				}
			}
			else if (frameCount >= characterRigDeadline)
			{
				characterFail("the idle sway never moved the skeletal bounds");
			}
		}
		else if (characterRigPhase == CharacterRigPhase::Resume)
		{
			if (!characterResumeArmed)
			{
				// simulate a scene LOAD of a mid-animation state through the
				// reflected setters (the exact loadComponentProperties path):
				// walk at 0.5 s while idle is the live clip. The resume is
				// applied on the NEXT tick (@see AnimationComponent).
				if (anim)
				{
					anim->setPlaybackClip("walk");
					anim->setPlaybackLoop(true);
					anim->setPlaybackTime(0.5f);
					characterResumeArmed = true;
				}
			}
			else
			{
				// after the restore tick, walk (not idle) plays at ~0.5 s: the
				// loaded state resumed the saved clip AND phase
				optr<Orkige::MeshInstance> mesh = rigMesh();
				const bool walkPlaying = clipEnabled("walk");
				const bool idleStopped = !clipEnabled("idle");
				if (mesh && walkPlaying && idleStopped)
				{
					const float resumedTime = mesh->getAnimationTime("walk");
					if (std::abs(resumedTime - 0.5f) < 0.1f)
					{
						SDL_Log("orkige_player: character rig selfcheck complete "
							"- 3D skeletal playback + blend + bone attachment "
							"verified, and a loaded mid-animation state resumed "
							"the saved clip at the saved phase (walk at %.3f s, "
							"armed 0.5, idle dropped) - playback serialization "
							"verified", resumedTime);
						characterRigPhase = CharacterRigPhase::Done;
						running = false;
					}
					else if (frameCount >= characterRigDeadline)
					{
						characterFail("the resumed clip is at the wrong phase");
					}
				}
				else if (frameCount >= characterRigDeadline)
				{
					characterFail("the loaded playback state did not resume the "
						"saved clip");
				}
			}
		}
	}
	if (characterRigCheck && characterRigCheckFailed)
	{
		exitCode = 1;
		running = false;
	}

	// --- roller PROGRESSION selfcheck (see the block above the loop) --
	if (rollerProgressionCheck && !rollerProgCheckFailed &&
		rollerProgPhase == RollerProgPhase::Boot)
	{
		if (frameCount == 5)
		{
			if (!rollerFlag("gameReady") || !rollerFlag("ballReady"))
			{
				rollerProgFail("scripts did not publish shared.roller");
			}
			else if (levelManager.count() < 2)
			{
				rollerProgFail("the level sequence did not load (need "
					">= 2 levels, got " +
					std::to_string(levelManager.count()) + ")");
			}
			else if (static_cast<int>(rollerStat("levelIndex", -1.0)) != 0)
			{
				rollerProgFail("did not boot at level 1 (index 0)");
			}
			else
			{
				SDL_Log("orkige_player: roller progression selfcheck - "
					"booted at level 1 of %d, solving...",
					levelManager.count());
				rollerProgPhase = RollerProgPhase::L1Slide;
				rollerProgStepFrame = 0;
			}
		}
	}
	// solve level 1: TAB into move mode, DOWN slides tile B into the
	// empty slot, TAB back to play (frame-scripted from the anchor)
	else if (rollerProgressionCheck && !rollerProgCheckFailed &&
		rollerProgPhase == RollerProgPhase::L1Slide)
	{
		if (rollerProgStepFrame == 0)
		{
			rollerProgStepFrame = frameCount;
		}
		const unsigned long step = frameCount - rollerProgStepFrame;
		if (step == 5) { pushKeyEvent(SDL_SCANCODE_TAB, SDLK_TAB, true); }
		if (step == 7) { pushKeyEvent(SDL_SCANCODE_TAB, SDLK_TAB, false); }
		if (step == 15)
		{
			if (rollerMode() != "move")
			{
				rollerProgFail("TAB did not enter move-world mode");
			}
			else
			{
				pushKeyEvent(SDL_SCANCODE_DOWN, SDLK_DOWN, true);
			}
		}
		if (step == 17) { pushKeyEvent(SDL_SCANCODE_DOWN, SDLK_DOWN, false); }
		if (step == 27)
		{
			if (rollerStat("slides", 0.0) < 1.0)
			{
				rollerProgFail("DOWN in move mode did not slide a tile");
			}
			else
			{
				pushKeyEvent(SDL_SCANCODE_TAB, SDLK_TAB, true);
			}
		}
		if (step == 29) { pushKeyEvent(SDL_SCANCODE_TAB, SDLK_TAB, false); }
		if (step == 35)
		{
			if (rollerMode() != "play")
			{
				rollerProgFail("TAB did not resume play mode");
			}
			else
			{
				pushKeyEvent(SDL_SCANCODE_RIGHT, SDLK_RIGHT, true);
				rollerProgPhase = RollerProgPhase::L1Roll;
				rollerProgDeadline = frameCount + 3600;
			}
		}
	}
	// hold RIGHT until the ball rolls onto the goal (level 1's win)
	else if (rollerProgressionCheck && !rollerProgCheckFailed &&
		rollerProgPhase == RollerProgPhase::L1Roll)
	{
		if (rollerStat("wins", 0.0) >= 1.0)
		{
			pushKeyEvent(SDL_SCANCODE_RIGHT, SDLK_RIGHT, false);
			SDL_Log("orkige_player: roller progression selfcheck - "
				"level 1 solved (slides=%.0f), waiting for the deferred "
				"switch to level 2", rollerStat("slides", -1.0));
			rollerProgPhase = RollerProgPhase::WaitSwitch;
			// the complete banner lingers ADVANCE_SECONDS before the
			// deferred load, then the level-2 scripts init - fat deadline
			rollerProgDeadline = frameCount + 1800;
		}
		else if (frameCount >= rollerProgDeadline)
		{
			rollerProgFail("holding RIGHT never reached the goal on "
				"level 1");
		}
	}
	// wait for the deferred scene switch to reach level 2
	else if (rollerProgressionCheck && !rollerProgCheckFailed &&
		rollerProgPhase == RollerProgPhase::WaitSwitch)
	{
		if (static_cast<int>(rollerStat("levelIndex", -1.0)) == 1)
		{
			std::error_code ignored;
			// progression rides the shared SaveStore now; saveProgress()
			// flushed it to that file on level complete
			const bool saveWritten = !saveStore.getSaveFile().empty() &&
				std::filesystem::exists(saveStore.getSaveFile(), ignored);
			if (!saveWritten)
			{
				rollerProgFail("the progression save file was not "
					"written on level complete ('" +
					saveStore.getSaveFile() + "')");
			}
			else if (levelManager.resumeLevel() != 1)
			{
				rollerProgFail("the resume level did not persist to 1 "
					"(got " + std::to_string(levelManager.resumeLevel()) +
					")");
			}
			else if (levelManager.bestMoves(0) < 0)
			{
				rollerProgFail("level 1's best moves were not recorded");
			}
			else if (!rollerFlag("gameReady") || !rollerFlag("ballReady"))
			{
				rollerProgFail("the level-2 scripts did not re-init "
					"after the switch");
			}
			else
			{
				SDL_Log("orkige_player: roller progression selfcheck - "
					"SWITCHED to level 2 (save written, resume=1, "
					"level 1 best=%d slides); solving level 2...",
					levelManager.bestMoves(0));
				pushKeyEvent(SDL_SCANCODE_RIGHT, SDLK_RIGHT, true);
				rollerProgPhase = RollerProgPhase::L2Roll;
				rollerProgDeadline = frameCount + 3600;
			}
		}
		else if (frameCount >= rollerProgDeadline)
		{
			rollerProgFail("the deferred load never switched to level 2 "
				"(levelIndex still " +
				std::to_string(static_cast<int>(
					rollerStat("levelIndex", -1.0))) + ")");
		}
	}
	// level 2 is the straight shot: hold RIGHT until the win
	else if (rollerProgressionCheck && !rollerProgCheckFailed &&
		rollerProgPhase == RollerProgPhase::L2Roll)
	{
		if (rollerStat("wins", 0.0) >= 1.0)
		{
			pushKeyEvent(SDL_SCANCODE_RIGHT, SDLK_RIGHT, false);
			SDL_Log("orkige_player: roller progression selfcheck "
				"complete - the level sequence, the deferred scene "
				"switch, the progression save AND the straight-shot "
				"level 2 win all verified");
			rollerProgPhase = RollerProgPhase::Done;
			running = false;
		}
		else if (frameCount >= rollerProgDeadline)
		{
			rollerProgFail("holding RIGHT never reached the goal on "
				"level 2 (the straight-shot level is unsolvable as "
				"scripted)");
		}
	}
	// ORKIGE_SPRITEBATCH_SELFCHECK (@see PlayerSelfChecks.h): the exact run
	// structure, the live escape hatch and a moved member's run re-upload.
	// The ctest driver pins the flavor-exact numbers from the printed lines.
	if (spriteBatchCheck && !spriteBatchDone)
	{
		Orkige::SpriteBatcher* batcher =
			Orkige::SpriteBatcher::getSingletonPtr();
		auto cvarOn = []() -> bool
		{
			return Orkige::CVarManager::getSingleton().getBool(
				"r.spriteBatching", true);
		};
		if (frameCount == 5)
		{
			spriteBatchStartedOn = cvarOn();
		}
		if (frameCount == 20)
		{
			// move a batched member: its run must re-upload and the frame-60
			// screenshot must show the sprite at the new place
			optr<Orkige::GameObject> probe =
				gameObjectManager.getGameObject("A4").lock();
			Orkige::TransformComponent* transform = probe
				? probe->getComponentPtr<Orkige::TransformComponent>()
				: nullptr;
			if (!transform || !batcher)
			{
				SDL_Log("orkige_player: spritebatch selfcheck FAILED - no "
					"A4 sprite or no batcher in the fixture run");
				exitCode = 1;
				running = false;
			}
			else
			{
				transform->setPosition(Orkige::Vec3(-0.25f, 0.05f, -8.05f));
			}
		}
		// MODE-OVER-WINDOW sampling (@see PlayerSelfChecks.h): the absolute
		// frame-stats batch count is driver-dependent and transients only ever
		// ADD batches, so each leg reads the most frequent count over a
		// 40-frame window instead of trusting any absolute number or frame
		// ordinal. The driver asserts the driver-independent contract from
		// the printed values (run count, on/off delta, live-off equality).
		auto histMode = [this]() -> std::pair<std::size_t, unsigned int>
		{
			std::size_t value = 0;
			unsigned int best = 0;
			for (auto const& entry : spriteBatchHist)
			{
				if (entry.second > best)
				{
					value = entry.first;
					best = entry.second;
				}
			}
			return { value, best };
		};
		auto histDump = [this]() -> std::string
		{
			std::string out;
			for (auto const& entry : spriteBatchHist)
			{
				out += std::to_string(entry.first) + ":" +
					std::to_string(entry.second) + " ";
			}
			return out;
		};
		const unsigned int minModeShare = 24;	// >= 60% of a 40-frame window
		if (frameCount >= 30 && frameCount < 70)
		{
			spriteBatchHist[render->getFrameStats().batchCount]++;
		}
		if (frameCount == 70)
		{
			const auto [mode, share] = histMode();
			if (share < minModeShare)
			{
				SDL_Log("orkige_player: spritebatch selfcheck FAILED - the "
					"baseline batch count never settled (histogram %s)",
					histDump().c_str());
				exitCode = 1;
				spriteBatchDone = true;
			}
			else
			{
				spriteBatchBatchesOn = mode;
				spriteBatchRunsOn = batcher ? batcher->activeRunCount() : 0;
				spriteBatchHist.clear();
				if (!spriteBatchStartedOn)
				{
					// the batching-off reference run: counts only, no toggle
					SDL_Log("orkige_player: spritebatch selfcheck complete - "
						"batching off, batches=%zu runs=%zu",
						spriteBatchBatchesOn, spriteBatchRunsOn);
					spriteBatchDone = true;
				}
				else
				{
					// the live escape hatch: runs must release without a
					// reboot (safely after the frame-60 screenshot, which
					// must capture the batched rendering)
					Orkige::CVarManager::getSingleton().setString(
						"r.spriteBatching", "0");
				}
			}
		}
		if (spriteBatchStartedOn && !spriteBatchDone)
		{
			if (frameCount >= 75 && frameCount < 115)
			{
				spriteBatchHist[render->getFrameStats().batchCount]++;
			}
			if (frameCount == 115)
			{
				const auto [mode, share] = histMode();
				if (share < minModeShare)
				{
					SDL_Log("orkige_player: spritebatch selfcheck FAILED - "
						"the live-off batch count never settled (histogram %s)",
						histDump().c_str());
					exitCode = 1;
					spriteBatchDone = true;
				}
				else
				{
					spriteBatchBatchesLiveOff = mode;
					spriteBatchHist.clear();
					Orkige::CVarManager::getSingleton().setString(
						"r.spriteBatching", "1");
				}
			}
			if (frameCount >= 120 && frameCount < 160)
			{
				spriteBatchHist[render->getFrameStats().batchCount]++;
			}
			if (frameCount == 160)
			{
				const auto [mode, share] = histMode();
				const std::size_t runsBack =
					batcher ? batcher->activeRunCount() : 0;
				if (share < minModeShare || mode != spriteBatchBatchesOn ||
					runsBack != spriteBatchRunsOn)
				{
					SDL_Log("orkige_player: spritebatch selfcheck FAILED - "
						"re-enabling did not restore the runs (batches %zu -> "
						"%zu, runs %zu -> %zu, histogram %s)",
						spriteBatchBatchesOn, mode, spriteBatchRunsOn, runsBack,
						histDump().c_str());
					exitCode = 1;
				}
				else
				{
					SDL_Log("orkige_player: spritebatch selfcheck complete - "
						"batching on, batches=%zu runs=%zu liveOffBatches=%zu "
						"restored", spriteBatchBatchesOn, spriteBatchRunsOn,
						spriteBatchBatchesLiveOff);
				}
				spriteBatchDone = true;
			}
		}
	}
	if (spriteBatchCheck && spriteBatchDone && frameCount > 60)
	{
		// verdict reached and the frame-60 screenshot is on disk - the run
		// has nothing left to prove, so end it instead of idling to the
		// frame cap (a software-GPU host pays real seconds per frame)
		running = false;
	}

	// ORKIGE_STATICMOVE_SELFCHECK (@see PlayerSelfChecks.h): a runtime move
	// of a STATIC object must land correctly through the backend repair
	// path. The ctest driver asserts the flavor-specific batch delta and
	// the one mobility-contract warning from the lines printed here.
	if (staticMoveCheck && !staticMoveDone)
	{
		if (frameCount == 30)
		{
			staticMoveBatchesBefore = render->getFrameStats().batchCount;
			optr<Orkige::GameObject> probe =
				gameObjectManager.getGameObject("Static3").lock();
			Orkige::TransformComponent* transform = probe
				? probe->getComponentPtr<Orkige::TransformComponent>()
				: nullptr;
			if (!transform || !transform->getStaticFlag())
			{
				SDL_Log("orkige_player: staticmove selfcheck FAILED - "
					"no static Static3 object in the fixture scene");
				exitCode = 1;
				running = false;
			}
			else
			{
				// the contract violation under test: reposition a static
				// object mid-run (warned once, then repaired)
				transform->setPosition(Orkige::Vec3(0.05f, 3.05f, -9.05f));
			}
		}
		if (frameCount == 60)
		{
			optr<Orkige::GameObject> probe =
				gameObjectManager.getGameObject("Static3").lock();
			Orkige::TransformComponent* transform = probe
				? probe->getComponentPtr<Orkige::TransformComponent>()
				: nullptr;
			const Orkige::Vec3 world = transform
				? transform->getWorldPosition()
				: Orkige::Vec3::ZERO;
			const std::size_t batchesAfter =
				render->getFrameStats().batchCount;
			if (!transform ||
				std::abs(world.x - 0.05f) > 0.001f ||
				std::abs(world.y - 3.05f) > 0.001f ||
				std::abs(world.z + 9.05f) > 0.001f)
			{
				SDL_Log("orkige_player: staticmove selfcheck FAILED - "
					"the move did not land (world %.3f %.3f %.3f)",
					world.x, world.y, world.z);
				exitCode = 1;
			}
			else
			{
				SDL_Log("orkige_player: staticmove selfcheck complete - "
					"moved Static3 to (0.05, 3.05, -9.05); batches "
					"before=%zu after=%zu",
					staticMoveBatchesBefore, batchesAfter);
			}
			staticMoveDone = true;
		}
	}

	if (rollerProgressionCheck && rollerProgCheckFailed)
	{
		exitCode = 1;
		running = false;
	}

	// --- tween selfcheck (see the block above the loop) -------------
	if (tweenCheck && !tweenCheckFailed && !tweenCheckDone)
	{
		const std::string scriptVerdict =
			Orkige::ScriptRuntime::getSingleton().getString(
				{"shared", "tween", "failed"}, "");
		if (!scriptVerdict.empty())
		{
			tweenFail("the script reported: " + scriptVerdict);
		}
		else if (tweenFlag("done"))
		{
			// the script says every tween ran its course - now verify
			// the world from the OUTSIDE
			Orkige::TransformComponent* tweener =
				gameObjectManager.getGameObject("Tweener").lock()
				? gameObjectManager.getGameObject("Tweener").lock()
					->getComponentPtr<Orkige::TransformComponent>()
				: nullptr;
			Orkige::SpriteComponent* sprite =
				gameObjectManager.getGameObject("Tweener").lock()
				&& gameObjectManager.getGameObject("Tweener").lock()
					->hasComponent<Orkige::SpriteComponent>()
				? gameObjectManager.getGameObject("Tweener").lock()
					->getComponentPtr<Orkige::SpriteComponent>()
				: nullptr;
			if (tweenStat("updates", 0.0) < 1.0)
			{
				tweenFail("tween.to never called its onUpdate closure");
			}
			else if (tweenStat("completes", 0.0) != 1.0)
			{
				tweenFail("onComplete did not fire exactly once");
			}
			else if (!tweener ||
				std::abs(tweener->getPosition().x - 3.0f) > 0.001f)
			{
				tweenFail("the tween.to closure did not land the "
					"Tweener on x=3 exactly");
			}
			else if (!sprite ||
				std::abs(sprite->getTint().a - 0.25f) > 0.001f)
			{
				tweenFail("tween.fade did not land the sprite tint "
					"alpha on 0.25");
			}
			else if (std::abs(soundManager.getGroupVolume("music") -
				0.25f) > 0.001f)
			{
				tweenFail("tween.volume did not land the music group "
					"volume on 0.25 (the ducking recipe channel)");
			}
			else if (std::abs(soundManager.getMasterVolume() - 0.8f) >
				0.001f)
			{
				tweenFail("the manifest Setting audio.master=0.8 was "
					"not applied on project load");
			}
			else if (std::abs(soundManager.getGroupVolume("hud") -
				0.4f) > 0.001f)
			{
				tweenFail("the manifest Setting audio.group.hud=0.4 "
					"was not applied on project load");
			}
			else if (tweenFlag("soundChecked") &&
				(!soundManager.getSound("beep") ||
					std::abs(soundManager.getSound("beep")
						->getEffectiveGain() - 0.15f) > 0.001f))
			{
				tweenFail("the beep's effective gain is not 0.15 "
					"(base 0.6 x tweened music group 0.25)");
			}
			else
			{
				SDL_Log("orkige_player: tween selfcheck complete - "
					"closure tween.to (x=%.3f, %0.f updates, "
					"onComplete once), fade (alpha=%.3f), volume "
					"(music=%.3f), cancel, delay and the manifest "
					"mixer settings all verified",
					tweener->getPosition().x,
					tweenStat("updates", -1.0),
					sprite->getTint().a,
					soundManager.getGroupVolume("music"));
				tweenCheckDone = true;
				running = false;
			}
		}
		else if (frameCount >= 900)
		{
			// fat deadline: ~2.2s of simulated tween time must have
			// long finished after 900 floored-dt frames (15s sim)
			tweenFail("the script never reported done");
		}
	}
	if (tweenCheck && tweenCheckFailed)
	{
		exitCode = 1;
		running = false;
	}

	// --- hot-reload selfcheck (see the block above the loop) ---------
	if (hotreloadCheck && !hotreloadCheckFailed &&
		hotreloadPhase == HotReloadPhase::Boot && frameCount == 5)
	{
		Orkige::ScriptComponent* script = hotreloadProbeScript();
		Orkige::TransformComponent* transform =
			hotreloadProbeTransform();
		if (!script)
		{
			hotreloadFail("no Probe object with a ScriptComponent");
		}
		else if (script->hasScriptError())
		{
			hotreloadFail("script error: " + script->getScriptError());
		}
		else if (!script->isScriptStarted())
		{
			hotreloadFail("script never loaded/started");
		}
		else if (hotreloadNumber("value", -1.0) != 1.0)
		{
			hotreloadFail("variant A did not publish value == 1");
		}
		else if (!transform)
		{
			hotreloadFail("no Probe TransformComponent");
		}
		else
		{
			// save the committed bytes for restoration on exit
			hotreloadScriptPath = Orkige::ScriptRuntime::getSingleton()
				.resolveScriptPath(script->getScriptFile());
			std::ifstream original(hotreloadScriptPath,
				std::ios::binary);
			hotreloadOriginal.assign(
				std::istreambuf_iterator<char>(original),
				std::istreambuf_iterator<char>());
			if (hotreloadScriptPath.empty() ||
				hotreloadOriginal.empty())
			{
				hotreloadFail("could not read the fixture script from "
					"disk");
			}
			else
			{
				// engine-side state that the swap must NOT reset (the
				// new init does not touch the transform)
				transform->setPosition(Orkige::Vec3(7.0f, 0.0f, 0.0f));
				SDL_Log("orkige_player: hotreload selfcheck - variant A "
					"up (value=1), transform parked at x=7");
			}
		}
	}
	else if (hotreloadCheck && !hotreloadCheckFailed &&
		hotreloadPhase == HotReloadPhase::Boot && frameCount == 10)
	{
		// (1) overwrite with variant B (value = 2), (2) hot-swap
		if (!hotreloadWriteScript(
			"function init(self)\n"
			"\tshared.hotreload = shared.hotreload or {}\n"
			"\tshared.hotreload.value = 2\n"
			"\tshared.hotreload.inits = "
				"(shared.hotreload.inits or 0) + 1\n"
			"end\n"
			"function update(self, dt)\n"
			"\tshared.hotreload.ticks = "
				"(shared.hotreload.ticks or 0) + 1\n"
			"end\n"))
		{
			hotreloadFail("could not write variant B to disk");
		}
		else
		{
			Orkige::ScriptComponent* script = hotreloadProbeScript();
			Orkige::TransformComponent* transform =
				hotreloadProbeTransform();
			script->hotReload();
			if (script->hasReloadError())
			{
				hotreloadFail("a valid variant B failed to reload: " +
					script->getLastReloadError());
			}
			else if (script->hasScriptError())
			{
				hotreloadFail("the swap disabled the instance "
					"(mFailed set)");
			}
			else if (hotreloadNumber("value", -1.0) != 2.0)
			{
				hotreloadFail("the running behavior did not change to "
					"value == 2 after the swap");
			}
			else if (hotreloadNumber("inits", -1.0) != 2.0)
			{
				hotreloadFail("the new init did not run (inits != 2)");
			}
			else if (!transform || std::abs(
				transform->getPosition().x - 7.0f) > 0.001f)
			{
				hotreloadFail("the engine-side transform did NOT "
					"survive the swap (x != 7)");
			}
			else
			{
				SDL_Log("orkige_player: hotreload selfcheck - swap OK "
					"(value 1->2, re-init ran, transform x=7 persisted)");
				hotreloadPhase = HotReloadPhase::AfterSwap;
			}
		}
	}
	else if (hotreloadCheck && !hotreloadCheckFailed &&
		hotreloadPhase == HotReloadPhase::AfterSwap && frameCount == 15)
	{
		// NEGATIVE: overwrite with broken Lua and hot-swap - the old
		// (variant B) instance must keep running, error non-fatal
		hotreloadTicksAtBreak = hotreloadNumber("ticks", -1.0);
		if (!hotreloadWriteScript("this is not valid lua ((\n"))
		{
			hotreloadFail("could not write the broken variant to disk");
		}
		else
		{
			Orkige::ScriptComponent* script = hotreloadProbeScript();
			script->hotReload();
			if (!script->hasReloadError())
			{
				hotreloadFail("a broken script reloaded without an "
					"error");
			}
			else if (script->hasScriptError())
			{
				hotreloadFail("the failed reload wrongly disabled the "
					"instance (mFailed set - it must stay alive)");
			}
			else if (hotreloadNumber("value", -1.0) != 2.0)
			{
				hotreloadFail("the failed reload clobbered the running "
					"instance (value != 2)");
			}
			else
			{
				SDL_Log("orkige_player: hotreload selfcheck - broken "
					"edit contained (reload error surfaced, old instance "
					"kept, mFailed false)");
				hotreloadPhase = HotReloadPhase::AfterBreak;
			}
		}
	}
	else if (hotreloadCheck && !hotreloadCheckFailed &&
		hotreloadPhase == HotReloadPhase::AfterBreak && frameCount == 20)
	{
		// the surviving instance kept ticking across the failed reload
		if (hotreloadNumber("ticks", -1.0) <= hotreloadTicksAtBreak)
		{
			hotreloadFail("the surviving instance stopped updating "
				"after the failed reload");
		}
		else
		{
			SDL_Log("orkige_player: hotreload selfcheck complete - "
				"compile-before-swap (behavior change + engine-side "
				"state persistence) and broken-edit containment both "
				"verified");
			hotreloadPhase = HotReloadPhase::Done;
			running = false;
		}
	}
	if (hotreloadCheck && hotreloadCheckFailed)
	{
		exitCode = 1;
		running = false;
	}

	if (scriptPropCheck && !scriptPropCheckFailed &&
		scriptPropPhase == ScriptPropPhase::Boot && frameCount == 5)
	{
		Orkige::ScriptComponent* script = moverScript();
		if (!script)
		{
			scriptPropFail("no Mover object with a ScriptComponent");
		}
		else if (script->hasScriptError())
		{
			scriptPropFail("script error: " + script->getScriptError());
		}
		else if (!script->isScriptStarted())
		{
			scriptPropFail("the script never loaded/started");
		}
		else if (std::abs(
			scriptPropNumber("injectedSpeed", -1.0) - 5.0) > 0.001)
		{
			// the script's OWN default is 1; seeing 5 proves the
			// serialized per-instance value was loaded AND injected onto
			// self before init
			scriptPropFail("the serialized moveSpeed=5 was NOT injected "
				"before init");
		}
		else if (scriptPropNumber("x", 0.0) <=
			scriptPropNumber("startX", 0.0))
		{
			scriptPropFail("the script did not move the transform");
		}
		else
		{
			SDL_Log("orkige_player: scriptprop selfcheck - export "
				"injected before init (moveSpeed=5), transform moving");
			scriptPropPhase = ScriptPropPhase::Moving;
		}
	}
	else if (scriptPropCheck && !scriptPropCheckFailed &&
		scriptPropPhase == ScriptPropPhase::Moving && frameCount == 15)
	{
		const double x = scriptPropNumber("x", 0.0);
		const double elapsed = scriptPropNumber("elapsed", 0.0);
		Orkige::ScriptComponent* script = moverScript();
		if (elapsed <= 0.0)
		{
			scriptPropFail("no elapsed time accumulated");
		}
		else if (std::abs(x / elapsed - 5.0) > 0.25)
		{
			scriptPropFail("the script did not move at the injected "
				"speed (x/elapsed != 5)");
		}
		else if (!script)
		{
			scriptPropFail("lost the Mover ScriptComponent");
		}
		else
		{
			// flip moveSpeed live through the REFLECTED setter - the
			// exact path a debug-protocol set_property / MCP
			// set_component write takes to a dynamic export property
			const Orkige::PropertySchema schema =
				Orkige::getComponentSchema(*script);
			Orkige::PropertyDesc const* desc = schema.find("moveSpeed");
			if (!desc || desc->isReadOnly())
			{
				scriptPropFail("moveSpeed not writable through the "
					"reflected schema");
			}
			else
			{
				desc->set(script,
					Orkige::PropertyValue::makeFloat(0.0));
				scriptPropXAtFreeze = scriptPropNumber("x", 0.0);
				SDL_Log("orkige_player: scriptprop selfcheck - moved at "
					"speed 5, set moveSpeed->0 through the reflected "
					"setter");
				scriptPropPhase = ScriptPropPhase::Frozen;
			}
		}
	}
	else if (scriptPropCheck && !scriptPropCheckFailed &&
		scriptPropPhase == ScriptPropPhase::Frozen && frameCount == 25)
	{
		if (std::abs(scriptPropNumber("x", -999.0) -
			scriptPropXAtFreeze) > 0.05)
		{
			scriptPropFail("the live moveSpeed->0 did not reach the "
				"script (x kept moving)");
		}
		else
		{
			SDL_Log("orkige_player: scriptprop selfcheck complete - "
				"export injected-before-init, behaved, serialized "
				"per-instance, live reflected set reached self.moveSpeed");
			scriptPropPhase = ScriptPropPhase::Done;
			running = false;
		}
	}
	if (scriptPropCheck && scriptPropCheckFailed)
	{
		exitCode = 1;
		running = false;
	}

	// --- integration: contact + tags + input action (see the block
	// above the loop). Condition-driven with a fat frame ceiling: the
	// physics drop takes a variable number of frames, so every wait is a
	// poll-until, never a fixed frame.
	if (integrationContactCheck && !integContactFailed &&
		integContactPhase != IntegContactPhase::Done)
	{
		if (integContactPhase == IntegContactPhase::VerifyTags)
		{
			if (integContactNum("found", -1.0) >= 0.0 && frameCount >= 3)
			{
				if (integContactNum("found", -1.0) != 1.0)
				{
					integContactFail("world.findByTag(\"goal\") did not "
						"find exactly one goal");
				}
				else if (integContactStr("foundGoal") != "Goal")
				{
					integContactFail("the tag-discovered goal was not "
						"the 'Goal' object");
				}
				else
				{
					// fire the named "jump" action: hold SPACE so the
					// action layer samples a down edge (released below)
					pushKeyEvent(SDL_SCANCODE_SPACE, SDLK_SPACE, true);
					integContactInputFrame = frameCount;
					integContactPhase = IntegContactPhase::DriveInput;
					SDL_Log("orkige_player: integration contact "
						"selfcheck - goal found by tag; injecting "
						"the 'jump' action");
				}
			}
			else if (frameCount > 600)
			{
				integContactFail("the ball script never published its "
					"tag discovery");
			}
		}
		else if (integContactPhase == IntegContactPhase::DriveInput)
		{
			if (frameCount == integContactInputFrame + 5)
			{
				pushKeyEvent(SDL_SCANCODE_SPACE, SDLK_SPACE, false);
			}
			if (integContactNum("input", 0.0) >= 1.0)
			{
				integContactPhase = IntegContactPhase::AwaitContact;
			}
			else if (frameCount > integContactInputFrame + 300)
			{
				integContactFail("the injected 'jump' action never "
					"reached the ball script");
			}
		}
		else if (integContactPhase == IntegContactPhase::AwaitContact)
		{
			if (integContactNum("contact", 0.0) >= 1.0)
			{
				if (integContactStr("contactOther") != "Goal")
				{
					integContactFail("onContactBegin fired for a body "
						"other than the tagged goal");
				}
				// the bespoke onContactBegin fired THIS or an earlier
				// frame; the physics.contactBegin BUS mirror is emitted at
				// the contact drain (after the script phase), so it is
				// delivered one frame later. Wait for it, then assert the
				// mirror count matches the bespoke count (additive, never
				// a replacement).
				else if (integContactNum("busContact", 0.0) >= 1.0)
				{
					if (integContactNum("busContact", 0.0) !=
						integContactNum("contact", 0.0))
					{
						integContactFail("the physics.contactBegin bus "
							"mirror count did not match the onContactBegin "
							"hook count");
					}
					else
					{
						SDL_Log("orkige_player: integration contact "
							"selfcheck complete - tag lookup + 'jump' "
							"action + physics drop + goal-sensor "
							"onContactBegin AND the physics.contactBegin "
							"bus mirror all fired");
						integContactPhase = IntegContactPhase::Done;
						running = false;
					}
				}
				else if (frameCount > integContactInputFrame + 950)
				{
					integContactFail("the physics.contactBegin bus mirror "
						"was never delivered after onContactBegin");
				}
			}
			else if (frameCount > integContactInputFrame + 900)
			{
				integContactFail("the input-driven ball never contacted "
					"the goal sensor");
			}
		}
	}
	if (integrationContactCheck && integContactFailed)
	{
		exitCode = 1;
		running = false;
	}

	// --- integration: level switch with a live tween + particles (see
	// the block above the loop). Condition-driven throughout.
	if (integrationLevelCheck && !integLevelFailed &&
		integLevelPhase != IntegLevelPhase::Done)
	{
		if (integLevelPhase == IntegLevelPhase::ObserveA)
		{
			// director.lua's init published aliveA + moverStartX and
			// kicked off the tween + burst; confirm the tween is
			// advancing Mover AND the emitter has live particles, both
			// mid-flight (before the ~0.3s deferred switch)
			const double startX = integLevelNum("moverStartX", -999.0);
			const double moverX = integLevelNum("moverX", -999.0);
			if (integLevelNum("levelBBooted", 0.0) >= 1.0)
			{
				integLevelFail("the level switched before the live "
					"tween + particles could be observed on level A");
			}
			else if (integLevelNum("aliveA", 0.0) >= 1.0 &&
				startX > -900.0 && moverX > startX + 0.02 &&
				integFxLiveCount() > 0)
			{
				SDL_Log("orkige_player: integration levelswitch "
					"selfcheck - level A live (tween moved Mover to "
					"%.3f, %d particles alive)", moverX,
					integFxLiveCount());
				integLevelPhase = IntegLevelPhase::AwaitSwitch;
			}
			else if (frameCount > 600)
			{
				integLevelFail("never observed a live tween + particles "
					"on level A");
			}
		}
		else if (integLevelPhase == IntegLevelPhase::AwaitSwitch)
		{
			if (integLevelNum("levelBBooted", 0.0) >= 1.0)
			{
				integLevelTicksAtEntry =
					integLevelNum("levelBTicks", 0.0);
				integLevelVerifyFrame = frameCount;
				integLevelPhase = IntegLevelPhase::VerifyB;
			}
			else if (frameCount > 600)
			{
				integLevelFail("the deferred level switch never applied "
					"(level B never booted)");
			}
		}
		else if (integLevelPhase == IntegLevelPhase::VerifyB)
		{
			const bool moverGone =
				gameObjectManager.getGameObject("Mover").lock() ==
				nullptr;
			if (!moverGone)
			{
				integLevelFail("the old level's Mover survived the "
					"switch (scene teardown did not clear it)");
			}
			else if (integLevelNum("carrySeen", -1.0) != 4242.0)
			{
				integLevelFail("the shared table did not survive the "
					"switch (carry marker lost)");
			}
			else if (integLevelNum("levelBTicks", 0.0) >
				integLevelTicksAtEntry)
			{
				SDL_Log("orkige_player: integration levelswitch "
					"selfcheck complete - deferred switch tore down a "
					"live tween + emitter, shared state survived and "
					"level B ticks");
				integLevelPhase = IntegLevelPhase::Done;
				running = false;
			}
			else if (frameCount > integLevelVerifyFrame + 300)
			{
				integLevelFail("level B booted but never ticked");
			}
		}
	}
	if (integrationLevelCheck && integLevelFailed)
	{
		exitCode = 1;
		running = false;
	}

	if (frameCount == 60)
	{
		// ORKIGE_DEMO_SCREENSHOT: dump the framebuffer for automated
		// visual verification
		if (const char* shotPath = std::getenv("ORKIGE_DEMO_SCREENSHOT"))
		{
			render->saveWindowContents(shotPath);
		}
	}
	// ORKIGE_DEMO_SCREENSHOT2: an OPTIONAL second capture at a later frame
	// (ORKIGE_DEMO_SHOT2_FRAME, default 105) - a driver that needs two frames
	// APART IN TIME from one boot (e.g. the motion-not-magnitude anim probe:
	// an animated band must CHANGE while a static reference band holds)
	if (const char* shot2Path = std::getenv("ORKIGE_DEMO_SCREENSHOT2"))
	{
		unsigned long shot2Frame = 105;
		if (const char* f = std::getenv("ORKIGE_DEMO_SHOT2_FRAME"))
		{
			shot2Frame = std::strtoul(f, nullptr, 10);
		}
		if (frameCount == shot2Frame)
		{
			render->saveWindowContents(shot2Path);
		}
	}
	// ORKIGE_FADE_SELFCHECK: drive a fade-out -> scene switch -> fade-in
	// and observe the overlay alpha and the switch through the real loop
	if (fadeCheck)
	{
		if (frameCount == 5)
		{
			// the exact coordination the Lua screen.loadScene does: the
			// deferred switch is requested at full opacity
			screenFade.transition(0.2f, 0.2f, [&levelManager]()
			{
				levelManager.loadScenePath("scenes/sceneB.oscene");
			});
			fadeStarted = true;
		}
		if (fadeStarted)
		{
			fadeMaxAlpha = std::max(fadeMaxAlpha, screenFade.getAlpha());
			const bool markerBHere =
				gameObjectManager.getGameObject("MarkerB").lock() != nullptr;
			if (markerBHere && !fadeSwitched)
			{
				fadeSwitched = true;
				fadeAlphaAtSwitch = screenFade.getAlpha();
			}
			if (fadeSwitched && !screenFade.isFading() &&
				screenFade.getAlpha() < 0.02f)
			{
				fadeSawClearAfterSwitch = true;
			}
		}
		if (frameCount == 90)
		{
			bool ok = true;
			std::string detail;
			if (!fadeStarted || fadeMaxAlpha < 0.9f)
			{
				ok = false;
				detail = "overlay never reached opacity (max alpha " +
					std::to_string(fadeMaxAlpha) + ")";
			}
			else if (!fadeSwitched)
			{
				ok = false;
				detail = "scene never switched to sceneB";
			}
			else if (fadeAlphaAtSwitch < 0.5f)
			{
				ok = false;
				detail = "scene switched while the screen was not covered "
					"(alpha " + std::to_string(fadeAlphaAtSwitch) + ")";
			}
			else if (!fadeSawClearAfterSwitch)
			{
				ok = false;
				detail = "fade never cleared after the switch";
			}
			SDL_Log("orkige_player: FADE SELFCHECK %s%s%s",
				ok ? "PASSED" : "FAILED", ok ? "" : " - ", detail.c_str());
			exitCode = ok ? 0 : 1;
			running = false;
		}
	}
	// ORKIGE_BREADCRUMB_SELFCHECK: by now (a dozen frames) the Crasher's
	// init has run, failed, and been recorded; read the trail off disk
	// and assert the script_error line is there with the object id
	if (breadcrumbCheck && frameCount == 15)
	{
		bool ok = true;
		std::string detail;
		const std::string file =
			Orkige::Breadcrumbs::getSingleton().getFile();
		Orkige::String trail;
		if (file.empty() || !Orkige::Breadcrumbs::loadFile(file, trail))
		{
			ok = false;
			detail = "breadcrumbs.jsonl not written";
		}
		else if (trail.find("\"script_error\"") == std::string::npos ||
			trail.find("Crasher") == std::string::npos)
		{
			ok = false;
			detail = "no script_error breadcrumb for the Crasher object";
		}
		else if (trail.find("\"boot\"") == std::string::npos)
		{
			ok = false;
			detail = "boot marker missing";
		}
		SDL_Log("orkige_player: BREADCRUMB SELFCHECK %s%s%s",
			ok ? "PASSED" : "FAILED", ok ? "" : " - ", detail.c_str());
		exitCode = ok ? 0 : 1;
		running = false;
	}
	// ORKIGE_LIFECYCLE_SELFCHECK: drive the real player wiring with
	// synthetic SDL lifecycle events (SDL_PushEvent - processed at the
	// top of the NEXT iteration, exactly like a device's events) and
	// assert the backgrounding contract across a few frames.
	if (perfCheck && !perfCheckFailed && frameCount == 60)
	{
		auto perfFail = [&](std::string const& what)
		{
			SDL_Log("orkige_player: PERF SELFCHECK FAILED - %s "
				"(frame %lu)", what.c_str(), frameCount);
			perfCheckFailed = true;
			exitCode = 1;
			running = false;
		};
		// the frame boundary folded every frame so far
		if (Orkige::MemoryManager::framesSampled() < 50 ||
			Orkige::ProfileManager::framesSampled() < 50)
		{
			perfFail("frame boundaries never folded the instruments");
		}
		else if (Orkige::ProfileManager::lastFrameMilliseconds() <= 0.0)
		{
			perfFail("no frame duration was measured");
		}
		else
		{
			// the canonical tick phases all appear as depth-0 scopes
			std::vector<Orkige::ProfileManager::SnapshotNode> rows;
			Orkige::ProfileManager::snapshot(rows);
			const char* const requiredPhases[] = { "input", "scripts",
				"events", "tweens", "load", "audio", "present",
				"debug", "render" };
			for (const char* phase : requiredPhases)
			{
				bool found = false;
				for (Orkige::ProfileManager::SnapshotNode const& row :
					rows)
				{
					if (row.depth == 0 &&
						std::strcmp(row.name, phase) == 0)
					{
						found = true;
						break;
					}
				}
				if (!found)
				{
					perfFail(std::string("phase scope missing from "
						"the snapshot: ") + phase);
					break;
				}
			}
			if (!perfCheckFailed)
			{
				// the deliverable: the measured breakdown, logged
				SDL_Log("orkige_player: PERF SELFCHECK - frame %.3f ms, "
					"alloc/frame %zu (peak %zu), %zu profile rows",
					Orkige::ProfileManager::lastFrameMilliseconds(),
					Orkige::MemoryManager::lastFrameTotal(),
					Orkige::MemoryManager::peakFrameTotal(),
					rows.size());
				for (Orkige::ProfileManager::SnapshotNode const& row :
					rows)
				{
					if (row.depth == 0)
					{
						SDL_Log("orkige_player:   %s: %.3f ms (x%u)",
							row.name, row.milliseconds, row.calls);
					}
				}
				SDL_Log("orkige_player: PERF SELFCHECK COMPLETE");
				running = false;	// clean exit, code 0
			}
		}
	}

	// ORKIGE_BENCHMARK_SELFCHECK: the scene has ticked for ~90 frames with
	// the recorder armed; finalize the artifact and assert it exists and
	// parses with a meta line, at least one scene record with frames>0 and
	// a measured frameMs.avg, and a clean (non-aborted) summary.
	if (benchmarkCheck && frameCount == 90)
	{
		bool ok = true;
		std::string detail;
		if (!benchmarkRecorder.isArmed())
		{
			ok = false;
			detail = "recorder not armed (ORKIGE_BENCHMARK unset?)";
		}
		else
		{
			// flush meta + the open scene's record + the summary
			benchmarkRecorder.finish(false);
			const std::string file = benchmarkRecorder.getFile();
			std::ifstream artifact(file.c_str(), std::ios::binary);
			std::ostringstream buffer;
			buffer << artifact.rdbuf();
			const std::string text = buffer.str();
			if (text.empty())
			{
				ok = false;
				detail = "results artifact not written";
			}
			else
			{
				bool sawMeta = false;
				bool sawScene = false;
				bool sawSummary = false;
				std::istringstream lineStream(text);
				std::string jsonLine;
				while (ok && std::getline(lineStream, jsonLine))
				{
					if (jsonLine.empty())
					{
						continue;
					}
					Orkige::JsonValue value;
					if (!Orkige::JsonValue::parse(jsonLine, value) ||
						!value.isObject())
					{
						ok = false;
						detail = "artifact line is not valid JSON";
						break;
					}
					const Orkige::String kind =
						value.get("type").asString();
					if (kind == "meta")
					{
						sawMeta = true;
						if (value.get("flavor").asString().empty() ||
							value.get("renderSystem").asString().empty())
						{
							ok = false;
							detail = "meta line missing flavor/renderSystem";
						}
					}
					else if (kind == "scene")
					{
						sawScene = true;
						if (value.get("frames").asInt() <= 0)
						{
							ok = false;
							detail = "scene record has no frames";
						}
						else if (value.get("frameMs").get("avg")
							.asNumber() <= 0.0)
						{
							ok = false;
							detail = "scene record has no frameMs.avg";
						}
					}
					else if (kind == "summary")
					{
						sawSummary = true;
						if (value.get("aborted").asBool())
						{
							ok = false;
							detail = "summary marked aborted";
						}
					}
				}
				if (ok && (!sawMeta || !sawScene || !sawSummary))
				{
					ok = false;
					detail = "artifact missing a meta/scene/summary line";
				}
			}
		}
		SDL_Log("orkige_player: BENCHMARK SELFCHECK %s%s%s",
			ok ? "PASSED" : "FAILED", ok ? "" : " - ", detail.c_str());
		exitCode = ok ? 0 : 1;
		running = false;
	}

	if (lifecycleCheck && !lifecycleFailed)
	{
		auto lifecycleFail = [&](std::string const& what)
		{
			SDL_Log("orkige_player: LIFECYCLE SELFCHECK FAILED - %s "
				"(frame %lu)", what.c_str(), frameCount);
			lifecycleFailed = true;
			exitCode = 1;
			running = false;
		};
		auto crumbSeen = [](char const* kind) -> bool
		{
			const std::string needle = std::string("\"") + kind + "\"";
			return Orkige::Breadcrumbs::getSingleton().contents()
				.find(needle) != std::string::npos;
		};
		if (lifecyclePhase == LifecyclePhase::Init && frameCount == 5)
		{
			// the App script must be up - its onAppPause/onAppResume
			// hooks are what this selfcheck exercises
			optr<Orkige::GameObject> app =
				gameObjectManager.getGameObject("App").lock();
			Orkige::ScriptComponent* script = (app &&
				app->hasComponent<Orkige::ScriptComponent>()) ?
				app->getComponentPtr<Orkige::ScriptComponent>() : nullptr;
			if (!script || !script->isScriptStarted() ||
				script->hasScriptError())
			{
				lifecycleFail("App script never started cleanly");
			}
			else
			{
				SDL_Event evt{};
				evt.type = SDL_EVENT_WILL_ENTER_BACKGROUND;
				SDL_PushEvent(&evt);
				evt.type = SDL_EVENT_DID_ENTER_BACKGROUND;
				SDL_PushEvent(&evt);
				lifecyclePhase = LifecyclePhase::Backgrounded;
			}
		}
		else if (lifecyclePhase == LifecyclePhase::Backgrounded &&
			frameCount == 8)
		{
			// the background contract: sim gated, rendering stopped, the
			// save FLUSHED (dirty cleared) with the value onAppPause wrote
			// persisted, a "background" breadcrumb recorded
			if (!lifecycle.isSimPaused())
			{
				lifecycleFail("sim not paused after background");
			}
			else if (!lifecycle.isRenderingStopped())
			{
				lifecycleFail("rendering not stopped after background");
			}
			else if (saveStore.getNumber("lifecycle.pauses", -1.0) < 1.0)
			{
				lifecycleFail("onAppPause did not run (no saved value)");
			}
			else if (saveStore.isDirty())
			{
				lifecycleFail("save store not flushed on background");
			}
			else if (!crumbSeen("background"))
			{
				lifecycleFail("no background breadcrumb");
			}
			else
			{
				SDL_Event evt{};
				evt.type = SDL_EVENT_WILL_ENTER_FOREGROUND;
				SDL_PushEvent(&evt);
				evt.type = SDL_EVENT_DID_ENTER_FOREGROUND;
				SDL_PushEvent(&evt);
				lifecyclePhase = LifecyclePhase::Foregrounded;
			}
		}
		else if (lifecyclePhase == LifecyclePhase::Foregrounded &&
			frameCount == 11)
		{
			// the foreground contract: sim resumed running, rendering
			// back, onAppResume fired, a "foreground" breadcrumb recorded
			if (lifecycle.isSimPaused())
			{
				lifecycleFail("sim still paused after foreground");
			}
			else if (lifecycle.isRenderingStopped())
			{
				lifecycleFail("rendering still stopped after foreground");
			}
			else if (saveStore.getNumber("lifecycle.resumes", -1.0) < 1.0)
			{
				lifecycleFail("onAppResume did not run (no saved value)");
			}
			else if (!crumbSeen("foreground"))
			{
				lifecycleFail("no foreground breadcrumb");
			}
			else
			{
				SDL_Log("orkige_player: LIFECYCLE SELFCHECK PASSED");
				lifecyclePhase = LifecyclePhase::Done;
				exitCode = 0;
				running = false;
			}
		}
	}

	// ORKIGE_RESIZE_SELFCHECK: the window-resize plumbing, end to end.
	// Request a smaller host window (SDL_SetWindowSize - the resize event
	// then takes the real poll-loop path, exactly like a device rotation's
	// drawable change) and require the render system to report the new
	// drawable size with the window camera's aspect re-derived to match.
	if (resizeCheck && !resizeCheckFailed)
	{
		auto resizeFail = [&](std::string const& what)
		{
			SDL_Log("orkige_player: RESIZE SELFCHECK FAILED - %s "
				"(frame %lu)", what.c_str(), frameCount);
			resizeCheckFailed = true;
			exitCode = 1;
			running = false;
		};
		// the window camera's aspect, read back through its projection:
		// proj[1][1] / proj[0][0] equals width/height for both projection
		// types (and both render flavors - the facade contract)
		auto cameraAspect = [&render]() -> double
		{
			optr<Orkige::RenderCamera> camera = render->getWindowCamera();
			if (!camera)
			{
				return 0.0;
			}
			const Orkige::Mat4 projection = camera->getProjectionMatrix();
			return projection[0][0] != 0.0f
				? static_cast<double>(projection[1][1]) /
					static_cast<double>(projection[0][0])
				: 0.0;
		};
		if (resizePhase == ResizePhase::Baseline && frameCount == 5)
		{
			// a maximized or fullscreen window ignores programmatic size
			// requests (a small hosted-runner desktop maximizes the player
			// at creation); force it windowed and restored, and SYNC - both
			// operations are asynchronous window-manager round trips on some
			// platforms, so without the sync the size request below can race
			// them and get swallowed
			SDL_Window* window = context.host.getWindow();
			SDL_SetWindowFullscreen(window, false);
			SDL_RestoreWindow(window);
			SDL_SyncWindow(window);
		}
		if (resizePhase == ResizePhase::Baseline && frameCount == 10)
		{
			render->getWindowSize(resizeBaselineW, resizeBaselineH);
			const double aspect = cameraAspect();
			// coherence gate: on a host whose window manager clamped the
			// real window without SDL's bookkeeping following (a hosted
			// runner's tiny desktop: SDL holds the requested logical size
			// while the drawable is the clamped one), no size request this
			// probe makes can round-trip - that is the HARNESS unable to
			// express window geometry, not the resize path failing. Skip
			// with the evidence; every coherent host stays a hard gate.
			{
				int logicalW = 0;
				int logicalH = 0;
				SDL_GetWindowSize(context.host.getWindow(), &logicalW,
					&logicalH);
				const double pixelDensity = SDL_GetWindowPixelDensity(
					context.host.getWindow());
				const double expectedW = logicalW * pixelDensity;
				const double expectedH = logicalH * pixelDensity;
				if (expectedW > 0.0 && expectedH > 0.0 &&
					(std::abs(resizeBaselineW - expectedW) > 0.05 * expectedW ||
					 std::abs(resizeBaselineH - expectedH) > 0.05 * expectedH))
				{
					SDL_Log("orkige_player: RESIZE SELFCHECK SKIPPED - the "
						"window system holds %ux%u while SDL believes "
						"%dx%d at density %.2f; this host cannot express "
						"window geometry faithfully",
						resizeBaselineW, resizeBaselineH, logicalW, logicalH,
						pixelDensity);
					resizePhase = ResizePhase::Done;
					exitCode = 77;
					running = false;
					return;
				}
			}
			if (resizeBaselineW == 0 || resizeBaselineH == 0)
			{
				resizeFail("no drawable at the baseline frame");
			}
			else if (std::abs(aspect - static_cast<double>(resizeBaselineW) /
				static_cast<double>(resizeBaselineH)) > 0.02)
			{
				resizeFail("window camera aspect does not match the "
					"baseline drawable");
			}
			else
			{
				// halve the LOGICAL width (the drawable follows at the
				// display's own pixel density) - a clear aspect change a
				// window manager never clamps, in the same direction a
				// portrait rotation moves it
				SDL_Window* window = context.host.getWindow();
				int logicalW = 0;
				int logicalH = 0;
				SDL_GetWindowSize(window, &logicalW, &logicalH);
				SDL_SetWindowSize(window, logicalW / 2, logicalH);
				SDL_SyncWindow(window);
				// the diagnostic burst a failure needs to be conclusive from
				// a CI log alone: the window state words, the logical size
				// the request landed on, and the drawable the renderer sees
				int syncedW = 0;
				int syncedH = 0;
				SDL_GetWindowSize(window, &syncedW, &syncedH);
				const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
				SDL_Log("orkige_player: resize selfcheck - requested %dx%d "
					"(was %dx%d), synced logical %dx%d, flags%s%s%s%s",
					logicalW / 2, logicalH, logicalW, logicalH,
					syncedW, syncedH,
					(flags & SDL_WINDOW_FULLSCREEN) ? " fullscreen" : "",
					(flags & SDL_WINDOW_MAXIMIZED) ? " maximized" : "",
					(flags & SDL_WINDOW_MINIMIZED) ? " minimized" : "",
					(flags & SDL_WINDOW_RESIZABLE) ? " resizable" : "");
				resizePhase = ResizePhase::WaitResize;
				resizeDeadline = frameCount + 300;
			}
		}
		else if (resizePhase == ResizePhase::WaitResize)
		{
			unsigned int windowW = 0;
			unsigned int windowH = 0;
			render->getWindowSize(windowW, windowH);
			if (windowW != 0 && windowH != 0 && windowW != resizeBaselineW)
			{
				const double aspect = cameraAspect();
				const double expected = static_cast<double>(windowW) /
					static_cast<double>(windowH);
				if (std::abs(aspect - expected) > 0.02)
				{
					resizeFail("window camera aspect not re-derived after "
						"the resize (stale projection)");
				}
				else
				{
					SDL_Log("orkige_player: RESIZE SELFCHECK PASSED - "
						"%ux%u -> %ux%u, camera aspect %.4f",
						resizeBaselineW, resizeBaselineH, windowW, windowH,
						aspect);
					resizePhase = ResizePhase::Done;
					exitCode = 0;
					running = false;
				}
			}
			else if (frameCount >= resizeDeadline)
			{
				resizeFail("drawable never changed size (resize event "
					"lost?) - still " + std::to_string(windowW) + "x" +
					std::to_string(windowH));
			}
		}
	}
}

//---------------------------------------------------------
void PlayerSelfChecks::atLoopEnd(PlayerContext& context)
{
	int& exitCode = context.exitCode;

	if (jumperLuaCheck && !jumperCheckFailed &&
		jumperPhase != JumperCheckPhase::Done)
	{
		SDL_Log("orkige_player: JUMPER-LUA SELFCHECK FAILED - run ended "
			"in phase %d", static_cast<int>(jumperPhase));
		exitCode = 1;
	}
	if (rollerCheck && !rollerCheckFailed &&
		rollerPhase != RollerCheckPhase::Done)
	{
		SDL_Log("orkige_player: ROLLER SELFCHECK FAILED - run ended in "
			"phase %d", static_cast<int>(rollerPhase));
		exitCode = 1;
	}
	if (rollerProgressionCheck && !rollerProgCheckFailed &&
		rollerProgPhase != RollerProgPhase::Done)
	{
		SDL_Log("orkige_player: ROLLER PROGRESSION SELFCHECK FAILED - run "
			"ended in phase %d", static_cast<int>(rollerProgPhase));
		exitCode = 1;
	}
	if (tweenCheck && !tweenCheckFailed && !tweenCheckDone)
	{
		SDL_Log("orkige_player: TWEEN SELFCHECK FAILED - run ended before "
			"the check completed");
		exitCode = 1;
	}
	if (staticMoveCheck && !staticMoveDone)
	{
		SDL_Log("orkige_player: STATICMOVE SELFCHECK FAILED - run ended "
			"before the check completed");
		exitCode = 1;
	}
	if (spriteBatchCheck && !spriteBatchDone)
	{
		SDL_Log("orkige_player: SPRITEBATCH SELFCHECK FAILED - run ended "
			"before the check completed");
		exitCode = 1;
	}
	if (characterRigCheck && !characterRigCheckFailed &&
		characterRigPhase != CharacterRigPhase::Done)
	{
		SDL_Log("orkige_player: CHARACTER RIG SELFCHECK FAILED - run ended "
			"in phase %d", static_cast<int>(characterRigPhase));
		exitCode = 1;
	}
	if (lifecycleCheck && !lifecycleFailed &&
		lifecyclePhase != LifecyclePhase::Done)
	{
		SDL_Log("orkige_player: LIFECYCLE SELFCHECK FAILED - run ended in "
			"phase %d", static_cast<int>(lifecyclePhase));
		exitCode = 1;
	}
	if (resizeCheck && !resizeCheckFailed &&
		resizePhase != ResizePhase::Done)
	{
		SDL_Log("orkige_player: RESIZE SELFCHECK FAILED - run ended in "
			"phase %d", static_cast<int>(resizePhase));
		exitCode = 1;
	}
	if (integrationContactCheck && !integContactFailed &&
		integContactPhase != IntegContactPhase::Done)
	{
		SDL_Log("orkige_player: INTEGRATION CONTACT SELFCHECK FAILED - "
			"run ended in phase %d",
			static_cast<int>(integContactPhase));
		exitCode = 1;
	}
	if (integrationLevelCheck && !integLevelFailed &&
		integLevelPhase != IntegLevelPhase::Done)
	{
		SDL_Log("orkige_player: INTEGRATION LEVELSWITCH SELFCHECK FAILED "
			"- run ended in phase %d",
			static_cast<int>(integLevelPhase));
		exitCode = 1;
	}
	if (hotreloadCheck)
	{
		// restore the committed fixture script no matter how the run ended
		// (the selfcheck rewrites it in place) so the working tree stays
		// clean; the write is best-effort
		if (!hotreloadScriptPath.empty() && !hotreloadOriginal.empty())
		{
			std::ofstream restore(hotreloadScriptPath,
				std::ios::binary | std::ios::trunc);
			restore << hotreloadOriginal;
		}
		if (!hotreloadCheckFailed && hotreloadPhase != HotReloadPhase::Done)
		{
			SDL_Log("orkige_player: HOTRELOAD SELFCHECK FAILED - run ended "
				"in phase %d", static_cast<int>(hotreloadPhase));
			exitCode = 1;
		}
	}
}
