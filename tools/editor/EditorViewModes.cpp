/********************************************************************
	created:	Friday 2026/07/25 at 09:00
	filename: 	EditorViewModes.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "EditorViewModes.h"

namespace OrkigeEditor
{
	//---------------------------------------------------------
	SceneViewModeInfo sceneViewModeInfo(Orkige::RenderViewMode mode,
		bool wireframeViewCap, bool wireframeOverlayViewCap)
	{
		SceneViewModeInfo info;
		switch(mode)
		{
		case Orkige::RenderViewMode::Shaded:
			// the solid look every target renders - always available
			info.available = true;
			break;
		case Orkige::RenderViewMode::Wireframe:
			info.available = wireframeViewCap;
			if(!info.available)
			{
				info.reason = "Wireframe view is not available on this render "
					"backend (polygon mode is baked into the pipeline state with "
					"no per-view override).";
			}
			break;
		case Orkige::RenderViewMode::ShadedWireframe:
			// the solid look with a wireframe drawn ON TOP via overlay items
			// (a second renderable per scene mesh, depth-biased onto the shaded
			// surface) - available on both flavors by the same road
			info.available = wireframeOverlayViewCap;
			if(!info.available)
			{
				info.reason = "Shaded + Wireframe view is not available on this "
					"render backend.";
			}
			break;
		}
		return info;
	}
	//---------------------------------------------------------
	SceneViewModeInfo sceneLightingToggleInfo(bool unlitViewCap)
	{
		SceneViewModeInfo info;
		info.available = unlitViewCap;
		if(!info.available)
		{
			info.reason = "The lighting toggle is not available in this version "
				"(per-view suppression of the scene's directional light is not yet "
				"possible on either render backend).";
		}
		return info;
	}
	//---------------------------------------------------------
	GameViewRenderer chooseGameViewRenderer(bool sceneVisible,
		bool previewVisible, GameViewRenderer lastFocused)
	{
		if(sceneVisible && previewVisible)
		{
			// both up (split layout): the most-recently-focused renders, the other
			// freezes; default to the Scene view when neither was focused yet
			return lastFocused == GameViewRenderer::Preview
				? GameViewRenderer::Preview : GameViewRenderer::Scene;
		}
		if(sceneVisible)
		{
			return GameViewRenderer::Scene;
		}
		if(previewVisible)
		{
			return GameViewRenderer::Preview;
		}
		return GameViewRenderer::None;
	}
	//---------------------------------------------------------
	bool shouldSuppressLighting(bool lightingOn, bool sceneIsRenderer)
	{
		// flat-unlit only when the toggle is off AND the Scene view is the one game
		// view rendering this frame (the invariant guarantees the preview is not)
		return !lightingOn && sceneIsRenderer;
	}
	//---------------------------------------------------------
	bool shouldWireframeScene(Orkige::RenderViewMode mode, bool sceneIsRenderer)
	{
		// wireframe only when the selected mode is Wireframe AND the Scene view is
		// the one game view rendering this frame - next's global flip cannot leak
		// into the preview/Play because they never render in a Scene-view frame.
		// (ShadedWireframe drives the OVERLAY path - @see shouldWireframeOverlay -
		// not the flip, so it never arms the scene wireframe here.)
		return mode == Orkige::RenderViewMode::Wireframe && sceneIsRenderer;
	}
	//---------------------------------------------------------
	bool shouldWireframeOverlay(Orkige::RenderViewMode mode, bool sceneIsRenderer)
	{
		// the shaded+wireframe overlay only when the selected mode is
		// ShadedWireframe AND the Scene view is the one game view rendering this
		// frame - the overlay items' editor-only visibility bit then never even
		// reaches a Game-Preview / Play frame. Distinct from shouldWireframeScene
		// by the RenderViewMode value, so the two are radio-exclusive.
		return mode == Orkige::RenderViewMode::ShadedWireframe && sceneIsRenderer;
	}
	//---------------------------------------------------------
	char const* sceneViewModeName(Orkige::RenderViewMode mode)
	{
		switch(mode)
		{
		case Orkige::RenderViewMode::Shaded:			return "shaded";
		case Orkige::RenderViewMode::Wireframe:			return "wireframe";
		case Orkige::RenderViewMode::ShadedWireframe:	return "shaded_wireframe";
		}
		return "shaded";
	}
	//---------------------------------------------------------
	bool parseSceneViewMode(Orkige::String const& name,
		Orkige::RenderViewMode& outMode)
	{
		if(name == "shaded")
		{
			outMode = Orkige::RenderViewMode::Shaded;
			return true;
		}
		if(name == "wireframe")
		{
			outMode = Orkige::RenderViewMode::Wireframe;
			return true;
		}
		if(name == "shaded_wireframe")
		{
			outMode = Orkige::RenderViewMode::ShadedWireframe;
			return true;
		}
		return false;
	}
}
