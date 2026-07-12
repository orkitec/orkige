/********************************************************************
	created:	Wednesday 2026/07/08 at 22:00
	filename: 	DrawLayer2DNext.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file DrawLayer2DNext.cpp
//! @brief Ogre-Next implementation of the DrawLayer2D facade
//! @remarks The v2 shape of the 2D contract, built from the sprite
//! machinery: every batch is one v2 ManualObject (VaoManager-backed
//! dynamic buffers) with a generated "DrawLayer2D/<tex>" HlmsUnlit
//! datablock (alpha-blended, depth-IGNORED, two-sided, clamped point
//! sampling), living in the dedicated UI render queue (200, v2 FAST by
//! default) that ONLY the window workspace's late scene pass draws -
//! through a pixel-space orthographic camera, so vertex positions stay
//! pixels (x right, y down via negation). Ordering: alpha-blended
//! datablocks are transparent to the render queue, and transparents
//! sort back to front by camera depth - so every batch node gets a
//! unique depth from the global (layer zOrder, creation order,
//! submission order) walk, reassigned whenever content changes.
//! Colour parity note: since the backend went gamma-space passthrough
//! (non-sRGB swapchain/textures, see the boot's "gamma" note in
//! NextBackend.cpp) vertex colours upload RAW - values, blending and
//! screenshots byte-match the classic backend.
//! Scissors/indices never reach this backend: DrawLayer2DClip.h
//! resolves both into flat triangle lists at submission time.

#include "engine_render_next/NextBackend.h"
#include "engine_render/DrawLayer2DClip.h"

#include <OgreRoot.h>
#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreManualObject2.h>
#include <OgreCamera.h>
#include <OgreWindow.h>
#include <OgreHlmsManager.h>
#include <OgreHlms.h>
#include <OgreLogManager.h>
#include <OgreHlmsUnlit.h>
#include <OgreHlmsUnlitDatablock.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>
#include <vector>

namespace Orkige
{
	namespace
	{
		//! all live layers in creation order (creation order breaks
		//! zOrder ties, per the facade contract)
		std::vector<DrawLayer2D*> gDrawLayers;
		//! batch depths need reassignment before the next frame
		bool gOrderDirty = false;
		//! the pixel-space UI camera (owned by the scene manager)
		Ogre::Camera* gUICamera = NULL;
		//! window size the UI camera was last shaped for
		unsigned int gUICameraWidth = 0;
		unsigned int gUICameraHeight = 0;
		//! textures that already logged their load failure (log once)
		std::set<String> gFailedTextures;

		//! per-target 2D visibility bits handed out (bit 0 is the window's
		//! UI_WINDOW_VISIBILITY; targets take bits 1..29 of the user range).
		//! A simple free-bit mask - the editor uses at most one preview target
		unsigned int gUiVisibilityInUse = RenderBackend::UI_WINDOW_VISIBILITY;
		//! highest usable user visibility bit (Ogre-Next reserves the top 2
		//! bits for LAYER_VISIBILITY/LAYER_SHADOW_CASTER; the user range is the
		//! low 30, so bit 29 is the last one we can hand out)
		const unsigned int UI_VISIBILITY_LAST_BIT = 29u;
		bool gUiVisibilityExhaustedLogged = false;

		//! depth spacing between consecutive batches: comfortably above
		//! the render queue's transparent depth quantization at our far
		//! clip, so the painter order can never collapse into one key
		const Ogre::Real BATCH_DEPTH_SPACING = Ogre::Real(8.0);
		const Ogre::Real UI_CAMERA_FAR_CLIP = Ogre::Real(20000.0);
	}

	//---------------------------------------------------------
	//--- RenderBackend plumbing -------------------------------
	//---------------------------------------------------------
	char const * RenderBackend::drawLayer2DCameraName()
	{
		return "Orkige/DrawLayer2D/Camera";
	}
	//---------------------------------------------------------
	unsigned int RenderBackend::allocateUiVisibilityFlag()
	{
		for(unsigned int bit = 1u; bit <= UI_VISIBILITY_LAST_BIT; ++bit)
		{
			const unsigned int flag = 1u << bit;
			if((gUiVisibilityInUse & flag) == 0u)
			{
				gUiVisibilityInUse |= flag;
				return flag;
			}
		}
		if(!gUiVisibilityExhaustedLogged)
		{
			gUiVisibilityExhaustedLogged = true;
			Ogre::LogManager::getSingleton().logMessage(
				"Orkige next backend: offscreen 2D visibility bits exhausted - "
				"further preview targets fall back to the window band");
		}
		return 0u;
	}
	//---------------------------------------------------------
	void RenderBackend::freeUiVisibilityFlag(unsigned int flag)
	{
		// never free the window bit (bit 0)
		if(flag != 0u && flag != RenderBackend::UI_WINDOW_VISIBILITY)
		{
			gUiVisibilityInUse &= ~flag;
		}
	}
	//---------------------------------------------------------
	void RenderBackend::shapeUICamera(Ogre::Camera* camera,
		unsigned int width, unsigned int height)
	{
		oAssert(camera);
		if(width == 0u || height == 0u)
		{
			return;
		}
		// pixel-space ortho: (0,0) at the top-left, +x right, +y down
		// (vertex y is negated into the world's y-up at build time)
		camera->setOrthoWindow(Ogre::Real(width), Ogre::Real(height));
		camera->setPosition(Ogre::Vector3(
			Ogre::Real(width) * Ogre::Real(0.5),
			Ogre::Real(height) * Ogre::Real(-0.5),
			Ogre::Real(0.0)));
		camera->setOrientation(Ogre::Quaternion::IDENTITY);	// looks -Z
	}
	//---------------------------------------------------------
	Ogre::Camera* RenderBackend::ensureDrawLayer2DCamera()
	{
		if(gUICamera)
		{
			return gUICamera;
		}
		Ogre::SceneManager* sceneManager = RenderBackend::worldSceneManager();
		oAssert(sceneManager);
		gUICamera = sceneManager->createCamera(drawLayer2DCameraName());
		gUICamera->setProjectionType(Ogre::PT_ORTHOGRAPHIC);
		gUICamera->setNearClipDistance(Ogre::Real(1.0));
		gUICamera->setFarClipDistance(UI_CAMERA_FAR_CLIP);
		// the painter order lives in the batch nodes' z (see the depth
		// walk below); the default SortModeDistance would let each
		// batch's PLANAR position and AABB radius swamp that tiny depth
		// spacing - pure view-space depth is the only mode where the
		// assigned z alone decides the transparent draw order
		gUICamera->mSortMode = Ogre::Camera::SortModeDepthRadiusIgnoring;
		gUICameraWidth = gUICameraHeight = 0;	// shaped on the next update
		RenderBackend::updateDrawLayer2DFrame();
		return gUICamera;
	}
	//---------------------------------------------------------
	void RenderBackend::updateDrawLayer2DFrame()
	{
		if(!gUICamera || !RenderBackend::system())
		{
			return;
		}
		unsigned int windowWidth = 0, windowHeight = 0;
		RenderBackend::system()->getWindowSize(windowWidth, windowHeight);
		if(windowWidth > 0 && windowHeight > 0 &&
			(windowWidth != gUICameraWidth || windowHeight != gUICameraHeight))
		{
			RenderBackend::shapeUICamera(gUICamera, windowWidth, windowHeight);
			gUICameraWidth = windowWidth;
			gUICameraHeight = windowHeight;
		}
		if(gOrderDirty)
		{
			gOrderDirty = false;
			// reassign every batch node's depth in global draw order
			// (ascending layer zOrder, layer creation order, batch
			// submission order); earlier draws sit farther from the camera
			std::vector<DrawLayer2D*> ordered = gDrawLayers;
			std::stable_sort(ordered.begin(), ordered.end(),
				[](DrawLayer2D const * a, DrawLayer2D const * b)
				{ return a->getZOrder() < b->getZOrder(); });
			size_t totalBatches = 0;
			for(DrawLayer2D* layer : ordered)
			{
				totalBatches += layer->mImpl->batches.size();
			}
			size_t drawIndex = 0;
			for(DrawLayer2D* layer : ordered)
			{
				for(DrawLayer2D::Impl::Batch & batch : layer->mImpl->batches)
				{
					if(batch.node)
					{
						// drawIndex 0 farthest -> drawn first (back to front)
						const Ogre::Real depth = BATCH_DEPTH_SPACING *
							Ogre::Real(totalBatches - drawIndex);
						batch.node->setPosition(0.0f, 0.0f, -depth);
					}
					++drawIndex;
				}
			}
		}
	}
	//---------------------------------------------------------
	optr<DrawLayer2D> RenderBackend::createDrawLayer2D(int zOrder)
	{
		optr<DrawLayer2D> handle(new DrawLayer2D());
		handle->mImpl->zOrder = zOrder;
		gDrawLayers.push_back(handle.get());
		// the camera must exist before the workspace pass references it -
		// and the window workspace only carries the UI pass when a camera
		// is shown, so a rebuild picks the pass up in either order
		RenderBackend::ensureDrawLayer2DCamera();
		gOrderDirty = true;
		return handle;
	}
	//---------------------------------------------------------
	optr<DrawLayer2D> RenderBackend::createTargetDrawLayer2D(
		optr<RenderTexture> const & target, unsigned int visibilityFlag,
		int zOrder)
	{
		oAssert(target);
		optr<DrawLayer2D> handle(new DrawLayer2D());
		handle->mImpl->zOrder = zOrder;
		handle->mImpl->target = target;	// composites into the target, not the window
		// the target's UI pass masks to its own bit; the layer's batches carry
		// it so no other surface's pass draws them (RenderTexture::createLayer
		// ensured the bit + the per-target UI camera + workspace UI pass). A
		// 0 flag (pool exhausted) falls back to the window band, honestly.
		handle->mImpl->visibilityFlags = visibilityFlag != 0u
			? visibilityFlag : RenderBackend::UI_WINDOW_VISIBILITY;
		gDrawLayers.push_back(handle.get());
		gOrderDirty = true;
		return handle;
	}
	//---------------------------------------------------------
	void RenderBackend::unregisterDrawLayer2D(DrawLayer2D* layer)
	{
		gDrawLayers.erase(std::remove(gDrawLayers.begin(),
			gDrawLayers.end(), layer), gDrawLayers.end());
		gOrderDirty = true;
	}
	//---------------------------------------------------------
	void RenderBackend::markDrawLayer2DOrderDirty()
	{
		gOrderDirty = true;
	}
	//---------------------------------------------------------
	void RenderBackend::resetDrawLayer2DState()
	{
		// the camera and any batch objects die with the root's scene
		// manager; surviving facade handles free CPU memory only (their
		// dtors check RenderBackend::system())
		gUICamera = NULL;
		gUICameraWidth = gUICameraHeight = 0;
		gDrawLayers.clear();
		gFailedTextures.clear();
		gOrderDirty = false;
		gUiVisibilityInUse = RenderBackend::UI_WINDOW_VISIBILITY;
		gUiVisibilityExhaustedLogged = false;
	}
	//---------------------------------------------------------
	Ogre::HlmsDatablock* RenderBackend::getOrCreateDrawLayer2DDatablock(
		String const & textureName)
	{
		Ogre::HlmsManager* hlmsManager =
			RenderBackend::ogreRoot()->getHlmsManager();
		const String name = "DrawLayer2D/" + textureName;
		if(Ogre::HlmsDatablock* existing =
			hlmsManager->getDatablockNoDefault(name))
		{
			return existing;
		}
		Ogre::TextureGpu* texture = NULL;
		if(!textureName.empty())
		{
			texture = RenderBackend::loadTexture2D(textureName);
			if(!texture)
			{
				return NULL;	// load failure already logged
			}
		}
		// the facade's 2D render contract: unlit, alpha-blended,
		// depth-IGNORED (check AND write off - 2D composites over the
		// finished frame), two-sided; vertex colours flow automatically
		// from VES_DIFFUSE (hlms_colour)
		Ogre::HlmsUnlit* unlit = static_cast<Ogre::HlmsUnlit*>(
			hlmsManager->getHlms(Ogre::HLMS_UNLIT));
		Ogre::HlmsMacroblock macroblock;
		macroblock.mDepthCheck = false;
		macroblock.mDepthWrite = false;
		macroblock.mCullMode = Ogre::CULL_NONE;
		Ogre::HlmsBlendblock blendblock;
		blendblock.setBlendType(Ogre::SBT_TRANSPARENT_ALPHA);
		Ogre::HlmsUnlitDatablock* datablock =
			static_cast<Ogre::HlmsUnlitDatablock*>(unlit->createDatablock(
				name, name, macroblock, blendblock, Ogre::HlmsParamVec()));
		if(texture)
		{
			// clamped point sampling: crisp pixel UI, the Gorilla atlas
			// rule (identical to the classic backend's FO_NONE + clamp)
			Ogre::HlmsSamplerblock samplerblock;
			samplerblock.setFiltering(Ogre::TFO_NONE);
			samplerblock.setAddressingMode(Ogre::TAM_CLAMP);
			datablock->setTexture(0u, texture, &samplerblock);
		}
		RenderBackend::registerContentDatablock(datablock);
		return datablock;
	}
	//---------------------------------------------------------
	Ogre::HlmsDatablock* RenderBackend::getOrCreateDrawLayer2DRTTDatablock(
		optr<RenderTexture> const & renderTexture)
	{
		oAssert(renderTexture);
		Ogre::HlmsManager* hlmsManager =
			RenderBackend::ogreRoot()->getHlmsManager();
		const String name =
			"DrawLayer2D/RTT/" + RenderBackend::renderTextureName(renderTexture);
		Ogre::TextureGpu* current =
			RenderBackend::renderTextureGpu(renderTexture);
		if(Ogre::HlmsDatablock* existing =
			hlmsManager->getDatablockNoDefault(name))
		{
			// re-point at the target's current incarnation when it changed
			Ogre::HlmsUnlitDatablock* unlitBlock =
				static_cast<Ogre::HlmsUnlitDatablock*>(existing);
			if(unlitBlock->getTexture(0u) != current)
			{
				unlitBlock->setTexture(0u, current);
			}
			return existing;
		}
		// the 2D render contract minus the blending: RenderTexture batches
		// composite OPAQUE. Classic parity reasoning: classic RTTs carry no
		// alpha channel (PF_BYTE_RGB - sampling answers alpha 1), so its
		// alpha blend degenerates to a plain replace; this backend's RTTs
		// are RGBA whose alpha is a rendering BYPRODUCT (content is free to
		// write 0 - vertex-colour meshes do), and blending with it would
		// punch holes into the scene panel. Opaque = classic's exact
		// result. setForceTransparentRenderOrder keeps the batch in the
		// render queue's back-to-front TRANSPARENT path regardless - the
		// whole 2D painter order rides on that sorting (an opaque-path
		// object would jump before every blended UI batch).
		Ogre::HlmsUnlit* unlit = static_cast<Ogre::HlmsUnlit*>(
			hlmsManager->getHlms(Ogre::HLMS_UNLIT));
		Ogre::HlmsMacroblock macroblock;
		macroblock.mDepthCheck = false;
		macroblock.mDepthWrite = false;
		macroblock.mCullMode = Ogre::CULL_NONE;
		Ogre::HlmsBlendblock blendblock;
		blendblock.setForceTransparentRenderOrder(true);
		Ogre::HlmsUnlitDatablock* datablock =
			static_cast<Ogre::HlmsUnlitDatablock*>(unlit->createDatablock(
				name, name, macroblock, blendblock,
				Ogre::HlmsParamVec()));
		if(current)
		{
			Ogre::HlmsSamplerblock samplerblock;
			samplerblock.setFiltering(Ogre::TFO_NONE);
			samplerblock.setAddressingMode(Ogre::TAM_CLAMP);
			datablock->setTexture(0u, current, &samplerblock);
		}
		RenderBackend::registerContentDatablock(datablock);
		return datablock;
	}

	//---------------------------------------------------------
	//--- Impl batch objects -----------------------------------
	//---------------------------------------------------------
	void DrawLayer2D::Impl::buildBatchObject(Batch & batch)
	{
		Ogre::SceneManager* sceneManager = RenderBackend::worldSceneManager();
		oAssert(sceneManager);
		if(batch.triangles.empty())
		{
			return;	// nothing to draw - no backend objects
		}
		String datablockName;
		if(batch.renderTexture)
		{
			// offscreen-target batch: a per-target datablock re-pointed at
			// the target's CURRENT texture (resize-by-recreate safe; the
			// dying incarnation detaches itself, RenderTextureNext.cpp)
			Ogre::HlmsDatablock* datablock =
				RenderBackend::getOrCreateDrawLayer2DRTTDatablock(
					batch.renderTexture);
			oAssert(datablock);
			datablockName = "DrawLayer2D/RTT/" +
				RenderBackend::renderTextureName(batch.renderTexture);
		}
		else
		{
			String datablockTexture = batch.textureName;
			Ogre::HlmsDatablock* datablock =
				RenderBackend::getOrCreateDrawLayer2DDatablock(datablockTexture);
			if(!datablock && !datablockTexture.empty())
			{
				if(gFailedTextures.insert(datablockTexture).second)
				{
					Ogre::LogManager::getSingleton().logMessage(
						"Orkige next backend: DrawLayer2D texture '" +
						datablockTexture + "' not found - batch draws untextured");
				}
				datablockTexture.clear();
				datablock = RenderBackend::getOrCreateDrawLayer2DDatablock(
					datablockTexture);
			}
			oAssert(datablock);
			datablockName = "DrawLayer2D/" + datablockTexture;
		}
		batch.object = sceneManager->createManualObject(Ogre::SCENE_DYNAMIC);
		batch.object->setName(
			RenderBackend::generateName("RenderFacade/DrawLayer2D"));
		batch.object->setQueryFlags(0);	// UI never answers scene ray queries
		batch.object->setCastShadows(false);	// UI never throws scene shadows
		batch.object->estimateVertexCount(batch.triangles.size());
		batch.object->begin(datablockName, Ogre::OT_TRIANGLE_LIST);
		Ogre::uint32 index = 0;
		for(DrawLayer2D::Vertex2D const & vertex : batch.triangles)
		{
			// pixel space -> world: +y down becomes -y (the UI camera sits
			// over the negated rect); depth rides on the node. Colours go
			// RAW (gamma-space passthrough - classic parity)
			batch.object->position(vertex.x, -vertex.y, 0.0f);
			batch.object->colour(Ogre::ColourValue(vertex.colour.r,
				vertex.colour.g, vertex.colour.b, vertex.colour.a));
			batch.object->textureCoord(vertex.u, vertex.v);
			batch.object->index(index++);
		}
		batch.object->end();
		batch.object->setRenderQueueGroup(
			RenderBackend::DRAWLAYER2D_RENDER_QUEUE);
		batch.node = sceneManager->getRootSceneNode(Ogre::SCENE_DYNAMIC)
			->createChildSceneNode(Ogre::SCENE_DYNAMIC);
		batch.node->attachObject(batch.object);
		// v2 asserts on setVisible for unattached objects - visibility
		// only after the attach
		batch.object->setVisible(this->visible);
		// the surface band: window batches carry UI_WINDOW_VISIBILITY, an
		// offscreen-target layer carries the target's bit, so only the
		// matching UI pass draws this batch (window vs. the target's pass)
		batch.object->setVisibilityFlags(this->visibilityFlags);
		gOrderDirty = true;
	}
	//---------------------------------------------------------
	void DrawLayer2D::Impl::destroyBatchObject(Batch & batch)
	{
		if(batch.object)
		{
			if(batch.object->isAttached())
			{
				batch.object->detachFromParent();
			}
			RenderBackend::worldSceneManager()->destroyManualObject(batch.object);
			batch.object = NULL;
		}
		if(batch.node)
		{
			batch.node->getParentSceneNode()->removeAndDestroyChild(batch.node);
			batch.node = NULL;
		}
	}

	//---------------------------------------------------------
	//--- facade methods ---------------------------------------
	//---------------------------------------------------------
	DrawLayer2D::DrawLayer2D()
		: mImpl(new Impl())
	{
	}
	//---------------------------------------------------------
	DrawLayer2D::~DrawLayer2D()
	{
		RenderBackend::unregisterDrawLayer2D(this);
		// late destruction guard, same rule as the other facade handles:
		// after the backend died there is nothing left to destroy
		if(RenderBackend::system())
		{
			for(Impl::Batch & batch : this->mImpl->batches)
			{
				this->mImpl->destroyBatchObject(batch);
			}
		}
		delete this->mImpl;
	}
	//---------------------------------------------------------
	void DrawLayer2D::setVisible(bool visible)
	{
		this->mImpl->visible = visible;
		for(Impl::Batch & batch : this->mImpl->batches)
		{
			if(batch.object)
			{
				batch.object->setVisible(visible);
			}
		}
	}
	//---------------------------------------------------------
	bool DrawLayer2D::isVisible() const
	{
		return this->mImpl->visible;
	}
	//---------------------------------------------------------
	void DrawLayer2D::setZOrder(int zOrder)
	{
		if(this->mImpl->zOrder != zOrder)
		{
			this->mImpl->zOrder = zOrder;
			gOrderDirty = true;
		}
	}
	//---------------------------------------------------------
	int DrawLayer2D::getZOrder() const
	{
		return this->mImpl->zOrder;
	}
	//---------------------------------------------------------
	void DrawLayer2D::clear()
	{
		for(Impl::Batch & batch : this->mImpl->batches)
		{
			this->mImpl->destroyBatchObject(batch);
		}
		this->mImpl->batches.clear();
		gOrderDirty = true;
	}
	//---------------------------------------------------------
	void DrawLayer2D::addTriangles(String const & textureName,
		Vertex2D const * vertices, size_t vertexCount,
		unsigned short const * indices, size_t indexCount,
		ScissorRect const * scissor)
	{
		this->mImpl->batches.emplace_back();
		Impl::Batch & batch = this->mImpl->batches.back();
		batch.textureName = textureName;
		DrawLayer2DDetail::appendTriangles(batch.triangles, vertices,
			vertexCount, indices, indexCount, scissor);
		this->mImpl->buildBatchObject(batch);
	}
	//---------------------------------------------------------
	void DrawLayer2D::addTriangles(optr<RenderTexture> const & texture,
		Vertex2D const * vertices, size_t vertexCount,
		unsigned short const * indices, size_t indexCount,
		ScissorRect const * scissor)
	{
		this->mImpl->batches.emplace_back();
		Impl::Batch & batch = this->mImpl->batches.back();
		batch.renderTexture = texture;	// kept alive until clear()
		DrawLayer2DDetail::appendTriangles(batch.triangles, vertices,
			vertexCount, indices, indexCount, scissor);
		this->mImpl->buildBatchObject(batch);
	}
}
