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
	// available regardless of the wireframe capability, with no greyed reason
	REQUIRE(sceneViewModeInfo(RenderViewMode::Shaded, true).available);
	REQUIRE(sceneViewModeInfo(RenderViewMode::Shaded, false).available);
	REQUIRE(sceneViewModeInfo(RenderViewMode::Shaded, false).reason.empty());
}

TEST_CASE("scene view mode: Wireframe follows the backend capability",
	"[editor][viewmode]")
{
	// a backend that reports the capability (both flavors do now - classic
	// per-target, next global-under-the-invariant) offers it with no reason
	REQUIRE(sceneViewModeInfo(RenderViewMode::Wireframe, true).available);
	REQUIRE(sceneViewModeInfo(RenderViewMode::Wireframe, true).reason.empty());

	// a hypothetical backend without the capability greys it with a reason
	const OrkigeEditor::SceneViewModeInfo noCap =
		sceneViewModeInfo(RenderViewMode::Wireframe, false);
	REQUIRE_FALSE(noCap.available);
	REQUIRE_FALSE(noCap.reason.empty());
}

TEST_CASE("scene view mode: ShadedWireframe is unavailable on every flavor",
	"[editor][viewmode]")
{
	// the v1 boundary - a wireframe overlay needs a second depth-biased pass,
	// so it is greyed whether or not the plain wireframe capability is present
	REQUIRE_FALSE(sceneViewModeInfo(RenderViewMode::ShadedWireframe, true)
		.available);
	REQUIRE_FALSE(sceneViewModeInfo(RenderViewMode::ShadedWireframe, false)
		.available);
	REQUIRE_FALSE(sceneViewModeInfo(RenderViewMode::ShadedWireframe, true)
		.reason.empty());
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
	// Shaded (or the unbuilt ShadedWireframe) never arms the scene wireframe
	REQUIRE_FALSE(shouldWireframeScene(RenderViewMode::Shaded, true));
	REQUIRE_FALSE(shouldWireframeScene(RenderViewMode::ShadedWireframe, true));
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
