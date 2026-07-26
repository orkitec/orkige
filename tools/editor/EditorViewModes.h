/********************************************************************
	created:	Friday 2026/07/25 at 09:00
	filename: 	EditorViewModes.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __EditorViewModes_h__25_7_2026__09_00_00__
#define __EditorViewModes_h__25_7_2026__09_00_00__

#include <core_util/String.h>
#include <engine_render/RenderTexture.h>	// RenderViewMode

//! @file EditorViewModes.h
//! @brief pure availability + string mapping for the Scene view's display modes
//! (the Display dropdown's view-mode radio + lighting toggle). The per-flavor
//! capability comes from RenderCaps (SceneWireframeView /
//! SceneWireframeOverlayView / SceneUnlitView) queried
//! at the editor's call sites; this header holds only the pure decision so the
//! editor UI (greying + tooltips), the MCP verb (refusal reasons) and the unit
//! test share ONE truth. No render backend is touched here - the caps arrive as
//! plain bools, so it is headless-unit-testable.

namespace OrkigeEditor
{
	//! availability + greyed-tooltip reason for one Scene-view display choice
	struct SceneViewModeInfo
	{
		bool			available = true;
		//! "" when available; the greyed-out tooltip / MCP refusal text otherwise
		Orkige::String	reason;
	};

	//! @brief is @p mode selectable on a backend whose wireframe-FLIP capability
	//! is @p wireframeViewCap (RenderCaps::SceneWireframeView) and whose
	//! shaded+wireframe OVERLAY capability is @p wireframeOverlayViewCap
	//! (RenderCaps::SceneWireframeOverlayView)? Shaded is always available;
	//! Wireframe follows the flip cap; ShadedWireframe (the solid look with a
	//! wireframe drawn on top via overlay items) follows the overlay cap.
	SceneViewModeInfo sceneViewModeInfo(Orkige::RenderViewMode mode,
		bool wireframeViewCap, bool wireframeOverlayViewCap);

	//! @brief is the Scene-view lighting (lit/unlit) toggle available on a backend
	//! whose unlit capability is @p unlitViewCap (RenderCaps::SceneUnlitView)?
	SceneViewModeInfo sceneLightingToggleInfo(bool unlitViewCap);

	//! @brief which of the two "game view" RTTs renders this frame - the hard
	//! invariant is that the Scene view and the Game Preview NEVER both render in
	//! the same frame (else the global lighting suppression could flatten a visible
	//! preview). The chooser: when only one is visible it renders; when BOTH are
	//! visible (an undocked/split layout) the most-recently-FOCUSED one renders and
	//! the other freezes; when neither is visible, None. @p lastFocused is the last
	//! of the two the user focused (defaults to Scene when None and both are up).
	enum class GameViewRenderer { None, Scene, Preview };
	GameViewRenderer chooseGameViewRenderer(bool sceneVisible,
		bool previewVisible, GameViewRenderer lastFocused);

	//! @brief should the WHOLE frame render flat-unlit? Lighting-off is a GLOBAL
	//! per-frame state (@see RenderWorld::setLightingSuppressed). Under the render
	//! invariant it simplifies to exactly: the Lighting toggle is OFF (@p lightingOn
	//! false) AND the Scene view is the ONE game view rendering this frame
	//! (@p sceneIsRenderer). The Game Preview, whenever IT renders, is never in a
	//! suppressed frame, so it is always the real lit look.
	bool shouldSuppressLighting(bool lightingOn, bool sceneIsRenderer);

	//! @brief should the WHOLE frame render the 3D scene in WIREFRAME? On the next
	//! flavor wireframe is a GLOBAL state (@see RenderWorld::setSceneWireframe), so
	//! like lighting-off it may be armed ONLY when the Scene view is the ONE game
	//! view rendering this frame (@p sceneIsRenderer) - the render invariant then
	//! guarantees the Game Preview / Play are not in a wireframe frame. Armed when
	//! the selected @p mode is Wireframe AND the Scene view is the renderer.
	//! (Classic ignores this - it wireframes per-target via RenderTexture::
	//! setViewMode; the editor calls both routes, each flavor honors its own.)
	bool shouldWireframeScene(Orkige::RenderViewMode mode, bool sceneIsRenderer);

	//! @brief should the Scene view arm the SHADED+WIREFRAME OVERLAY this frame
	//! (@see RenderWorld::setSceneWireframeOverlay)? The overlay adds a second
	//! wireframe renderable per scene mesh ON TOP of the untouched shaded pass, so
	//! - unlike the wireframe FLIP - it is not a global look-changing state; but it
	//! is still armed ONLY when the Scene view is the ONE game view rendering this
	//! frame (@p sceneIsRenderer), the same dock-tab discipline, so the overlay
	//! items' editor-only visibility bit never even gets a chance to reach a
	//! Game-Preview frame. Armed when @p mode is ShadedWireframe AND the Scene view
	//! is the renderer. Radio-exclusive with shouldWireframeScene by construction
	//! (the two modes are distinct RenderViewMode values).
	bool shouldWireframeOverlay(Orkige::RenderViewMode mode, bool sceneIsRenderer);

	//! @brief the stable string for a Scene-view mode (MCP value + ini persistence):
	//! "shaded" / "wireframe" / "shaded_wireframe"
	char const* sceneViewModeName(Orkige::RenderViewMode mode);

	//! @brief parse an MCP/persisted Scene-view mode string into a RenderViewMode;
	//! returns false (leaving @p outMode untouched) for an unknown name
	bool parseSceneViewMode(Orkige::String const& name,
		Orkige::RenderViewMode& outMode);
}

#endif //__EditorViewModes_h__25_7_2026__09_00_00__
