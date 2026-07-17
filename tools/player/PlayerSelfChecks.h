/**************************************************************
	created:	2026/07/16 at 06:00
	filename: 	PlayerSelfChecks.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __PlayerSelfChecks_h__16_7_2026__06_00_00__
#define __PlayerSelfChecks_h__16_7_2026__06_00_00__

#include <cstddef>
#include <optional>
#include <string>

struct PlayerContext;

//! @brief the player's env-hooked end-to-end verifications: every
//! ORKIGE_*_SELFCHECK (plus the demo frame cap plumbing they share). One
//! struct owns every check's flags and phase state so the frame loop stays a
//! frame loop; the hooks below are called from main.cpp at the exact
//! sequence points the historical inline blocks occupied. Pure data +
//! logic - nothing here owns an engine object.
struct PlayerSelfChecks
{
	//--- the env flags (readEnvironment) ---------------------------------
	bool jumperLuaCheck = false;
	bool rollerCheck = false;
	std::string rollerShotDir;
	bool softbodyCheck = false;
	bool vectorAnimCheck = false;
	bool characterRigCheck = false;
	bool rollerProgressionCheck = false;
	std::string assetIdCheckTexture;
	std::string cookedCheckTexture;
	bool tweenCheck = false;
	bool hotreloadCheck = false;
	bool scriptPropCheck = false;
	bool integrationContactCheck = false;
	bool integrationLevelCheck = false;
	bool breadcrumbCheck = false;
	bool fadeCheck = false;
	bool lifecycleCheck = false;
	bool resizeCheck = false;
	bool perfCheck = false;
	bool benchmarkCheck = false;
	//! the static-mobility contract probe (@see the perFrame block)
	bool staticMoveCheck = false;
	//! the sprite-run batching probe (@see the perFrame block)
	bool spriteBatchCheck = false;

	//--- per-check phase state (comments ride with each check) -----------
	// --- ORKIGE_JUMPER_LUA_SELFCHECK=1: the ScriptComponent milestone,
	// verified end to end against projects/jumper-lua (run with
	// --project projects/jumper-lua). Synthetic SDL key AND mouse events
	// take the real input path (poll loop -> injectEvent -> InputManager
	// events -> GuiManager/isKeyDown); the C++ side observes ONLY
	// what any outsider could: the Player object's components through
	// the world, the Lua-booted gui widgets through the
	// GuiManager singleton and the stats the scripts publish into
	// the Lua `shared` tables. Asserted, frame-scripted:
	//   frame  5  both scripts loaded (player.lua, game.lua), the UI is
	//             up (all widgets exist), the game is on the visible
	//             title screen and the HUD is hidden
	//   10..20    a synthetic mouse click on the START button (move,
	//             press, release) -> the game switches to "playing",
	//             title hidden, HUD visible
	//   then      hold RIGHT until the player moved +1.0m (CONDITION-
	//             driven with a fat deadline - dt-correct movement covers
	//             wall time, so a frame count overshoots on slow hosts;
	//             the roller tilt phases document the same convention)
	//             and the HUD progress bar advanced
	//   then      wait for grounded (deadline-bounded); press SPACE ->
	//             the player rises >0.8m and lands again
	//   then      teleport into the first gap -> the script's kill plane
	//             respawns the player at the start (shared respawns +1)
	//   then      teleport in front of the goal -> the script's win
	//             check fires (shared wins +1) -> the win screen shows
	//   then      press ENTER -> the game restarts into "playing" with
	//             the win screen hidden; the follow camera stayed
	//             roll-free over the whole session
	// Any missed deadline exits non-zero; measured values are logged.
	// (jumperLuaCheck itself is read above, before the engine window
	// exists.)
	enum class JumperCheckPhase
	{
		Script,			// fixed-frame part (boot+UI, click, move, jump)
		WaitLanding,	// jump flight until the script reports grounded
		WaitRespawn,	// falling in the gap until the script respawned
		WaitWin,		// in front of the goal until the win fired
		WaitWinUi,		// until the win screen is visible, then ENTER
		WaitRestart,	// until ENTER put the game back into "playing"
		Done
	};

	JumperCheckPhase jumperPhase = JumperCheckPhase::Script;
	float jumperStartX = 0.0f;
	float jumperMovedX = 0.0f;
	//! condition-driven move/grounded pacing (frame-count pacing walked the
	//! player off the platform on slow hosts - dt-correct movement covers
	//! wall time, not frames): 0 = leg not yet active
	unsigned long jumperMoveDeadline = 0;
	unsigned long jumperGroundedDeadline = 0;
	float jumperJumpStartY = 0.0f;
	float jumperMaxRise = 0.0f;
	unsigned long jumperJumpFrame = 0;
	unsigned long jumperEnterFrame = 0;
	unsigned long jumperPhaseDeadline = 0;
	double jumperBaseRespawns = 0.0;
	double jumperBaseWins = 0.0;
	size_t jumperBatchesWithUi = 0;
	bool jumperCheckFailed = false;
	// the Lua-booted UI, seen through the GuiManager singleton: does
	// the widget exist, and is its screen (= its shared z layer) visible.
	// gui runs on BOTH render flavors
	// (engine:hasUISystem() is true everywhere), so the UI assertions
	// are no longer flavor-gated - uiChecksEnabled stays as the one
	// switch a future UI-less flavor would flip.
	static constexpr bool uiChecksEnabled = true;
	// --- ORKIGE_ROLLER_SELFCHECK=1: the 2D tier (SpriteComponent, ortho
	// camera, tilt input, physics pause + kinematic tile teleports),
	// verified end to end against projects/roller (run with
	// --project projects/roller). Same rules as the jumper selfcheck:
	// synthetic SDL key events take the real input path, the C++ side
	// observes only shared.roller, components through the world and the
	// gui widgets. Frame-scripted:
	//   frame  5  both scripts booted, HUD up, mode "play", sim running
	//   then      ACTIVE GATE: deactivate the TileC subtree + the Ball -
	//             triangle count drops, the tile's wall body leaves the
	//             simulation, ball.lua stops ticking; reactivate restores
	//   then      hold LEFT -> the simulated tilt turns gravity left and
	//             the ball rolls -x (position sampled)
	//             screenshot roller_play.png (when the env dir is set)
	//   then      TAB -> mode "move": physics PAUSED, cursor visible
	//             over the empty slot; collision probed at tile B's
	//             future wall location (must be free)
	//             DOWN -> tile B slides into the empty slot VIA ITS
	//             PARENT (one teleport of the "TileB" group object):
	//             frame sprite + GOAL moved -6 in WORLD y, their LOCAL
	//             transforms untouched, and a ray probe proves the wall
	//             BODY collides at the new spot (and no longer at the
	//             old one) - while still paused
	//             TAB -> back to "play", sim unpaused
	//   then      hold RIGHT -> gravity swings right, the ball rolls
	//             through tile A's right opening into the slid-down
	//             tile B onto the goal star -> shared.roller.wins fires
	//             -> the win banner shows
	// Any missed deadline exits non-zero; measured values are logged.
	// note: the tilt SIMULATION advances on wall-clock frame time (the
	// human-facing behavior) while headless automated runs floor the game
	// dt at 1/60 - so the tilt phases below are CONDITION-driven with fat
	// frame deadlines instead of fixed frame numbers
	enum class RollerCheckPhase
	{
		Boot,		// scripts/HUD/camera/mode checks at frame 5
		ActiveGate,	// deactivate a tile subtree + the ball: no render,
					// no collision, no script ticks; reactivate restores
		TiltRoll,	// hold LEFT until the tilt built up and the ball rolled
		MoveWorld,	// TAB, probe, DOWN-slide, probe, TAB (frame-scripted)
		WaitWin,	// rolling right until the script reports the win
		WaitWinUi,	// until the win banner layer is visible
		Done
	};

	RollerCheckPhase rollerPhase = RollerCheckPhase::Boot;
	unsigned long rollerStepFrame = 0;	// current phase's anchor frame
	float rollerStartX = 0.0f;
	float rollerMovedX = 0.0f;
	float rollerTileBStartY = 0.0f;
	float rollerGoalStartY = 0.0f;
	float rollerTileBFrameLocalY = 0.0f;	// child LOCAL y (must not change)
	std::size_t rollerTrianglesActive = 0;	// triangles with everything active
	double rollerBallUpdatesGated = 0.0;	// ball.lua ticks while deactivated
	unsigned long rollerRollFrame = 0;
	unsigned long rollerPhaseDeadline = 0;
	bool rollerCheckFailed = false;
	// --- ORKIGE_SOFTBODY_SELFCHECK=1: soft, deformable organic shapes end to
	// end against projects/vectorshapes (scenes/softbody.oscene). Phased,
	// condition-driven (physics is wall-clock/fixed-step paced):
	//   fall     the "FallBlob" (VectorShape softBody + a dynamic RigidBody)
	//            drops; on landing a CONTACT squashes it (getSquash != 0) and
	//            the wobble springs move the mesh off rest (displacement > 0)
	//   settle   the wobble decays back to the rest pose (not deforming)
	//   morph    the "MorphBlob" is deformed by a Lua self.shape:playMorph
	//            call (proves the morph blend + the Lua drive path)
	// A missed deadline exits non-zero. On boot it logs the MEASURED per-frame
	// deform cost - the mobile-viability budget number the owner asked for.
	enum class SoftBodyPhase { Fall, Settle, Morph, Done };

	SoftBodyPhase softbodyPhase = SoftBodyPhase::Fall;
	bool softbodyCheckFailed = false;
	bool softbodySawSquash = false;
	bool softbodySawWobble = false;
	float softbodyPeakSquash = 0.0f;
	float softbodyPeakWobble = 0.0f;
	float softbodyMorphPeak = 0.0f;
	unsigned long softbodyPhaseDeadline = 0;
	bool softbodyMorphStarted = false;
	// --- ORKIGE_VECTORANIM_SELFCHECK=1: vector (Lottie) animation rigs end
	// to end against projects/vectorshapes (scenes/vectoranim.oscene). The
	// hero rig carries an `idle` (loop) and a `hop` (once) clip; a script
	// plays idle and, after a beat, crossFades to hop. Phased,
	// condition-driven (the script paces the crossfade off wall clock):
	//   boot   frame 5: the Hero has a built VectorAnimationComponent with
	//          both clips, playing the idle clip
	//   idle   the idle clip advances - the composed pose SIGNATURE changes
	//          as frames tick (the vertices move: the runtime evaluate+bake)
	//   hop    the scripted crossFade switched the current clip to `hop`
	//   ended  the one-shot hop completed: the ended event reached the script
	//          (shared.heroanim.ended incremented) and the component is atEnd
	// A missed deadline exits non-zero. On boot it logs the MEASURED per-frame
	// evaluate+tessellate cost per character (the mobile budget number).
	enum class VectorAnimPhase { Boot, Idle, Hop, Ended, Done };

	VectorAnimPhase vectorAnimPhase = VectorAnimPhase::Boot;
	bool vectorAnimCheckFailed = false;
	float vectorAnimPoseMin = 0.0f;
	float vectorAnimPoseMax = 0.0f;
	bool vectorAnimPoseSeeded = false;
	unsigned long vectorAnimDeadline = 0;
	// --- ORKIGE_CHARACTER_RIG_SELFCHECK=1: 3D SKELETAL character animation
	// end to end against tests/projects/character (a generated blocky
	// mannequin, Util/make_character_rig.py: 7-joint skeleton, skin weights,
	// walk + idle clips). The Mannequin object carries a ModelComponent (the
	// skinned .glb) + an AnimationComponent. Phased:
	//   Boot     frame 5: the rig loaded. If the mesh carries NO animations
	//            (a flavor that imports glTF statically, dropping the
	//            skeleton - both shipping flavors import it whole), the check
	//            SKIPS HONESTLY with that finding as the message (exit 0).
	//            Otherwise assert the walk+idle clips exist, play walk, seed
	//            the animated bounds.
	//   Walk     the walk clip advances: the skeleton-driven local bounds SPREAD
	//            (a swinging limb moves the skinned vertices - the bone-driven
	//            deformation proof), then crossFadeTo("idle") is requested.
	//   Blend    the crossfade transitions: BOTH clips are enabled mid-blend and
	//            the progress ramps to completion, then only idle remains.
	//   Idle     idle is the sole playing clip and its sway still moves bounds.
	// A missed deadline exits non-zero.
	enum class CharacterRigPhase { Boot, Walk, Blend, Idle, Done };

	CharacterRigPhase characterRigPhase = CharacterRigPhase::Boot;
	bool characterRigCheckFailed = false;
	bool characterRigSkipped = false;
	float characterRigBoundsMin = 0.0f;
	float characterRigBoundsMax = 0.0f;
	bool characterRigBoundsSeeded = false;
	bool characterRigSawBothEnabled = false;
	bool characterRigSawBlending = false;
	float characterRigIdleBoundsMin = 0.0f;
	float characterRigIdleBoundsMax = 0.0f;
	bool characterRigIdleSeeded = false;
	unsigned long characterRigDeadline = 0;
	// --- ORKIGE_ROLLER_PROGRESSION_SELFCHECK=1: the level sequence + the
	// DEFERRED scene switch + the progression save, end to end
	// against projects/roller. Same discipline as the roller selfcheck
	// (synthetic keys through the real input path, C++ observes only
	// shared.roller, the components and the LevelManager). Phased:
	//   frame 5   scripts up, at level 1 (index 0), the sequence loaded
	//   solve L1  TAB->move, DOWN slides tile B into the empty slot, TAB->
	//             play, hold RIGHT until the ball rolls onto the goal (win)
	//   switch    game.lua completes the level, records/persists progress
	//             and, after the banner beat, the DEFERRED load switches to
	//             level 2: assert shared.roller.levelIndex became 1, the
	//             save file was written and resumeLevel persisted to 1
	//   solve L2  the straight-shot level: hold RIGHT until the win
	// note: the tilt phases are wall-clock paced, so they are condition-
	// driven with fat frame deadlines (like the roller selfcheck).
	enum class RollerProgPhase
	{
		Boot,			// scripts up + at level 0 at frame 5
		L1Slide,		// TAB, DOWN-slide, TAB (frame-scripted from the anchor)
		L1Roll,			// hold RIGHT until level 1's win
		WaitSwitch,		// until the deferred load reaches level 2
		L2Roll,			// hold RIGHT until level 2's win
		Done
	};

	RollerProgPhase rollerProgPhase = RollerProgPhase::Boot;
	unsigned long rollerProgStepFrame = 0;
	unsigned long rollerProgDeadline = 0;
	bool rollerProgCheckFailed = false;
	// --- ORKIGE_TWEEN_SELFCHECK=1: the tween system end to end against
	// tests/projects/tween (run with --project tests/projects/tween).
	// The scene's Tweener object runs scripts/tween_check.lua, which
	// starts the whole Lua tween surface (tween.to closure + typed
	// fade/volume helpers, cancel, delay, onComplete) and publishes its
	// observations into shared.tween; the C++ side verifies only what an
	// outsider can see: the script's verdict, the Tweener transform (the
	// tween.to closure drove x to 3), the sprite tint alpha (tween.fade),
	// the "music" group volume (tween.volume) and the manifest-loaded
	// mixer settings (audio.master=0.8 / audio.group.hud=0.4 from
	// project.orkproj). Condition-driven (durations live in simulated
	// time, deterministic under the floored automated dt) with a fat
	// frame deadline.
	bool tweenCheckDone = false;
	bool tweenCheckFailed = false;
	// --- ORKIGE_STATICMOVE_SELFCHECK=1: the static-mobility contract,
	// verified against projects/benchmark scenes/fixture_static.oscene.
	// Frame 30: record the draw-batch count, then MOVE the static "Static3"
	// object through its TransformComponent (a mobility-contract violation:
	// the backend warns once and lands the move through its repair path).
	// Frame 60: assert the world position landed exactly and print the
	// before/after batch counts - the ctest driver pins the flavor-specific
	// delta (classic: +1, the demoted entity draws individually again;
	// next: 0) and greps the one warning line.
	bool staticMoveDone = false;
	std::size_t staticMoveBatchesBefore = 0;
	// --- ORKIGE_SPRITEBATCH_SELFCHECK=1: sprite-run batching, verified
	// against projects/benchmark scenes/fixture_sprites.oscene (11 sprites
	// whose exact grouping is 3 runs + 1 solo, see the generator). Frame 20
	// MOVES sprite "A4" (a batched member - the run re-uploads and the
	// frame-60 screenshot must render the move; the ctest driver compares it
	// against a batching-off run of the same move). Frame 30 records the
	// draw-batch count and the batcher's realized run count. When batching
	// started ON, the LIVE TOGGLE leg follows: off at frame 70, count at 90,
	// back on at 100, count + runs at 120 - the escape hatch must release
	// and re-form the runs without a reboot. The driver pins the
	// flavor-exact numbers from the printed lines.
	bool spriteBatchDone = false;
	bool spriteBatchStartedOn = false;
	std::size_t spriteBatchBatchesOn = 0;
	std::size_t spriteBatchRunsOn = 0;
	std::size_t spriteBatchBatchesLiveOff = 0;
	//! steady-state sampling for the batch baseline + restore legs: a run
	//! re-upload can surface as a transient extra batch for a frame on some
	//! backends, so counts lock on two consecutive agreeing frames and the
	//! restore converges under a deadline instead of asserting one frame
	bool spriteBatchBaselineLocked = false;
	std::size_t spriteBatchPrevSample = static_cast<std::size_t>(-1);
	unsigned long spriteBatchToggleFrame = 0;
	unsigned long spriteBatchRestoreDeadline = 0;
	// --- ORKIGE_HOTRELOAD_SELFCHECK=1: Lua hot-reload, verified
	// end to end against tests/projects/hotreload (run with --project
	// tests/projects/hotreload). The Probe object runs scripts/
	// reload_probe.lua, whose init publishes shared.hotreload.value = 1.
	// Frame-scripted (hotReload is synchronous, so fixed frames are safe):
	//   frame  5  script loaded + started, value == 1; save the script's
	//             ORIGINAL bytes and MOVE the Probe transform to a
	//             distinctive engine-side pose (7,0,0)
	//   frame 10  overwrite the file to publish value = 2, drive
	//             hotReload() -> the running value flips to 2 (behavior
	//             changed) AND the (7,0,0) transform SURVIVED the swap
	//             (engine-side state persists), re-init ran, no error
	//   frame 15  record the tick count, overwrite the file with BROKEN
	//             Lua, drive hotReload() -> a non-fatal reload error
	//             surfaces (hasReloadError, mFailed still false) and the
	//             OLD instance (value == 2) stays intact
	//   frame 20  the surviving instance kept ticking (ticks advanced) ->
	//             done. The committed script is restored below on exit.
	enum class HotReloadPhase { Boot, AfterSwap, AfterBreak, Done };

	HotReloadPhase hotreloadPhase = HotReloadPhase::Boot;
	bool hotreloadCheckFailed = false;
	std::string hotreloadScriptPath;	// resolved reload_probe.lua path
	std::string hotreloadOriginal;		// its committed bytes (restored on exit)
	double hotreloadTicksAtBreak = -1.0;
	// --- ORKIGE_SCRIPTPROP_SELFCHECK=1: Lua script exported properties
	// verified end to end against tests/projects/scriptprop.
	// The scene bakes the Mover's exported moveSpeed at 5 (the script's own
	// default is 1), so the running script seeing 5 at init proves the
	// PER-INSTANCE value round-tripped through serialization AND was injected
	// onto `self` before init. The script moves +x at moveSpeed; a live set
	// of moveSpeed -> 0 through the reflected setter (the debug-protocol /
	// MCP write path) must stop the motion. Frame-scripted:
	//   frame  5  script up, injectedSpeed == 5, x advanced from 0
	//   frame 15  x == 5 * elapsed (behaved with the injected value), then
	//             flip moveSpeed -> 0 through the reflected setter
	//   frame 25  x stopped (the live set reached self.moveSpeed) -> done
	enum class ScriptPropPhase { Boot, Moving, Frozen, Done };

	ScriptPropPhase scriptPropPhase = ScriptPropPhase::Boot;
	bool scriptPropCheckFailed = false;
	double scriptPropXAtFreeze = 0.0;
	// --- ORKIGE_INTEGRATION_CONTACT_SELFCHECK=1: tags + input action +
	// physics + contact events, end to end against
	// tests/projects/integration/scenes/contact.oscene. ball_probe.lua
	// discovers the goal BY TAG at init, then hangs still (gravity off)
	// until the injected "jump" action turns gravity on; the ensuing drop
	// overlaps the goal SENSOR and fires onContactBegin. Condition-driven:
	//   [VerifyTags]   the script published exactly one tag-found goal
	//                  ("Goal"), then inject a HELD SPACE (the "jump" edge)
	//   [DriveInput]   release SPACE a few frames later; the action must
	//                  have reached the script (input count advanced)
	//   [AwaitContact] the input-driven fall must fire onContactBegin with
	//                  the tagged goal as the other body
	enum class IntegContactPhase { VerifyTags, DriveInput, AwaitContact,
		Done };

	IntegContactPhase integContactPhase = IntegContactPhase::VerifyTags;
	bool integContactFailed = false;
	unsigned long integContactInputFrame = 0;
	// --- ORKIGE_INTEGRATION_LEVELSWITCH_SELFCHECK=1: a deferred level
	// switch fired while a tween + a live particle emitter run, end to end
	// against tests/projects/integration/scenes/levelA.oscene ->
	// levelB.oscene. director.lua starts a 1.5s tween.move and a particle
	// burst, then requests world.loadScene mid-tween; survivor.lua on level
	// B proves the switched-to world ticks and that the shared table (a
	// carry marker) survived the GameObjectManager::clear teardown.
	// Condition-driven:
	//   [ObserveA]     on level A, confirm the tween is moving Mover AND the
	//                  Fx emitter has live particles (both mid-flight)
	//   [AwaitSwitch]  wait for level B to boot (the deferred switch applied)
	//   [VerifyB]      Mover is GONE (clean teardown), the carry survived,
	//                  and level B keeps ticking
	enum class IntegLevelPhase { ObserveA, AwaitSwitch, VerifyB, Done };

	IntegLevelPhase integLevelPhase = IntegLevelPhase::ObserveA;
	bool integLevelFailed = false;
	double integLevelTicksAtEntry = -1.0;
	unsigned long integLevelVerifyFrame = 0;
	// ORKIGE_LIFECYCLE_SELFCHECK phased state (the block lives at the loop
	// bottom): Init -> Backgrounded -> Foregrounded -> Done
	enum class LifecyclePhase { Init, Backgrounded, Foregrounded, Done };

	LifecyclePhase lifecyclePhase = LifecyclePhase::Init;
	bool lifecycleFailed = false;
	// --- ORKIGE_RESIZE_SELFCHECK=1: the window-resize plumbing end to end -
	// the same path a device rotation takes (drawable size change -> SDL
	// resize event -> notifyWindowResized -> swapchain/drawable recreate +
	// window-camera aspect re-derive). At the baseline frame the check
	// records the drawable size and asserts the window camera's projection
	// matches it, then requests a DIFFERENT window size through the host
	// window (SDL_SetWindowSize, so the resize event takes the real
	// poll-loop path). Condition-driven with a fat deadline (the window
	// system delivers the event on its own schedule): the render system
	// must report the new drawable size AND the window camera's aspect
	// must match it - the stale-aspect (stretched image) regression guard.
	enum class ResizePhase { Baseline, WaitResize, Done };

	ResizePhase resizePhase = ResizePhase::Baseline;
	bool resizeCheckFailed = false;
	unsigned int resizeBaselineW = 0;
	unsigned int resizeBaselineH = 0;
	unsigned long resizeDeadline = 0;
	bool perfCheckFailed = false;

	// fade selfcheck state (see the ORKIGE_FADE_SELFCHECK block below)
	bool fadeStarted = false;
	bool fadeSwitched = false;
	bool fadeSawClearAfterSwitch = false;
	float fadeMaxAlpha = 0.0f;
	float fadeAlphaAtSwitch = -1.0f;

	//--- the hooks (called from main.cpp at the historical points) --------
	//! read the ORKIGE_* env hooks; also derives context.frameLimit and
	//! context.automatedRun (they gate vsync and frame pacing at boot)
	void readEnvironment(PlayerContext& context);
	//! tilt-calibration + haptics: synchronous, pre-world - a run exit code
	//! when one of them ran, nullopt to continue booting
	std::optional<int> earlySynchronousChecks(PlayerContext& context);
	//! the asset-id rename check, right after the scene loaded
	std::optional<int> afterSceneLoad(PlayerContext& context);
	//! game-support + gameplay packs: synchronous against the live wiring
	std::optional<int> gameplaySynchronousChecks(PlayerContext& context);
	//! pre-loop work: the measured deform/animation budgets + profiler arm
	void beforeLoop(PlayerContext& context);
	//! the per-frame check chain, called at the loop bottom (after the
	//! frame's render/instrument folds, before the frame-cap gate)
	void perFrame(PlayerContext& context);
	//! the end-of-run verdicts (a check that never reached Done fails the
	//! run) + the hot-reload script restore
	void atLoopEnd(PlayerContext& context);
};

#endif //__PlayerSelfChecks_h__16_7_2026__06_00_00__
