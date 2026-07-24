/********************************************************************
	created:	Friday 2026/07/24 at 12:00
	filename: 	GamePreviewStage.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __GamePreviewStage_h__24_7_2026__12_00_00__
#define __GamePreviewStage_h__24_7_2026__12_00_00__

//! @file GamePreviewStage.h
//! @brief the editor's Game Preview stage: the authored scene rendered through
//! its OWN scene camera (a CameraComponent's reflected projection + its object's
//! world transform) into an offscreen RenderTexture at a simulated DEVICE
//! context (resolution / content scale / safe-area), with an optional `.oui`
//! SCREEN composited on top through the shared GuiPreviewStage machinery. It
//! answers "what will this look like on the device?" WITHOUT playing: the world
//! stays dormant - no scripts, no gameplay ticking, ever (the editor safety
//! contract). The one exception is an opt-in ANIMATE MATERIALS clock that
//! advances material-parameter animation only (RenderSystem::setWaterTime).
//! ONE stage instance backs the Game Preview tab (the human's live view) and
//! the preview_game MCP verb (an agent's screenshots); the Scene panel's
//! selected-camera picture-in-picture inset uses a SECOND instance pointed at a
//! different camera source.

#include "GuiPreviewStage.h"

#include <core_util/optr.h>
#include <core_util/String.h>

#include <string>
#include <vector>

namespace Orkige
{
	class RenderTexture;
	class RenderCamera;
	class RenderNode;
	class GameObject;
	class GameObjectManager;
	class Project;
}

namespace OrkigeEditor
{
	//! @brief the camera object the editor treats as THE game camera of the
	//! current scene: a "Main Camera" if present, else the first object
	//! carrying a CameraComponent (stable map order) - the exact rule the Game
	//! Preview tracks with no explicit source. Shared with the Hierarchy's
	//! camera-owner glyph so both surfaces agree. NULL when no usable camera
	//! (CameraComponent + TransformComponent) exists.
	Orkige::GameObject* resolveActiveSceneCamera(
		Orkige::GameObjectManager& world);

	//! @brief the render visibility-flag bit that EDITOR-ONLY 3D content (the
	//! ground grid, the camera-frustum gizmos) carries so the Game Preview RTT
	//! can mask it OFF (MeshInstance::setVisibilityFlags + RenderTexture::
	//! setVisibilityMask) while the Scene RTT keeps the default and shows it. A
	//! high user bit, clear of the 2D/UI visibility scheme (bits 0..~5 in use).
	static const unsigned int EDITOR_ONLY_VISIBILITY = 0x00400000u;

	//! @brief the composited scene-through-a-scene-camera preview stage
	//! (@see the file comment). Owns a scene RTT + camera + node; the `.oui`
	//! overlay rides an internal GuiPreviewStage in external-target mode.
	class GamePreviewStage
	{
	public:
		//! @param targetName a unique backend target name (two coexisting
		//! instances - the panel + the Scene-panel inset - must not collide)
		explicit GamePreviewStage(std::string const& targetName =
			"EditorGamePreviewRT");
		~GamePreviewStage();

		//! @brief set the simulated device context (RTT size, content scale,
		//! safe-area). A size change recreates the RTT + camera; the overlay is
		//! rebuilt against the new surface. @return true when it actually changed.
		bool setContext(GuiPreviewContext const& context);
		GuiPreviewContext const& getContext() const { return this->mContext; }

		//! @brief the `.oui` screen composited over the scene ("" = scene only).
		//! Registers the file's directory + loads it through the shared overlay
		//! (next-only: classic refuses with an honest error, the scene still
		//! renders). @return false + outError on a load failure.
		bool setOverlayScreen(std::string const& projectRoot,
			std::string const& ouiRelPath, std::string& outError);
		std::string const& getOverlayScreen() const;

		//--- localisation (forwarded to the overlay stage) ---
		void loadLocalisation(Orkige::Project const& project);
		std::vector<std::string> getLanguages() const;
		void setPreviewLanguage(std::string const& language);
		std::string const& getPreviewLanguage() const;

		//! @brief per-frame drive: resolve the camera SOURCE object, copy its
		//! reflected projection + world transform onto the preview camera, advance
		//! the material clock (when @p animateMaterials), and tick the overlay.
		//! @param world the editor's live authored world (never mutated as gameplay)
		//! @param sourceCameraId the object to track ("" = the active scene camera:
		//! a "Main Camera" if present, else the first CameraComponent object)
		//! @param animateMaterials advance RenderSystem::setWaterTime this frame
		//! @param deltaSeconds the material clock step (only used when animating)
		//! @remarks a scene with NO CameraComponent still RENDERS - through the
		//! same DEFAULT window-camera state the player boots with (pose (0,2.5,9)
		//! looking at the origin, perspective 45deg, near 1 / far 100000), so the
		//! preview mirrors what the game genuinely shows (@see usedDefaultCamera).
		void update(Orkige::GameObjectManager& world,
			std::string const& sourceCameraId, bool animateMaterials,
			float deltaSeconds);

		//! did the last update() resolve a scene CameraComponent to track?
		//! (false => it fell back to the default window camera - still renders)
		bool hasCamera() const { return this->mHasCamera; }
		//! @brief did the last update() render through the DEFAULT window camera
		//! (the scene has no CameraComponent)? The panel shows a subtle hint then.
		bool usedDefaultCamera() const { return this->mUsedDefaultCamera; }
		//! the object id the preview is tracking ("" = the default window camera)
		std::string const& getTrackedCameraId() const { return this->mTrackedId; }

		//! the composite target (scene + overlay); null until the first update/
		//! setContext builds it. The panel shows this inside an ImGui image and
		//! the MCP verb reads it back.
		Orkige::optr<Orkige::RenderTexture> getTarget() const { return this->mTarget; }

		//! @brief the resolved overlay widget rects (pixels, in the simulated
		//! surface space). EDIT-MODE SEAM: a future preview edit mode hit-tests
		//! over these to pick/drag widgets on top of the live scene - the rects
		//! stay addressable here on purpose. Empty when no overlay is loaded.
		std::vector<GuiPreviewWidgetRect> getOverlayWidgetRects() const;

		//! @brief render the composite once and write it to a PNG (the
		//! preview_game verb / a headless screenshot). Renders the backend frame
		//! depth so the offscreen target holds the CURRENT scene + overlay.
		//! @return false + outError when nothing can be captured or the write fails.
		bool renderAndCapture(std::string const& pngPath, std::string& outError);

		//! the last failure message (setOverlayScreen / renderAndCapture)
		std::string const& getLastError() const { return this->mLastError; }

	private:
		//! (re)create the scene RTT + camera at the current context; wires the
		//! overlay's external target to it. Returns false + sets mLastError when
		//! the render system cannot make the target.
		bool ensureTarget();
		//! drop the RTT + camera (the overlay is torn down by its own dtor)
		void teardown();
		//! resolve the source camera object; "" tracks the active scene camera
		Orkige::GameObject* resolveCamera(Orkige::GameObjectManager& world,
			std::string const& sourceCameraId) const;

		std::string							mTargetName;	//!< unique backend target name
		GuiPreviewContext					mContext;
		Orkige::optr<Orkige::RenderTexture>	mTarget;		//!< the composite scene RTT
		Orkige::optr<Orkige::RenderCamera>	mCamera;		//!< the preview scene camera
		Orkige::optr<Orkige::RenderNode>	mCameraNode;	//!< the camera's rig node
		GuiPreviewStage						mOverlay;		//!< the `.oui` overlay (external-target)
		bool								mContextDirty;	//!< the RTT needs (re)building
		bool								mHasCamera;		//!< last update resolved a CameraComponent
		bool								mUsedDefaultCamera;	//!< last update fell back to the default window camera
		std::string							mTrackedId;		//!< tracked camera object id
		std::string							mLastError;
	};
}

#endif //__GamePreviewStage_h__24_7_2026__12_00_00__
