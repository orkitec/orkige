/**************************************************************
	created:	2026/07/25 at 16:00
	filename: 	WorldTextComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/WorldTextComponent.h"
#include <core_script/ScriptRuntime.h>	// OSCRIPT_HANDLE: ScriptComponentAccess registry
#include "engine_gocomponent/TransformComponent.h"
#include "engine_gocomponent/ComponentPropertyReflect.h"	// Color pack/unpack for OPROPERTY
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include "engine_render/RenderCamera.h"	// view matrix -> camera billboard axes
#include "engine_render/RenderNode.h"
#include "engine_gui/FontAtlas.h"
#include "engine_gui/UiAtlas.h"
#include <core_game/GameObject.h>
#include <core_game/SceneSerializer.h>
#include <core_debug/DebugMacros.h>
#include <core_debug/MemoryManager.h>

namespace Orkige
{
	namespace
	{
		//! the engine-default world-text font declaration (Nunito, baked once
		//! into the shared page). It resolves through the engine font resource
		//! directory the player/editor register (media/fonts).
		const char * const WORLD_TEXT_OGUI = "world_text.ogui";

		//! @brief the ONE shared world-text font page, baked lazily from the
		//! engine-default TTF via engine_gui/FontAtlas (the SAME baker the gui
		//! uses) and reused by every WorldTextComponent - NOT a second bake per
		//! component. Tied to the live render system: a re-boot (a test
		//! recreating the engine) rebuilds rather than binding a destroyed
		//! texture. Returns NULL headless / when the font resource is absent.
		//! @remarks The FontAtlas dtor is teardown-safe (it only frees the GPU
		//! texture when a render system is still up), so the function-local
		//! static releasing at process exit is safe.
		FontAtlas * sharedWorldTextAtlas()
		{
			static optr<FontAtlas> atlas;
			static RenderSystem * builtFor = NULL;
			RenderSystem * renderSystem = RenderSystem::get();
			if(renderSystem == NULL)
			{
				return NULL;
			}
			if(atlas && builtFor != renderSystem)
			{
				atlas.reset();		// stale page from a previous engine boot
			}
			if(!atlas)
			{
				if(!FontAtlas::oguiDeclaresRuntimeContent(WORLD_TEXT_OGUI))
				{
					return NULL;	// the engine font dir is not registered
				}
				atlas = onew(new FontAtlas(WORLD_TEXT_OGUI));
				builtFor = renderSystem;
				if(!atlas->isValid())
				{
					atlas.reset();
					return NULL;
				}
			}
			return atlas.get();
		}
	}

	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	WorldTextComponent::WorldTextComponent()
		: mSize(1.0f), mColour(1.0f, 1.0f, 1.0f, 1.0f), mBillboard(true),
		mVisible(true), mLayoutDirty(true), mTicked(false), mFreshBuild(false)
	{
		this->addDependency<TransformComponent>();
		// receive per-frame ticks under a runtime so billboard text re-faces the
		// moving camera/object (cheap no-op for non-billboard idle text). The
		// editor never ticks GameObjects - it re-faces through faceCamera and
		// builds synchronously on a property change instead.
		this->setWantsUpdates(true);
	}
	//---------------------------------------------------------
	WorldTextComponent::~WorldTextComponent()
	{
	}
	//---------------------------------------------------------
	void WorldTextComponent::setText(String const & text)
	{
		if(this->mText == text)
		{
			return;
		}
		this->mText = text;
		this->mLayoutDirty = true;
		// editor / pre-first-tick: build + submit synchronously so the change
		// shows now. Under a ticking runtime, onUpdateComponent is the single
		// coalesced upload site (the next backend forbids two maps per frame).
		if(!this->mTicked)
		{
			this->refreshQuads(true);
		}
	}
	//---------------------------------------------------------
	void WorldTextComponent::setSize(float size)
	{
		if(this->mSize == size)
		{
			return;
		}
		this->mSize = size;
		this->mLayoutDirty = true;
		if(!this->mTicked)
		{
			this->refreshQuads(true);
		}
	}
	//---------------------------------------------------------
	void WorldTextComponent::setColour(float red, float green, float blue,
		float alpha)
	{
		this->mColour = Color(red, green, blue, alpha);
		// colour rides the vertices - no layout rebuild, just a resubmit
		if(!this->mTicked)
		{
			this->refreshQuads(true);
		}
	}
	//---------------------------------------------------------
	void WorldTextComponent::setBillboard(bool billboard)
	{
		if(this->mBillboard == billboard)
		{
			return;
		}
		this->mBillboard = billboard;
		// the placement node differs by mode (root for billboard, the transform
		// node otherwise), so re-attach then resubmit with the matching basis
		this->attachBatch();
		if(!this->mTicked)
		{
			this->refreshQuads(true);
		}
	}
	//---------------------------------------------------------
	void WorldTextComponent::setTextVisible(bool visible)
	{
		this->mVisible = visible;
		this->applyVisibility();
	}
	//---------------------------------------------------------
	std::size_t WorldTextComponent::getSubmittedQuadCount() const
	{
		return this->mBatch ? this->mBatch->getQuadCount() : 0;
	}
	//---------------------------------------------------------
	void WorldTextComponent::faceCamera(Vec3 const & cameraRight,
		Vec3 const & cameraUp)
	{
		if(!this->mBillboard)
		{
			return;		// the transform already orients non-billboard text
		}
		this->ensureBatch();
		if(!this->mBatch)
		{
			return;
		}
		if(this->mLayoutDirty)
		{
			this->rebuildLayout();
		}
		this->submitQuads(cameraRight, cameraUp, true);
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void WorldTextComponent::onAdd()
	{
		// build the batch + geometry now so the text shows in the editor's edit
		// mode (which never ticks GameObjects); the player re-faces each frame.
		// A headless host / a text-less component simply defers until a font and
		// a text exist.
		this->refreshQuads(true);
	}
	//---------------------------------------------------------
	void WorldTextComponent::onRemove()
	{
		// RAII: dropping the handle detaches and destroys the batch geometry
		this->mBatch.reset();
	}
	//---------------------------------------------------------
	void WorldTextComponent::onSetActive(bool activeInHierarchy)
	{
		this->applyVisibility();
	}
	//---------------------------------------------------------
	void WorldTextComponent::onUpdateComponent(float deltaTime)
	{
		// a ticking runtime reaches here (the editor never does): from now on
		// uploads are coalesced to THIS single per-frame site
		this->mTicked = true;
		// one-tick defer after a synchronous submit: it already mapped the batch
		// buffer this frame (a setText/load before the first render), so skip
		// THIS tick's dynamic upload - the next backend forbids mapping the same
		// buffer twice per frame (the VectorAnimation deferral discipline)
		if(this->mFreshBuild)
		{
			this->mFreshBuild = false;
			return;
		}
		// billboard text re-faces the (possibly moving) camera and follows the
		// object each frame; non-billboard text only needs a resubmit when its
		// layout changed (the transform node carries its motion for free)
		if(this->mBillboard || this->mLayoutDirty)
		{
			this->refreshQuads(false);
		}
	}
	//---------------------------------------------------------
	void WorldTextComponent::ensureBatch()
	{
		if(this->mBatch)
		{
			return;
		}
		if(this->mText.empty())
		{
			return;		// nothing to show yet
		}
		UiFont const * font = WorldTextComponent::sharedFont();
		if(font == NULL)
		{
			return;		// no render system / no font resource (headless)
		}
		RenderSystem * renderSystem = RenderSystem::get();
		if(!renderSystem || !renderSystem->getWorld())
		{
			return;
		}
		// share the gui's baked font page: the batch binds it by name (the page
		// was uploaded by sharedFont()). Alpha-blended: the glyph coverage is the
		// texture's alpha, the vertex colour tints it.
		optr<SpriteBatch> batch = renderSystem->getWorld()->createSpriteBatch(
			WorldTextComponent::sharedFontTextureName(), SpriteBatch::BLEND_ALPHA);
		if(!batch)
		{
			return;		// load failure already logged
		}
		this->mBatch = batch;
		this->attachBatch();
		this->applyVisibility();
	}
	//---------------------------------------------------------
	void WorldTextComponent::attachBatch()
	{
		if(!this->mBatch)
		{
			return;
		}
		RenderSystem * renderSystem = RenderSystem::get();
		if(!renderSystem || !renderSystem->getWorld())
		{
			return;
		}
		if(this->mBillboard)
		{
			// world-space quads hang off the root, so the text does NOT inherit
			// the object's rotation/scale - only its world position (applied per
			// vertex). This is the 3D-particle attachment.
			this->mBatch->attachTo(renderSystem->getWorld()->getRootNode());
			return;
		}
		// local-plane quads ride the transform node, so the object orients them
		GameObject * owner = this->getComponentOwner();
		optr<TransformComponent> transform = owner
			? owner->getComponent<TransformComponent>().lock()
			: optr<TransformComponent>();
		if(transform && transform->getNode())
		{
			this->mBatch->attachTo(transform->getNode());
		}
		else
		{
			this->mBatch->attachTo(renderSystem->getWorld()->getRootNode());
		}
	}
	//---------------------------------------------------------
	void WorldTextComponent::rebuildLayout()
	{
		this->mLayoutDirty = false;
		UiFont const * font = WorldTextComponent::sharedFont();
		if(font == NULL)
		{
			this->mLayout = WorldTextLayout::Result();
			return;
		}
		this->mLayout = WorldTextLayout::build(*font, this->mText, this->mSize);
		// a codepoint beyond the eager range baked into the page during layout
		// (CJK/Cyrillic) - push it to the GPU so the batch samples it
		WorldTextComponent::flushSharedFont();
	}
	//---------------------------------------------------------
	void WorldTextComponent::refreshQuads(bool fresh)
	{
		this->ensureBatch();
		if(!this->mBatch)
		{
			return;
		}
		if(this->mLayoutDirty)
		{
			this->rebuildLayout();
		}
		Vec3 cameraRight(1.0f, 0.0f, 0.0f);
		Vec3 cameraUp(0.0f, 1.0f, 0.0f);
		if(this->mBillboard)
		{
			this->windowCameraAxes(cameraRight, cameraUp);
		}
		this->submitQuads(cameraRight, cameraUp, fresh);
	}
	//---------------------------------------------------------
	void WorldTextComponent::windowCameraAxes(Vec3 & outRight, Vec3 & outUp) const
	{
		// the CPU billboard axes: the window camera's world-space right/up read
		// from its view matrix (its first two rows ARE those axes in world
		// space) - the exact recipe ParticleComponent::writeQuads3D uses
		outRight = Vec3(1.0f, 0.0f, 0.0f);
		outUp = Vec3(0.0f, 1.0f, 0.0f);
		RenderSystem * renderSystem = RenderSystem::get();
		if(!renderSystem)
		{
			return;
		}
		optr<RenderCamera> camera = renderSystem->getWindowCamera();
		if(!camera)
		{
			return;
		}
		const Mat4 view = camera->getViewMatrix();
		outRight = Vec3(view[0][0], view[0][1], view[0][2]);
		outUp = Vec3(view[1][0], view[1][1], view[1][2]);
	}
	//---------------------------------------------------------
	void WorldTextComponent::submitQuads(Vec3 const & cameraRight,
		Vec3 const & cameraUp, bool fresh)
	{
		if(!this->mBatch)
		{
			return;
		}
		const std::size_t quadCount = this->mLayout.quads.size();
		this->mVertexScratch.clear();
		if(quadCount == 0)
		{
			this->mBatch->setQuads(NULL, 0);
			return;
		}
		const std::size_t capacityBefore = this->mVertexScratch.capacity();
		this->mVertexScratch.reserve(quadCount * 4);
		MemoryManager::countGrowth(MemoryManager::TAG_GUI,
			capacityBefore, this->mVertexScratch.capacity());

		// billboard: text-local (x,y) map onto the camera basis at the object's
		// world position. non-billboard: the (x,y) are the node-local quad
		// corners (z=0) and the transform node orients them.
		Vec3 origin(0.0f, 0.0f, 0.0f);
		if(this->mBillboard)
		{
			GameObject * owner = this->getComponentOwner();
			optr<TransformComponent> transform = owner
				? owner->getComponent<TransformComponent>().lock()
				: optr<TransformComponent>();
			if(transform)
			{
				origin = transform->getWorldPosition();
			}
		}
		for(std::size_t index = 0; index < quadCount; ++index)
		{
			WorldTextLayout::GlyphQuad const & quad = this->mLayout.quads[index];
			for(int corner = 0; corner < 4; ++corner)
			{
				Vec2 const & local = quad.corners[corner];
				SpriteBatch::Vertex vertex;
				if(this->mBillboard)
				{
					vertex.position = origin
						+ cameraRight * local.x + cameraUp * local.y;
				}
				else
				{
					vertex.position = Vec3(local.x, local.y, 0.0f);
				}
				vertex.uv = quad.uv[corner];
				vertex.colour = this->mColour;
				this->mVertexScratch.push_back(vertex);
			}
		}
		if(fresh)
		{
			// force a full-rebuild upload (a NEW batch buffer): clearing to zero
			// first drops the live buffer so the following upload reallocates
			// instead of re-mapping it. Safe to repeat between renders (the
			// synchronous editor/pre-tick path) - no beginUpdate on a buffer
			// already mapped this frame. Arms the one-tick defer for the next
			// runtime tick.
			this->mBatch->setQuads(NULL, 0);
			this->mBatch->setQuads(this->mVertexScratch.data(), quadCount);
			this->mFreshBuild = true;
		}
		else
		{
			// dynamic in-place upload (once per frame under a runtime): rides the
			// batch's beginUpdate fast path when the quad count is unchanged
			this->mBatch->setQuads(this->mVertexScratch.data(), quadCount);
		}
	}
	//---------------------------------------------------------
	void WorldTextComponent::applyVisibility()
	{
		if(!this->mBatch)
		{
			return;
		}
		GameObject * owner = this->getComponentOwner();
		const bool ownerActive = !owner || owner->isActiveInHierarchy();
		this->mBatch->setVisible(this->mVisible && ownerActive);
	}
	//---------------------------------------------------------
	void WorldTextComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		// every field is reflected - the schema serializes text/size/colour/
		// billboard/visible with no positional readers (the v7 reflection path)
		SceneSerializer::saveComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	void WorldTextComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		SceneSerializer::loadComponentProperties(ar, *this);
		this->mLayoutDirty = true;
		// the batch (re)builds on the first refresh (needs a render system)
	}
	//---------------------------------------------------------
	//--- private (shared font page) --------------------------
	//---------------------------------------------------------
	UiFont const * WorldTextComponent::sharedFont()
	{
		FontAtlas * atlas = sharedWorldTextAtlas();
		// [Font.0] is the world-text face declared in world_text.ogui
		return atlas ? atlas->atlas()->getFont(0) : NULL;
	}
	//---------------------------------------------------------
	String WorldTextComponent::sharedFontTextureName()
	{
		// FontAtlas derives the page's GPU texture name from the ogui file name
		// (the batch binds by that name)
		FontAtlas * atlas = sharedWorldTextAtlas();
		return atlas ? atlas->getTextureName() : String();
	}
	//---------------------------------------------------------
	void WorldTextComponent::flushSharedFont()
	{
		// push glyphs baked-on-demand during layout (CJK/Cyrillic paging) to the
		// GPU so the batch samples them
		FontAtlas * atlas = sharedWorldTextAtlas();
		if(atlas)
		{
			atlas->flush();
		}
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(WorldTextComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(setText)
		OFUNCCR(getText)
		OFUNC(setSize)
		OFUNC(getSize)
		OFUNC(setColour)
		OFUNC(setBillboard)
		OFUNC(getBillboard)
		OFUNC(setTextVisible)
		OFUNC(isTextVisible)
		// the reflected look: the ONE property registry feeds the inspector,
		// scene serialization, Lua self.<name>, the debug protocol and MCP
		OPROPERTY("text", Orkige::PropertyKind::String, getText, setText, Orkige::PROP_NONE)
		OPROPERTY("size", Orkige::PropertyKind::Float, getSize, setSize, Orkige::PROP_NONE)
		OPROPERTY("colour", Orkige::PropertyKind::Color, getColour, setColourValue, Orkige::PROP_NONE)
		OPROPERTY("billboard", Orkige::PropertyKind::Bool, getBillboard, setBillboard, Orkige::PROP_NONE)
		OPROPERTY("visible", Orkige::PropertyKind::Bool, isTextVisible, setTextVisible, Orkige::PROP_NONE)

		// self.worldtext / world.getWorldText(id) / getComponent("worldtext")
		// hand Lua a WEAK handle: locks per call, raises an honest error naming
		// the owner once gone. @see TransformComponent.
		OWEAKHANDLE_BEGIN(Orkige::WorldTextComponent, "WorldTextComponentHandle", "component handle", "component")
			OWEAKHANDLE_BASEMETHOD(setText)
			OWEAKHANDLE_BASEMETHOD(getText)
			OWEAKHANDLE_BASEMETHOD(setSize)
			OWEAKHANDLE_BASEMETHOD(getSize)
			OWEAKHANDLE_BASEMETHOD(setColour)
			OWEAKHANDLE_BASEMETHOD(setBillboard)
			OWEAKHANDLE_BASEMETHOD(getBillboard)
			OWEAKHANDLE_BASEMETHOD(setTextVisible)
			OWEAKHANDLE_BASEMETHOD(isTextVisible)
		OWEAKHANDLE_END
		OSCRIPT_HANDLE("worldtext", true, "getWorldText")
	OOBJECT_END
}
