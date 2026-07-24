/********************************************************************
	created:	Friday 2026/07/24 at 12:00
	filename: 	GamePreviewStage.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file GamePreviewStage.cpp
//! @brief the shared editor Game Preview stage (@see GamePreviewStage.h)

#include "GamePreviewStage.h"

#include <engine_render/RenderSystem.h>
#include <engine_render/RenderTexture.h>
#include <engine_render/RenderWorld.h>
#include <engine_render/RenderCamera.h>
#include <engine_render/RenderNode.h>
#include <engine_gocomponent/CameraComponent.h>
#include <engine_gocomponent/TransformComponent.h>
#include <engine_gocomponent/WaterComponent.h>
#include <core_game/GameObjectManager.h>
#include <core_game/GameObject.h>
#include <core_game/GameObjectComponent.h>
#include <core_util/CameraFit.h>

namespace OrkigeEditor
{
	using namespace Orkige;

	namespace
	{
		//! a neutral device-frame backdrop where the scene renders nothing
		const Color GAME_PREVIEW_BACKGROUND(0.09f, 0.10f, 0.12f, 1.0f);
		//! the perspective clip fallbacks when no window camera answers (the
		//! editor's own perspective near/far)
		const float PREVIEW_NEAR = 1.0f;
		const float PREVIEW_FAR = 100000.0f;
		//! the perspective FOV fallback (matches the engine/editor default)
		const float PREVIEW_FOV_DEG = 45.0f;
		//! the DEFAULT window-camera view a camera-less scene renders through -
		//! the exact pose the player boots with (tools/player/main.cpp: position
		//! (0, 2.5, 9) looking at the origin), so the preview mirrors the game
		const Vec3 DEFAULT_CAMERA_POSITION(0.0f, 2.5f, 9.0f);
	}

	//---------------------------------------------------------
	GamePreviewStage::GamePreviewStage(std::string const& targetName)
		: mTargetName(targetName)
		, mContextDirty(true)
		, mHasCamera(false)
		, mUsedDefaultCamera(false)
	{
	}
	//---------------------------------------------------------
	GamePreviewStage::~GamePreviewStage()
	{
		// the overlay holds DrawLayer2D handles into the target - drop it first
		this->mOverlay.setExternalTarget(optr<RenderTexture>());
		this->mOverlay.clear();
		this->teardown();
	}
	//---------------------------------------------------------
	bool GamePreviewStage::setContext(GuiPreviewContext const& context)
	{
		if(this->mContext == context)
		{
			return false;
		}
		this->mContext = context;
		this->mContextDirty = true;
		// the overlay bakes content scale + safe area at build time
		this->mOverlay.setContext(context);
		return true;
	}
	//---------------------------------------------------------
	bool GamePreviewStage::ensureTarget()
	{
		if(this->mTarget && !this->mContextDirty)
		{
			return true;
		}
		RenderSystem* render = RenderSystem::get();
		if(!render)
		{
			this->mLastError = "no render system";
			return false;
		}
		RenderWorld* world = render->getWorld();
		if(!world)
		{
			this->mLastError = "no render world";
			return false;
		}
		const unsigned int width = this->mContext.width > 0
			? this->mContext.width : 16u;
		const unsigned int height = this->mContext.height > 0
			? this->mContext.height : 16u;
		if(!this->mTarget)
		{
			this->mTarget = render->createRenderTexture(this->mTargetName,
				width, height);
			if(!this->mTarget)
			{
				this->mLastError = "could not create the preview render target";
				return false;
			}
			this->mCamera = world->createCamera(this->mTargetName + "Camera");
			this->mCameraNode = world->createNode(this->mTargetName + "Node");
			this->mCamera->attachTo(this->mCameraNode);
			// a VALID projection from the start: setCamera arms the target's
			// render (incl. any shadow-texture pass) and update() only runs on a
			// later frame - a camera with an unset / 0 near clip would make the
			// classic shadow-camera setup assert (nearPlane > 0)
			this->mCamera->setPerspective(Degree(PREVIEW_FOV_DEG),
				PREVIEW_NEAR, PREVIEW_FAR);
			this->mTarget->setCamera(this->mCamera);
			this->mTarget->setBackgroundColour(GAME_PREVIEW_BACKGROUND);
			// shadows per the scene's arm state, like the Scene RTT (the classic
			// PSSM shadow-split near is seeded from a sane window camera now, so
			// the arbitrary preview camera no longer degenerates it - @see
			// RenderSystemClassic showUIOnlyWindow)
			this->mTarget->setShadowsEnabled(true);
			// window 2D overlays (ImGui/Gui through the window path) never leak
			// into the preview - the `.oui` overlay uses per-target createLayer
			this->mTarget->setOverlaysEnabled(false);
			// EDITOR-ONLY content (the ground grid, camera-frustum gizmos) must
			// NOT appear in the game preview - mask off the editor-only bit (the
			// Scene RTT keeps the default and still shows them)
			this->mTarget->setVisibilityMask(0xFFFFFFFFu & ~EDITOR_ONLY_VISIBILITY);
		}
		else
		{
			this->mTarget->resize(width, height);
		}
		// point the `.oui` overlay at THIS target (composites after the scene
		// pass); a resize marks the overlay dirty so it rebuilds its layers
		this->mOverlay.setExternalTarget(this->mTarget);
		this->mContextDirty = false;
		return true;
	}
	//---------------------------------------------------------
	void GamePreviewStage::teardown()
	{
		// order: the camera rides the node, both ride in the world; drop the
		// target's camera binding by dropping the target last
		this->mCamera.reset();
		this->mCameraNode.reset();
		this->mTarget.reset();
	}
	//---------------------------------------------------------
	bool GamePreviewStage::setOverlayScreen(std::string const& projectRoot,
		std::string const& ouiRelPath, std::string& outError)
	{
		if(!this->ensureTarget())
		{
			outError = this->mLastError;
			return false;
		}
		const bool ok = this->mOverlay.show(projectRoot, ouiRelPath, outError);
		if(!ok)
		{
			this->mLastError = this->mOverlay.getLastError();
		}
		return ok;
	}
	//---------------------------------------------------------
	std::string const& GamePreviewStage::getOverlayScreen() const
	{
		return this->mOverlay.getLoadedFile();
	}
	//---------------------------------------------------------
	void GamePreviewStage::loadLocalisation(Project const& project)
	{
		this->mOverlay.loadLocalisation(project);
	}
	//---------------------------------------------------------
	std::vector<std::string> GamePreviewStage::getLanguages() const
	{
		return this->mOverlay.getLanguages();
	}
	//---------------------------------------------------------
	void GamePreviewStage::setPreviewLanguage(std::string const& language)
	{
		this->mOverlay.setPreviewLanguage(language);
	}
	//---------------------------------------------------------
	std::string const& GamePreviewStage::getPreviewLanguage() const
	{
		return this->mOverlay.getPreviewLanguage();
	}
	//---------------------------------------------------------
	Orkige::GameObject* resolveActiveSceneCamera(
		Orkige::GameObjectManager& world)
	{
		// the active scene camera: a "Main Camera" if present, else the first
		// object carrying a CameraComponent (stable map order). Shared between
		// the preview's camera tracking and the Hierarchy's camera-owner glyph
		// so the two never disagree about which camera "is" the game view.
		auto usable = [](GameObject* go) -> bool
		{
			return go && go->hasComponent<CameraComponent>() &&
				go->hasComponent<TransformComponent>();
		};
		GameObject* first = NULL;
		for(auto const& entry : world.getGameObjects())
		{
			GameObject* go = entry.second.get();
			if(!usable(go))
			{
				continue;
			}
			if(entry.first.rfind("Main Camera", 0) == 0)
			{
				return go;
			}
			if(!first)
			{
				first = go;
			}
		}
		return first;
	}
	//---------------------------------------------------------
	GameObject* GamePreviewStage::resolveCamera(GameObjectManager& world,
		std::string const& sourceCameraId) const
	{
		if(!sourceCameraId.empty())
		{
			GameObject* go = world.getGameObject(sourceCameraId).lock().get();
			return (go && go->hasComponent<CameraComponent>() &&
				go->hasComponent<TransformComponent>()) ? go : NULL;
		}
		return resolveActiveSceneCamera(world);
	}
	//---------------------------------------------------------
	void GamePreviewStage::update(GameObjectManager& world,
		std::string const& sourceCameraId, bool animateMaterials,
		float deltaSeconds)
	{
		if(!this->ensureTarget())
		{
			this->mHasCamera = false;
			this->mUsedDefaultCamera = false;
			this->mTrackedId.clear();
			return;
		}

		// ANIMATE MATERIALS: advance ONLY material-parameter animation - the same
		// per-frame call the player loop makes via WaterComponent's tick
		// (RenderSystem::setWaterTime). No scripts, physics, particles or any
		// other gameplay ticks - the world stays dormant. If future components
		// drive per-frame material times, tick them here too (this is the seam).
		if(animateMaterials && deltaSeconds > 0.0f)
		{
			for(auto const& entry : world.getGameObjects())
			{
				GameObject* go = entry.second.get();
				if(go && go->hasComponent<WaterComponent>())
				{
					// onUpdateComponent is public on the base; the water override
					// advances only the scroll clock + setWaterTime (no gameplay)
					GameObjectComponent* water =
						go->getComponentPtr<WaterComponent>();
					water->onUpdateComponent(deltaSeconds);
				}
			}
		}

		const float aspect = (this->mContext.width > 0 &&
			this->mContext.height > 0)
			? static_cast<float>(this->mContext.width) /
				static_cast<float>(this->mContext.height)
			: 1.0f;

		GameObject* camObject = this->resolveCamera(world, sourceCameraId);
		if(!camObject)
		{
			// NO CameraComponent: a camera-less scene still RUNS in the game
			// through the default window camera, so the preview mirrors THAT
			// exact state (the player's default view) instead of refusing.
			this->mHasCamera = false;
			this->mUsedDefaultCamera = true;
			this->mTrackedId.clear();
			this->mCameraNode->setPosition(DEFAULT_CAMERA_POSITION);
			this->mCameraNode->lookAt(Vec3::ZERO, RenderNode::TS_WORLD);
			this->mCamera->setPerspective(Degree(PREVIEW_FOV_DEG),
				PREVIEW_NEAR, PREVIEW_FAR);
			this->mCamera->setAspectRatio(aspect);
			// still tick the overlay so a loaded screen keeps compositing
			this->mOverlay.tick(deltaSeconds);
			return;
		}
		this->mHasCamera = true;
		this->mUsedDefaultCamera = false;
		this->mTrackedId = camObject->getObjectID();

		CameraComponent* cc = camObject->getComponentPtr<CameraComponent>();
		TransformComponent* tc = camObject->getComponentPtr<TransformComponent>();

		// place the preview camera on the source object's world pose
		this->mCameraNode->setPosition(tc->getWorldPosition());
		this->mCameraNode->setOrientation(tc->getWorldOrientation());

		// copy the reflected projection. The perspective FOV / clips come from
		// the window camera exactly as the game's CameraComponent applies them
		// (it reuses the window camera's own FOVy); fall back when headless.
		optr<RenderCamera> windowCam = RenderSystem::get()->getWindowCamera();
		const Degree fov = windowCam ? windowCam->getFOVy()
			: Degree(PREVIEW_FOV_DEG);
		Real nearClip = windowCam ? windowCam->getNearClip() : PREVIEW_NEAR;
		Real farClip = windowCam ? windowCam->getFarClip() : PREVIEW_FAR;
		// a UI-only editor window can report a 0 / degenerate near clip (the
		// classic backend asserts nearPlane > 0 in setNearClipDistance) - clamp
		// to the sane engine defaults so the preview camera is always valid
		if(nearClip <= 0.0f) { nearClip = PREVIEW_NEAR; }
		if(farClip <= nearClip) { farClip = PREVIEW_FAR; }
		if(cc->getProjectionMode() == CameraComponent::PM_ORTHOGRAPHIC)
		{
			// the 2D fit policy sizes the half-extent against the DEVICE aspect
			float halfExtent = cc->getOrthoSize();
			if(cc->getFitMode() != CameraComponent::FM_HEIGHT)
			{
				halfExtent = CameraFit::orthoHalfHeight(
					static_cast<CameraFit::FitMode>(cc->getFitMode()),
					cc->getDesignWidth(), cc->getDesignHeight(), aspect);
			}
			this->mCamera->setOrthographic(halfExtent, nearClip, farClip);
		}
		else
		{
			this->mCamera->setPerspective(fov, nearClip, farClip);
		}
		this->mCamera->setAspectRatio(aspect);

		// tick the overlay so it lays out + submits into the composite target
		this->mOverlay.tick(deltaSeconds);
	}
	//---------------------------------------------------------
	std::vector<GuiPreviewWidgetRect> GamePreviewStage::getOverlayWidgetRects() const
	{
		return this->mOverlay.getWidgetRects();
	}
	//---------------------------------------------------------
	bool GamePreviewStage::renderAndCapture(std::string const& pngPath,
		std::string& outError)
	{
		if(!this->mTarget)
		{
			this->mLastError = "the game preview has not been built yet";
			outError = this->mLastError;
			return false;
		}
		// resubmit the overlay, render the backend frame depth so the composite
		// target holds the CURRENT scene + overlay (multi-buffered 2D batches
		// settle within a few frames - the DrawLayer2D visibility contract),
		// then read it back. The MCP pump runs before the editor's own
		// NewFrame/renderOneFrame, so the extra renders here are safe.
		this->mOverlay.tick(0.0f);
		for(int settleFrame = 0; settleFrame < 3; ++settleFrame)
		{
			RenderSystem::get()->renderOneFrame();
		}
		try
		{
			this->mTarget->writeContentsToFile(pngPath);
		}
		catch(std::exception const& e)
		{
			this->mLastError = std::string("could not write the screenshot: ") +
				e.what();
			outError = this->mLastError;
			return false;
		}
		return true;
	}
}
