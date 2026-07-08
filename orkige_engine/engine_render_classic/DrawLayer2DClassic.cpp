/********************************************************************
	created:	Wednesday 2026/07/08 at 22:00
	filename: 	DrawLayer2DClassic.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file DrawLayer2DClassic.cpp
//! @brief classic-OGRE implementation of the DrawLayer2D facade
//! @remarks Gorilla's proven manual-render machinery, generalized: one
//! RenderQueueListener composites all layers after RENDER_QUEUE_OVERLAY,
//! but ONLY into the main window's viewport (Gorilla needed a
//! RenderTargetListener dance for that; checking the scene manager's
//! current viewport is the direct equivalent - RTT and editor side
//! viewports never see 2D layers). Per layer one dynamic vertex buffer
//! (pixel -> NDC on fill, exactly Gorilla's Screen::_transform), per
//! batch one generated unlit material "DrawLayer2D/<texture>" (cull
//! none, depth off, alpha blend, clamped point sampling - Gorilla's 2D
//! atlas recipe; RTSS generates the shaders) drawn via
//! SceneManager::manualRender with identity matrices. Scissors and
//! indices never reach this backend: DrawLayer2DClip.h resolved both
//! into flat triangle lists at submission time.

#include "engine_render_classic/ClassicBackend.h"
#include "engine_render/DrawLayer2DClip.h"
#include "engine_graphic/Engine.h"
#include <core_debug/DebugMacros.h>

#include <algorithm>
#include <set>

namespace Orkige
{
	namespace
	{
		//--- the render hook ------------------------------------------
		//! all live layers in creation order (creation order breaks
		//! zOrder ties, per the facade contract)
		std::vector<DrawLayer2D*> gDrawLayers;

		Ogre::MaterialPtr getOrCreateLayerMaterial(String const & textureName);

		//! the one RenderQueueListener compositing every layer (the
		//! worker lives on RenderBackend for facade-Impl access)
		class DrawLayer2DRenderer : public Ogre::RenderQueueListener
		{
		public:
			Ogre::SceneManager* mSceneManager = NULL;

			void renderQueueStarted(Ogre::uint8, const Ogre::String&,
				bool&) override {}

			void renderQueueEnded(Ogre::uint8 queueGroupId,
				const Ogre::String&, bool&) override
			{
				if(queueGroupId == Ogre::RENDER_QUEUE_OVERLAY)
				{
					RenderBackend::renderDrawLayers2D(this->mSceneManager);
				}
			}
		};

		DrawLayer2DRenderer* gRenderer = NULL;

		//! textures that already logged their load failure (log once)
		std::set<String> gFailedTextures;

		//--- generated materials --------------------------------------
		//! the shared master: unlit, alpha-blended, depth-ignored,
		//! two-sided - Gorilla's 2D master material, texture-less (the
		//! untextured/vertex-colour batches draw with it directly)
		Ogre::MaterialPtr getOrCreateMasterMaterial()
		{
			Ogre::MaterialPtr master = Ogre::MaterialManager::getSingleton()
				.getByName("DrawLayer2D/Master");
			if(master)
			{
				return master;
			}
			master = Ogre::MaterialManager::getSingleton().create(
				"DrawLayer2D/Master",
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
			Ogre::Pass* pass = master->getTechnique(0)->getPass(0);
			pass->setCullingMode(Ogre::CULL_NONE);
			pass->setDepthCheckEnabled(false);
			pass->setDepthWriteEnabled(false);
			pass->setLightingEnabled(false);
			// REQUIRED for the RTSS to route vertex colours into the
			// generated shader - Gorilla's original 2D material missed it
			// (a latent bug: harmless in the FFP era, but on the shader-only
			// render systems its vertex colours silently went white)
			pass->setVertexColourTracking(Ogre::TVC_DIFFUSE);
			pass->setSceneBlending(Ogre::SBT_TRANSPARENT_ALPHA);
			master->load();
			return master;
		}

		//! per-texture clone "DrawLayer2D/<texture>" with clamped point
		//! sampling (crisp pixel UI - the Gorilla atlas rule); NULL when
		//! the texture cannot be loaded (logged once per name)
		Ogre::MaterialPtr getOrCreateLayerMaterial(String const & textureName)
		{
			if(textureName.empty())
			{
				return getOrCreateMasterMaterial();
			}
			const String materialName = "DrawLayer2D/" + textureName;
			Ogre::MaterialPtr material = Ogre::MaterialManager::getSingleton()
				.getByName(materialName);
			if(material)
			{
				return material;
			}
			Ogre::TexturePtr texture;
			try
			{
				texture = Ogre::TextureManager::getSingleton().load(
					textureName,
					Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
			}
			catch(Ogre::Exception const &)
			{
			}
			if(!texture)
			{
				if(gFailedTextures.insert(textureName).second)
				{
					oDebugError("engine", 0, "DrawLayer2D: texture '"
						<< textureName
						<< "' not found - batch draws untextured");
				}
				return getOrCreateMasterMaterial();
			}
			material = getOrCreateMasterMaterial()->clone(materialName);
			Ogre::Pass* pass = material->getTechnique(0)->getPass(0);
			Ogre::TextureUnitState* unit = pass->createTextureUnitState();
			unit->setTextureName(texture->getName());
			unit->setTextureAddressingMode(Ogre::TextureUnitState::TAM_CLAMP);
			unit->setTextureFiltering(Ogre::FO_NONE, Ogre::FO_NONE,
				Ogre::FO_NONE);
			// load up front so the RTSS can generate the shader technique
			material->load();
			return material;
		}
	}

	//---------------------------------------------------------
	//--- RenderBackend factory/registry/render hook -----------
	//---------------------------------------------------------
	void RenderBackend::renderDrawLayers2D(Ogre::SceneManager* sceneManager)
	{
		RenderSystem* system = RenderBackend::system();
		if(!system || !sceneManager || gDrawLayers.empty())
		{
			return;
		}
		// 2D layers composite over the MAIN WINDOW only: the facade
		// contract keeps them out of RTTs and any other viewport
		// rendering through this scene manager
		Ogre::Viewport* current = sceneManager->getCurrentViewport();
		if(!current)
		{
			return;
		}
		Ogre::RenderWindow* window = system->mImpl->engine->getRenderWindow(0);
		if(!window || window->getNumViewports() == 0 ||
			current != window->getViewport(0))
		{
			return;
		}
		const float windowWidth =
			static_cast<float>(current->getActualWidth());
		const float windowHeight =
			static_cast<float>(current->getActualHeight());
		if(windowWidth <= 0.0f || windowHeight <= 0.0f)
		{
			return;
		}
		// ascending zOrder, creation order on ties
		std::vector<DrawLayer2D*> ordered = gDrawLayers;
		std::stable_sort(ordered.begin(), ordered.end(),
			[](DrawLayer2D const * a, DrawLayer2D const * b)
			{ return a->getZOrder() < b->getZOrder(); });
		for(DrawLayer2D* layer : ordered)
		{
			DrawLayer2D::Impl* impl = layer->mImpl;
			if(!impl->visible || impl->batches.empty())
			{
				continue;
			}
			if(impl->dirty || impl->filledWidth != windowWidth ||
				impl->filledHeight != windowHeight)
			{
				impl->fillVertexBuffer(windowWidth, windowHeight);
			}
			for(size_t each = 0; each < impl->batches.size(); ++each)
			{
				const std::pair<size_t, size_t> range =
					impl->batchRanges[each];
				if(range.second == 0)
				{
					continue;
				}
				Ogre::MaterialPtr material = getOrCreateLayerMaterial(
					impl->batches[each].textureName);
				if(!material)
				{
					continue;	// texture missing - logged once
				}
				// getBestTechnique resolves the RTSS-generated technique;
				// manualRender binds pass state + GPU auto params before
				// the draw (Gorilla's OGRE-14 recipe)
				Ogre::Pass* pass = material->getBestTechnique()->getPass(0);
				impl->renderOp.vertexData->vertexStart = range.first;
				impl->renderOp.vertexData->vertexCount = range.second;
				sceneManager->manualRender(&impl->renderOp,
					pass, current, Ogre::Affine3::IDENTITY,
					Ogre::Affine3::IDENTITY, Ogre::Matrix4::IDENTITY);
			}
		}
	}
	//---------------------------------------------------------
	optr<DrawLayer2D> RenderBackend::createDrawLayer2D(int zOrder)
	{
		optr<DrawLayer2D> handle(new DrawLayer2D());
		handle->mImpl->zOrder = zOrder;
		gDrawLayers.push_back(handle.get());
		// first layer wires the render hook into the world's scene manager
		if(!gRenderer)
		{
			gRenderer = new DrawLayer2DRenderer();
			gRenderer->mSceneManager =
				RenderBackend::system()->getWorld()->mImpl->sceneManager;
			gRenderer->mSceneManager->addRenderQueueListener(gRenderer);
		}
		return handle;
	}
	//---------------------------------------------------------
	void RenderBackend::unregisterDrawLayer2D(DrawLayer2D* layer)
	{
		gDrawLayers.erase(std::remove(gDrawLayers.begin(),
			gDrawLayers.end(), layer), gDrawLayers.end());
		if(gDrawLayers.empty() && gRenderer)
		{
			// detach the hook again (the backend may already be gone when
			// a script-held owner dies after ~Engine - same late-destroy
			// rule as the other facade handles)
			if(RenderBackend::system() && gRenderer->mSceneManager)
			{
				gRenderer->mSceneManager->removeRenderQueueListener(gRenderer);
			}
			delete gRenderer;
			gRenderer = NULL;
		}
	}

	//---------------------------------------------------------
	//--- Impl buffer plumbing ---------------------------------
	//---------------------------------------------------------
	void DrawLayer2D::Impl::fillVertexBuffer(float windowWidth,
		float windowHeight)
	{
		// lazy vertex data + declaration (Gorilla's layout: float3
		// position, float4 diffuse, float2 uv)
		if(!this->renderOp.vertexData)
		{
			this->renderOp.vertexData = OGRE_NEW Ogre::VertexData();
			this->renderOp.vertexData->vertexStart = 0;
			Ogre::VertexDeclaration* declaration =
				this->renderOp.vertexData->vertexDeclaration;
			size_t offset = 0;
			declaration->addElement(0, offset, Ogre::VET_FLOAT3,
				Ogre::VES_POSITION);
			offset += Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT3);
			declaration->addElement(0, offset, Ogre::VET_FLOAT4,
				Ogre::VES_DIFFUSE);
			offset += Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT4);
			declaration->addElement(0, offset, Ogre::VET_FLOAT2,
				Ogre::VES_TEXTURE_COORDINATES);
			this->renderOp.operationType =
				Ogre::RenderOperation::OT_TRIANGLE_LIST;
			this->renderOp.useIndexes = false;
		}
		size_t totalVertexCount = 0;
		for(Batch const & batch : this->batches)
		{
			totalVertexCount += batch.triangles.size();
		}
		// grow-only power-of-two capacity, discardable dynamic buffer
		if(totalVertexCount > this->vertexBufferCapacity ||
			!this->vertexBuffer)
		{
			size_t newCapacity = 64;
			while(newCapacity < totalVertexCount)
			{
				newCapacity <<= 1;
			}
			this->vertexBuffer = Ogre::HardwareBufferManager::getSingleton()
				.createVertexBuffer(
					this->renderOp.vertexData->vertexDeclaration
						->getVertexSize(0),
					newCapacity,
					Ogre::HardwareBuffer::HBU_DYNAMIC_WRITE_ONLY_DISCARDABLE,
					false);
			this->vertexBufferCapacity = newCapacity;
			this->renderOp.vertexData->vertexBufferBinding->setBinding(0,
				this->vertexBuffer);
		}
		// pixel -> NDC (Gorilla's Screen::_transform) + batch ranges
		this->batchRanges.clear();
		const float invWidth = 1.0f / windowWidth;
		const float invHeight = 1.0f / windowHeight;
		float* write = static_cast<float*>(
			this->vertexBuffer->lock(Ogre::HardwareBuffer::HBL_DISCARD));
		size_t vertexCursor = 0;
		for(Batch const & batch : this->batches)
		{
			this->batchRanges.push_back(
				std::make_pair(vertexCursor, batch.triangles.size()));
			for(DrawLayer2D::Vertex2D const & vertex : batch.triangles)
			{
				*write++ = vertex.x * invWidth * 2.0f - 1.0f;
				*write++ = vertex.y * invHeight * -2.0f + 1.0f;
				*write++ = 0.0f;
				*write++ = vertex.colour.r;
				*write++ = vertex.colour.g;
				*write++ = vertex.colour.b;
				*write++ = vertex.colour.a;
				*write++ = vertex.u;
				*write++ = vertex.v;
			}
			vertexCursor += batch.triangles.size();
		}
		this->vertexBuffer->unlock();
		this->filledWidth = windowWidth;
		this->filledHeight = windowHeight;
		this->dirty = false;
	}
	//---------------------------------------------------------
	void DrawLayer2D::Impl::destroyVertexBuffer()
	{
		OGRE_DELETE this->renderOp.vertexData;
		this->renderOp.vertexData = NULL;
		this->vertexBuffer.reset();
		this->vertexBufferCapacity = 0;
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
		if(RenderBackend::system())
		{
			this->mImpl->destroyVertexBuffer();
		}
		delete this->mImpl;
	}
	//---------------------------------------------------------
	void DrawLayer2D::setVisible(bool visible)
	{
		this->mImpl->visible = visible;
	}
	//---------------------------------------------------------
	bool DrawLayer2D::isVisible() const
	{
		return this->mImpl->visible;
	}
	//---------------------------------------------------------
	void DrawLayer2D::setZOrder(int zOrder)
	{
		this->mImpl->zOrder = zOrder;
	}
	//---------------------------------------------------------
	int DrawLayer2D::getZOrder() const
	{
		return this->mImpl->zOrder;
	}
	//---------------------------------------------------------
	void DrawLayer2D::clear()
	{
		this->mImpl->batches.clear();
		this->mImpl->dirty = true;
	}
	//---------------------------------------------------------
	void DrawLayer2D::addTriangles(String const & textureName,
		Vertex2D const * vertices, size_t vertexCount,
		unsigned short const * indices, size_t indexCount,
		ScissorRect const * scissor)
	{
		Impl::Batch batch;
		batch.textureName = textureName;
		DrawLayer2DDetail::appendTriangles(batch.triangles, vertices,
			vertexCount, indices, indexCount, scissor);
		this->mImpl->batches.push_back(std::move(batch));
		this->mImpl->dirty = true;
	}
}
