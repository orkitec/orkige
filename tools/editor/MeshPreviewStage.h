/********************************************************************
	created:	Monday 2026/07/14 at 10:00
	filename: 	MeshPreviewStage.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __MeshPreviewStage_h__14_7_2026__10_00_00__
#define __MeshPreviewStage_h__14_7_2026__10_00_00__

//! @file MeshPreviewStage.h
//! @brief the editor's static-snapshot 3D mesh preview: a `.glb` (or the
//! shared preview mesh for a `.omat`) rendered into an offscreen RTT from a
//! framed 3/4-view camera. The 3D twin of GuiPreviewStage / the
//! AnimationPreviewStage - shown in the Inspector for a selected mesh or
//! material asset.
//!
//! Facade discipline (the honest shape of this, @see the .cpp comment):
//! the engine has ONE render world and no facade knob to render a target
//! once or to confine a directional light, so this is NOT the task's literal
//! "render one frame then destroy" design. Instead the stage lives while the
//! asset is selected and its content sits FAR outside the editing scene
//! (a staging node past the editor camera's far clip, query-flags cleared so
//! picking/marquee never see it). Lighting is a pair of RANGE-BOUNDED POINT
//! lights at the staging origin - spatially confined so they never reach the
//! real scene 120000 units away (a directional light would light the whole
//! shared world every frame). The RTT then auto-renders each editor frame
//! exactly like the scene panel. clear() (selection change / deselect) tears
//! the whole rig down, so nothing lingers in the world.

#include <core_util/optr.h>
#include <core_util/String.h>

#include <string>

namespace Orkige
{
	class RenderTexture;
	class RenderCamera;
	class RenderNode;
	class RenderLight;
	class MeshInstance;
}

namespace OrkigeEditor
{
	//! @brief the introspection an Inspector mesh section shows + the honest
	//! degrade payload (bounds / submesh count) when the RTT is unavailable
	struct MeshPreviewInfo
	{
		bool		loaded = false;			//!< a mesh is staged
		std::string	meshName;				//!< the loaded mesh resource name
		int			subMeshCount = 0;		//!< number of sub-meshes
		float		sizeX = 0.0f;			//!< local AABB extents (world units)
		float		sizeY = 0.0f;
		float		sizeZ = 0.0f;
		float		boundingRadius = 0.0f;	//!< bounding-sphere radius
		std::string	material;				//!< applied material override ("" = imported)
	};

	//! @brief the shared mesh preview stage (@see the file comment)
	class MeshPreviewStage
	{
	public:
		MeshPreviewStage();
		~MeshPreviewStage();

		//! @brief stage a project-relative `.glb`/`.gltf`/... mesh, replacing
		//! whatever was loaded (registers the file's directory in the project
		//! resource group so a bare-name load resolves). Keeps the imported
		//! materials. @return false + getLastError() on a missing file / a mesh
		//! the backend could not load / no render system.
		bool load(std::string const& projectRoot, std::string const& meshRelPath,
			std::string& outError);
		//! @brief stage an ALREADY-REGISTERED mesh by bare resource name (the
		//! shared `.omat` preview surface - a lit mesh from the editor media),
		//! then apply @p materialName as an override (empty keeps imported).
		//! @return false + getLastError() when the mesh or material fails.
		bool loadNamedMesh(std::string const& meshName,
			std::string const& materialName, std::string& outError);
		//! @brief re-apply a material override to the staged mesh (the live
		//! `.omat` edit path - the caller updates the material via
		//! RenderSystem::createMaterial first, then re-points the mesh at it).
		//! Empty restores the imported materials (a fresh reload). @return false
		//! when nothing is staged or the material does not apply.
		bool setMaterial(std::string const& materialName, std::string& outError);
		//! @brief drop the staged mesh, lights, camera and target (empty state)
		void clear();

		//! is a mesh currently staged and rendering?
		bool isLoaded() const { return this->mMesh != nullptr; }
		//! the project-relative (or bare) name of the staged mesh ("" when none)
		std::string const& getLoadedFile() const { return this->mLoadedFile; }

		//! @brief the square pixel side the preview renders at (a change
		//! recreates the target on the next load). Clamped to a sane range.
		void setSize(int side);
		int getSize() const { return this->mSide; }

		//! @brief the orbit yaw/pitch (degrees) the framed camera views from.
		//! A change repositions the camera; the next editor frame shows it.
		void setOrbit(float yawDeg, float pitchDeg);
		//! @brief add to the orbit (a mouse-drag on the preview image); pitch is
		//! clamped to keep the model from flipping
		void addOrbit(float deltaYawDeg, float deltaPitchDeg);
		float getYaw() const { return this->mYawDeg; }
		float getPitch() const { return this->mPitchDeg; }

		//! the offscreen target the mesh renders into (null until loaded) - the
		//! Inspector shows this inside an ImGui image
		Orkige::optr<Orkige::RenderTexture> getTarget() const { return this->mTarget; }

		//! @brief force one render of the preview (a headless screenshot / the
		//! selfcheck) and write it to a PNG. Renders synchronously so the file
		//! reflects the CURRENT orbit. @return false + getLastError() when
		//! nothing is staged or the write fails.
		bool renderToPng(std::string const& pngPath, std::string& outError);

		//! @brief the introspection for the staged mesh (@see MeshPreviewInfo);
		//! valid even when the RTT could not be created (bounds/submesh degrade)
		MeshPreviewInfo getInfo() const { return this->mInfo; }
		//! the last failure message (set by load/setMaterial/renderToPng)
		std::string const& getLastError() const { return this->mLastError; }

	private:
		//! build camera + lights + target around the already-staged mesh, frame
		//! it, and record mInfo. Returns false + mLastError on a target failure
		//! (mInfo stays filled - the caller can still show the degrade text).
		bool buildRig();
		//! place the framed camera from the current orbit + framing
		void positionCamera();
		//! tear the whole rig (mesh, lights, camera, node, target) down
		void teardown();

		Orkige::optr<Orkige::RenderNode>	mStagingNode;	//!< far origin, holds everything
		Orkige::optr<Orkige::MeshInstance>	mMesh;			//!< the staged mesh (null = empty)
		Orkige::optr<Orkige::RenderNode>	mCameraNode;
		Orkige::optr<Orkige::RenderCamera>	mCamera;
		Orkige::optr<Orkige::RenderNode>	mKeyLightNode;
		Orkige::optr<Orkige::RenderLight>	mKeyLight;
		Orkige::optr<Orkige::RenderNode>	mFillLightNode;
		Orkige::optr<Orkige::RenderLight>	mFillLight;
		Orkige::optr<Orkige::RenderTexture>	mTarget;

		std::string	mLoadedFile;	//!< project-relative / bare name ("" = none)
		std::string	mMaterial;		//!< the applied material override ("" = imported)
		std::string	mLastError;
		MeshPreviewInfo	mInfo;

		int		mSide = 320;		//!< target pixel side
		float	mYawDeg = 35.0f;	//!< orbit around the model
		float	mPitchDeg = 22.0f;
		// framing (world space), recorded when the mesh is staged
		float	mCenterX = 0.0f, mCenterY = 0.0f, mCenterZ = 0.0f;
		float	mDistance = 1.0f;	//!< camera distance from the centre
		float	mRadius = 1.0f;		//!< bounding-sphere radius
	};
}

#endif //__MeshPreviewStage_h__14_7_2026__10_00_00__
