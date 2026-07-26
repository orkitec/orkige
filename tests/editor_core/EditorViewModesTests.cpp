/**************************************************************
	created:	2026/07/25 at 09:00
	filename: 	EditorViewModesTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#include "EditorViewModes.h"

#include <catch2/catch_test_macros.hpp>

using Orkige::RenderViewMode;
using OrkigeEditor::sceneViewModeInfo;
using OrkigeEditor::sceneLightingToggleInfo;
using OrkigeEditor::sceneViewModeName;
using OrkigeEditor::parseSceneViewMode;

TEST_CASE("scene view mode: Shaded is always available", "[editor][viewmode]")
{
	// available regardless of either wireframe capability, with no greyed reason
	REQUIRE(sceneViewModeInfo(RenderViewMode::Shaded, true, true).available);
	REQUIRE(sceneViewModeInfo(RenderViewMode::Shaded, false, false).available);
	REQUIRE(sceneViewModeInfo(RenderViewMode::Shaded, false, false)
		.reason.empty());
}

TEST_CASE("scene view mode: Wireframe follows the wireframe-flip capability",
	"[editor][viewmode]")
{
	// a backend that reports the flip capability (both flavors do now - classic
	// per-target, next global-under-the-invariant) offers it with no reason; the
	// OVERLAY capability is irrelevant to the plain Wireframe flip
	REQUIRE(sceneViewModeInfo(RenderViewMode::Wireframe, true, false).available);
	REQUIRE(sceneViewModeInfo(RenderViewMode::Wireframe, true, true)
		.reason.empty());

	// a hypothetical backend without the flip capability greys it with a reason
	const OrkigeEditor::SceneViewModeInfo noCap =
		sceneViewModeInfo(RenderViewMode::Wireframe, false, true);
	REQUIRE_FALSE(noCap.available);
	REQUIRE_FALSE(noCap.reason.empty());
}

TEST_CASE("scene view mode: ShadedWireframe follows the overlay capability",
	"[editor][viewmode]")
{
	// the overlay-items road: available whenever the overlay capability is
	// present (both flavors report it now), INDEPENDENT of the plain-wireframe
	// flip capability
	REQUIRE(sceneViewModeInfo(RenderViewMode::ShadedWireframe, false, true)
		.available);
	REQUIRE(sceneViewModeInfo(RenderViewMode::ShadedWireframe, true, true)
		.reason.empty());

	// a hypothetical backend without the overlay capability greys it with a reason
	const OrkigeEditor::SceneViewModeInfo noCap =
		sceneViewModeInfo(RenderViewMode::ShadedWireframe, true, false);
	REQUIRE_FALSE(noCap.available);
	REQUIRE_FALSE(noCap.reason.empty());
}

TEST_CASE("scene lighting toggle follows the backend capability",
	"[editor][viewmode]")
{
	// next (cap true) offers it; classic (cap false) greys it with a reason
	REQUIRE(sceneLightingToggleInfo(true).available);
	REQUIRE(sceneLightingToggleInfo(true).reason.empty());
	REQUIRE_FALSE(sceneLightingToggleInfo(false).available);
	REQUIRE_FALSE(sceneLightingToggleInfo(false).reason.empty());
}

TEST_CASE("game-view renderer invariant: only one renders per frame",
	"[editor][viewmode]")
{
	using OrkigeEditor::chooseGameViewRenderer;
	using GVR = OrkigeEditor::GameViewRenderer;
	// only one visible -> it renders
	REQUIRE(chooseGameViewRenderer(true, false, GVR::None) == GVR::Scene);
	REQUIRE(chooseGameViewRenderer(false, true, GVR::None) == GVR::Preview);
	// neither -> None
	REQUIRE(chooseGameViewRenderer(false, false, GVR::None) == GVR::None);
	// BOTH visible -> the most-recently-focused renders, the other freezes
	REQUIRE(chooseGameViewRenderer(true, true, GVR::Preview) == GVR::Preview);
	REQUIRE(chooseGameViewRenderer(true, true, GVR::Scene) == GVR::Scene);
	// both visible, nothing focused yet -> default to the Scene view
	REQUIRE(chooseGameViewRenderer(true, true, GVR::None) == GVR::Scene);
}

TEST_CASE("lighting suppression under the render invariant", "[editor][viewmode]")
{
	using OrkigeEditor::shouldSuppressLighting;
	// flat only when: toggle off AND the Scene view is the frame's renderer
	REQUIRE(shouldSuppressLighting(/*lightingOn*/false, /*sceneIsRenderer*/true));
	// toggle ON (lit) -> never suppress
	REQUIRE_FALSE(shouldSuppressLighting(true, true));
	// Scene is NOT the renderer (preview renders) -> stay lit (real look)
	REQUIRE_FALSE(shouldSuppressLighting(false, false));
}

TEST_CASE("scene wireframe under the render invariant", "[editor][viewmode]")
{
	using OrkigeEditor::shouldWireframeScene;
	// next arms its GLOBAL wireframe only when the mode is Wireframe AND the
	// Scene view is the frame's renderer (the invariant keeps it off the preview)
	REQUIRE(shouldWireframeScene(RenderViewMode::Wireframe, /*sceneIsRenderer*/true));
	// Wireframe selected but the preview renders this frame -> stay solid
	REQUIRE_FALSE(shouldWireframeScene(RenderViewMode::Wireframe, false));
	// Shaded (or ShadedWireframe - the OVERLAY road, not a flip) never arms the
	// scene wireframe flip
	REQUIRE_FALSE(shouldWireframeScene(RenderViewMode::Shaded, true));
	REQUIRE_FALSE(shouldWireframeScene(RenderViewMode::ShadedWireframe, true));
}

TEST_CASE("scene wireframe OVERLAY under the render invariant",
	"[editor][viewmode]")
{
	using OrkigeEditor::shouldWireframeOverlay;
	using OrkigeEditor::shouldWireframeScene;
	// the shaded+wireframe overlay arms only when the mode is ShadedWireframe AND
	// the Scene view is the frame's renderer (the editor-only-bit companions then
	// never reach a Game-Preview / Play frame)
	REQUIRE(shouldWireframeOverlay(RenderViewMode::ShadedWireframe,
		/*sceneIsRenderer*/true));
	// ShadedWireframe selected but the preview renders this frame -> no overlay
	REQUIRE_FALSE(shouldWireframeOverlay(RenderViewMode::ShadedWireframe, false));
	// the flip modes never arm the overlay
	REQUIRE_FALSE(shouldWireframeOverlay(RenderViewMode::Shaded, true));
	REQUIRE_FALSE(shouldWireframeOverlay(RenderViewMode::Wireframe, true));
	// RADIO-EXCLUSIVE: the flip and the overlay are never both armed in one frame
	// (the two modes are distinct RenderViewMode values)
	for(bool sceneIsRenderer : { true, false })
	{
		const bool wireBoth =
			shouldWireframeScene(RenderViewMode::Wireframe, sceneIsRenderer) &&
			shouldWireframeOverlay(RenderViewMode::Wireframe, sceneIsRenderer);
		REQUIRE_FALSE(wireBoth);
		const bool overlayBoth =
			shouldWireframeScene(RenderViewMode::ShadedWireframe, sceneIsRenderer) &&
			shouldWireframeOverlay(RenderViewMode::ShadedWireframe, sceneIsRenderer);
		REQUIRE_FALSE(overlayBoth);
	}
}

TEST_CASE("scene view mode string round-trips", "[editor][viewmode]")
{
	// the MCP/persistence vocabulary maps both ways with no drift
	const RenderViewMode modes[] = { RenderViewMode::Shaded,
		RenderViewMode::Wireframe, RenderViewMode::ShadedWireframe };
	for(RenderViewMode mode : modes)
	{
		RenderViewMode parsed = RenderViewMode::Shaded;
		REQUIRE(parseSceneViewMode(sceneViewModeName(mode), parsed));
		REQUIRE(parsed == mode);
	}
}

TEST_CASE("scene view mode: an unknown string is refused", "[editor][viewmode]")
{
	RenderViewMode parsed = RenderViewMode::Wireframe;
	REQUIRE_FALSE(parseSceneViewMode("solid", parsed));
	REQUIRE_FALSE(parseSceneViewMode("", parsed));
	// left untouched on refusal
	REQUIRE(parsed == RenderViewMode::Wireframe);
}
