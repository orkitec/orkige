/********************************************************************
	created:	Monday 2026/07/14 at 10:00
	filename: 	MeshPreviewStage.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file MeshPreviewStage.cpp
//! @brief the shared editor mesh preview stage (@see MeshPreviewStage.h)

#include "MeshPreviewStage.h"

#include <engine_render/RenderSystem.h>
#include <engine_render/RenderWorld.h>
#include <engine_render/RenderTexture.h>
#include <engine_render/RenderCamera.h>
#include <engine_render/RenderNode.h>
#include <engine_render/RenderLight.h>
#include <engine_render/MeshInstance.h>
#include <engine_render/RenderMath.h>
#include <core_project/Project.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace OrkigeEditor
{
	using namespace Orkige;

	namespace
	{
		//! the staged content lives FAR outside the editing scene: past the
		//! editor camera's 100000-unit far clip (so the main view never draws
		//! it) yet moderate enough that float precision on a preview-scale mesh
		//! stays adequate (only this one axis is large). @see the header.
		//! Per-instance stages step along +X so their content and lights never
		//! reach each other (the offset dwarfs any preview-scale mesh).
		const Vec3 STAGING_ORIGIN(0.0f, 120000.0f, 0.0f);
		const float STAGING_SLOT_STRIDE = 8000.0f;
		//! a neutral studio background so light and dark materials both read
		const Color PREVIEW_BACKGROUND(0.16f, 0.17f, 0.19f, 1.0f);
		//! the preview target's backend name (one stage per editor in practice)
		const char* PREVIEW_TARGET_NAME = "EditorMeshPreviewRT";

		//! the orbit direction (unit) for a yaw/pitch, matching EditorCamera's
		//! convention (yaw about +Y, pitch up); the camera sits centre + dir*d
		Vec3 orbitDirection(float yawDeg, float pitchDeg)
		{
			const float yaw = Radian(Degree(yawDeg)).valueRadians();
			const float pitch = Radian(Degree(pitchDeg)).valueRadians();
			return Vec3(
				std::cos(pitch) * std::sin(yaw),
				std::sin(pitch),
				std::cos(pitch) * std::cos(yaw));
		}
	}

	//---------------------------------------------------------
	MeshPreviewStage::MeshPreviewStage(int instanceSlot)
		: mInstanceSlot(instanceSlot) {}
	//---------------------------------------------------------
	MeshPreviewStage::~MeshPreviewStage()
	{
		this->teardown();
	}
	//---------------------------------------------------------
	void MeshPreviewStage::setSize(int side)
	{
		this->mSide = std::clamp(side, 64, 1024);
	}
	//---------------------------------------------------------
	void MeshPreviewStage::setOrbit(float yawDeg, float pitchDeg)
	{
		this->mYawDeg = yawDeg;
		this->mPitchDeg = std::clamp(pitchDeg, -85.0f, 85.0f);
		if(this->mCamera)
		{
			this->positionCamera();
		}
	}
	//---------------------------------------------------------
	void MeshPreviewStage::addOrbit(float deltaYawDeg, float deltaPitchDeg)
	{
		this->setOrbit(this->mYawDeg + deltaYawDeg,
			this->mPitchDeg + deltaPitchDeg);
	}
	//---------------------------------------------------------
	bool MeshPreviewStage::load(std::string const& projectRoot,
		std::string const& meshRelPath, std::string& outError)
	{
		this->mLastError.clear();
		this->teardown();
		if(meshRelPath.empty())
		{
			return true;	// cleared
		}
		RenderSystem* render = RenderSystem::get();
		if(!render)
		{
			this->mLastError = "no render system";
			outError = this->mLastError;
			return false;
		}
		namespace fs = std::filesystem;
		const fs::path abs = fs::path(projectRoot) / meshRelPath;
		std::error_code ec;
		if(!fs::is_regular_file(abs, ec))
		{
			this->mLastError = "no such mesh file: " + meshRelPath;
			outError = this->mLastError;
			return false;
		}
		// register the file's directory in the project group so the mesh (and
		// a freshly imported one) resolves by bare name - the idempotent
		// remove-then-add the editor uses for project asset locations
		const std::string dir = abs.parent_path().string();
		render->removeResourceLocation(dir, Project::RESOURCE_GROUP_NAME);
		render->addResourceLocation(dir, RenderSystem::LT_FILESYSTEM,
			Project::RESOURCE_GROUP_NAME, false);

		const std::string bare = abs.filename().string();
		optr<MeshInstance> mesh = render->getWorld()->createMeshInstance(bare);
		if(!mesh)
		{
			this->mLastError = "the render backend could not load the mesh '" +
				bare + "'";
			outError = this->mLastError;
			return false;
		}
		this->mMesh = mesh;
		this->mLoadedFile = meshRelPath;
		this->mMaterial.clear();
		if(!this->buildRig())
		{
			outError = this->mLastError;
			return false;	// mInfo still filled for the degrade text
		}
		return true;
	}
	//---------------------------------------------------------
	bool MeshPreviewStage::loadNamedMesh(std::string const& meshName,
		std::string const& materialName, std::string& outError)
	{
		this->mLastError.clear();
		this->teardown();
		if(meshName.empty())
		{
			return true;
		}
		RenderSystem* render = RenderSystem::get();
		if(!render)
		{
			this->mLastError = "no render system";
			outError = this->mLastError;
			return false;
		}
		optr<MeshInstance> mesh = render->getWorld()->createMeshInstance(meshName);
		if(!mesh)
		{
			this->mLastError = "the preview mesh '" + meshName +
				"' is not available";
			outError = this->mLastError;
			return false;
		}
		this->mMesh = mesh;
		this->mLoadedFile = meshName;
		this->mMaterial.clear();
		if(!materialName.empty())
		{
			this->mMesh->setMaterial(materialName);
			this->mMaterial = materialName;
		}
		if(!this->buildRig())
		{
			outError = this->mLastError;
			return false;
		}
		this->mInfo.material = this->mMaterial;
		return true;
	}
	//---------------------------------------------------------
	bool MeshPreviewStage::setMaterial(std::string const& materialName,
		std::string& outError)
	{
		this->mLastError.clear();
		if(!this->mMesh)
		{
			this->mLastError = "no mesh staged";
			outError = this->mLastError;
			return false;
		}
		if(!materialName.empty() && !this->mMesh->setMaterial(materialName))
		{
			this->mLastError = "the material '" + materialName +
				"' did not apply";
			outError = this->mLastError;
			return false;
		}
		this->mMaterial = materialName;
		this->mInfo.material = materialName;
		return true;
	}
	//---------------------------------------------------------
	bool MeshPreviewStage::buildRig()
	{
		RenderSystem* render = RenderSystem::get();
		RenderWorld* world = render->getWorld();

		// per-instance staging origin: coexisting stages step apart on +X
		const Vec3 stagingOrigin = STAGING_ORIGIN +
			Vec3(this->mInstanceSlot * STAGING_SLOT_STRIDE, 0.0f, 0.0f);

		// stage the mesh far outside the editing scene, invisible to picking
		this->mStagingNode = world->createNode();
		this->mStagingNode->setPosition(stagingOrigin);
		this->mMesh->attachTo(this->mStagingNode);
		this->mMesh->setQueryFlags(0);	// marquee/picking never see it
		this->mMesh->setCastShadows(false);

		// frame from the local bounds (the node is un-rotated, so local extents
		// map straight to world extents around the staging origin)
		const AABB bounds = this->mMesh->getLocalBounds();
		Vec3 centre(0, 0, 0);
		Vec3 half(0.5f, 0.5f, 0.5f);
		if(!bounds.isNull() && bounds.isFinite())
		{
			centre = bounds.getCenter();
			half = bounds.getHalfSize();
		}
		this->mRadius = std::max(half.length(), 0.001f);
		const Vec3 worldCentre = stagingOrigin + centre;
		this->mCenterX = worldCentre.x;
		this->mCenterY = worldCentre.y;
		this->mCenterZ = worldCentre.z;
		// a 40deg vertical FOV frames the bounding sphere with a little margin
		this->mDistance = this->mRadius /
			std::sin(Radian(Degree(20.0f)).valueRadians()) * 1.15f;

		// record the introspection now - valid even if the target fails below
		this->mInfo = MeshPreviewInfo();
		this->mInfo.loaded = true;
		this->mInfo.meshName = this->mLoadedFile;
		this->mInfo.subMeshCount = static_cast<int>(this->mMesh->getNumSubMeshes());
		this->mInfo.sizeX = half.x * 2.0f;
		this->mInfo.sizeY = half.y * 2.0f;
		this->mInfo.sizeZ = half.z * 2.0f;
		this->mInfo.boundingRadius = this->mRadius;
		this->mInfo.material = this->mMaterial;

		// the framed camera
		this->mCamera = world->createCamera();
		this->mCameraNode = world->createNode();
		this->mCamera->attachTo(this->mCameraNode);
		this->mCamera->setPerspective(Degree(40.0f),
			std::max(this->mRadius * 0.05f, 0.01f),
			this->mDistance + this->mRadius * 4.0f);
		this->mCameraNode->setFixedYawAxis(true);
		this->positionCamera();

		// RANGE-BOUNDED point lights confined to the staging origin: they light
		// the mesh but never reach the real scene 120000 units away (a
		// directional light would light the whole shared world every frame)
		const float lightRange = this->mRadius * 40.0f;
		this->mKeyLight = world->createLight();
		this->mKeyLight->setType(RenderLight::LT_POINT);
		this->mKeyLight->setDiffuseColour(Color(1.15f, 1.1f, 1.02f, 1.0f));
		this->mKeyLight->setRange(lightRange);
		this->mKeyLightNode = world->createNode();
		this->mKeyLightNode->setPosition(worldCentre +
			orbitDirection(this->mYawDeg + 25.0f, 45.0f) * (this->mRadius * 3.0f));
		this->mKeyLight->attachTo(this->mKeyLightNode);

		this->mFillLight = world->createLight();
		this->mFillLight->setType(RenderLight::LT_POINT);
		this->mFillLight->setDiffuseColour(Color(0.35f, 0.4f, 0.5f, 1.0f));
		this->mFillLight->setRange(lightRange);
		this->mFillLightNode = world->createNode();
		this->mFillLightNode->setPosition(worldCentre +
			orbitDirection(this->mYawDeg - 130.0f, 10.0f) * (this->mRadius * 3.0f));
		this->mFillLight->attachTo(this->mFillLightNode);

		// the offscreen target the mesh renders into (auto-updates each frame);
		// the name is per-instance so a second stage owns a distinct target
		const std::string targetName = this->mInstanceSlot == 0
			? std::string(PREVIEW_TARGET_NAME)
			: PREVIEW_TARGET_NAME + std::to_string(this->mInstanceSlot);
		this->mTarget = render->createRenderTexture(targetName,
			static_cast<unsigned int>(this->mSide),
			static_cast<unsigned int>(this->mSide));
		if(!this->mTarget)
		{
			this->mLastError = "could not create the mesh preview target";
			return false;	// mInfo is filled - the caller degrades to text
		}
		this->mTarget->setBackgroundColour(PREVIEW_BACKGROUND);
		this->mTarget->setOverlaysEnabled(false);
		this->mTarget->setShadowsEnabled(false);
		this->mTarget->setCamera(this->mCamera);
		return true;
	}
	//---------------------------------------------------------
	void MeshPreviewStage::positionCamera()
	{
		if(!this->mCameraNode)
		{
			return;
		}
		const Vec3 centre(this->mCenterX, this->mCenterY, this->mCenterZ);
		const Vec3 eye = centre +
			orbitDirection(this->mYawDeg, this->mPitchDeg) * this->mDistance;
		this->mCameraNode->setPosition(eye);
		this->mCameraNode->lookAt(centre, RenderNode::TS_WORLD);
	}
	//---------------------------------------------------------
	bool MeshPreviewStage::renderToPng(std::string const& pngPath,
		std::string& outError)
	{
		if(!this->mMesh || !this->mTarget)
		{
			this->mLastError = "nothing is staged in the mesh preview";
			outError = this->mLastError;
			return false;
		}
		// render one frame so the offscreen target holds the current orbit,
		// then read it back (the MCP/selfcheck pump runs before the editor's
		// own renderOneFrame, so an extra render here is safe)
		RenderSystem::get()->renderOneFrame();
		try
		{
			this->mTarget->writeContentsToFile(pngPath);
		}
		catch(std::exception const& e)
		{
			this->mLastError = std::string("could not write the preview PNG: ") +
				e.what();
			outError = this->mLastError;
			return false;
		}
		outError.clear();
		return true;
	}
	//---------------------------------------------------------
	void MeshPreviewStage::clear()
	{
		this->teardown();
	}
	//---------------------------------------------------------
	void MeshPreviewStage::teardown()
	{
		// the target references the camera; drop it first, then the scene
		// content (each optr reset detaches + destroys its facade object)
		this->mTarget.reset();
		this->mCamera.reset();
		this->mCameraNode.reset();
		this->mKeyLight.reset();
		this->mKeyLightNode.reset();
		this->mFillLight.reset();
		this->mFillLightNode.reset();
		this->mMesh.reset();
		this->mStagingNode.reset();
		this->mLoadedFile.clear();
		this->mMaterial.clear();
		this->mInfo = MeshPreviewInfo();
	}
}
