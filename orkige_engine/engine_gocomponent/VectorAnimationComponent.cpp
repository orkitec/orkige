/**************************************************************
	created:	2026/07/12 at 15:00
	filename: 	VectorAnimationComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/VectorAnimationComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include "engine_gocomponent/ComponentPropertyReflect.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include "engine_render/RenderNode.h"
#include "core_util/VectorAnimAsset.h"
#include <core_game/GameObject.h>
#include <core_game/SceneSerializer.h>
#include <core_project/AssetDatabase.h>
#include <core_script/ScriptEventBus.h>
#include <core_script/ScriptEventPayload.h>
#include <core_debug/DebugMacros.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(VectorAnimationComponent, VectorAnimationEndedEvent);

	// the clamp range mirrors the facade quad's window (shared 2D painter's
	// window: sprites, shapes and animation rigs sort against each other by the
	// same rule)
	const int VectorAnimationComponent::ZORDER_MIN = -40;
	const int VectorAnimationComponent::ZORDER_MAX = 40;
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	VectorAnimationComponent::VectorAnimationComponent()
	{
		this->mAnimName = "";
		this->mAnimAssetId = "";
		this->mClip = "";
		this->mSpeed = 1.0f;
		this->mPlaying = true;		// a placed rig plays on Play (dormant in the editor)
		this->mTransitionTime = 0.0f;
		this->mTint = Color::White;
		this->mScale = 1.0f;
		this->mEdgeSoftness = 0.0f;	// auto: derived from the pose bounds
		this->mZOrder = 0;
		this->mVisible = true;
		this->mFreshBuild = false;
		this->mNeedsUpload = false;
		this->mWasAtEnd = false;
		this->mEventData = onew(new StringUtil::StringObject(StringUtil::BLANK));
		this->addDependency<TransformComponent>();
	}
	//---------------------------------------------------------
	VectorAnimationComponent::~VectorAnimationComponent()
	{
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::loadAnimation(String const & animName)
	{
		oAssert(!animName.empty());
		oAssert(this->getComponentOwner());
		oAssert(this->mNode);

		// callers may pass this->mAnimName itself and removeAnimation() clears
		// that member - copy before anything can mutate what the parameter
		// aliases (the ModelComponent aliasing lesson)
		const String fileName = animName;

		if(this->mMesh)
		{
			this->removeAnimation();
		}

		// the `.oanim` is a small text resource read through the facade so this
		// stays backend-neutral (no renderer/Ogre here); AUTODETECT resolves
		// engine media and project assets alike
		String text;
		if(!RenderSystem::get()->readResourceText(fileName, text))
		{
			oDebugError("engine", 0, "VectorAnimationComponent: animation '"
				<< fileName << "' not found");
			return;
		}
		VectorAnimAsset::Document document;
		if(!VectorAnimAsset::parse(text, document))
		{
			oDebugError("engine", 0, "VectorAnimationComponent: animation '"
				<< fileName << "' is malformed");
			return;
		}
		if(!this->mEval.build(document))
		{
			oDebugError("engine", 0, "VectorAnimationComponent: animation '"
				<< fileName << "' is not a valid rig");
			return;
		}

		this->mMesh = RenderSystem::get()->getWorld()->createVectorMesh();
		if(!this->mMesh)
		{
			this->mEval = VectorAnimEval();
			return;
		}
		this->mAnimName = fileName;
		this->mAnimAssetId = AssetDatabase::referenceIdForValue(
			fileName, "", AssetDatabase::REF_FILE_NAME);
		// select the reflected/default clip, build the first pose and push the
		// mesh topology; a runtime tick then advances and refills it
		this->applyInitialClip();
		this->mMesh->attachTo(this->getNode());
		this->applyVisibility();
		// only a rig-carrying component ticks (like ScriptComponent, only a
		// GameObject-ticking runtime reaches onUpdateComponent)
		this->setWantsUpdates(true);
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::removeAnimation()
	{
		// RAII: dropping the handle detaches and destroys the mesh geometry
		this->mMesh.reset();
		this->mAnimName = "";
		this->mAnimAssetId = "";
		this->mEval = VectorAnimEval();
		this->mBuilt.clear();
		this->mScratchRegions.clear();
		this->mVertices.clear();
		this->mFreshBuild = false;
		this->mNeedsUpload = false;
		this->mWasAtEnd = false;
		this->setWantsUpdates(false);
	}
	//---------------------------------------------------------
	std::size_t VectorAnimationComponent::getTriangleCount() const
	{
		return this->mBuilt.triangleCount();
	}
	//---------------------------------------------------------
	std::size_t VectorAnimationComponent::getVertexCount() const
	{
		return this->mVertices.size();
	}
	//---------------------------------------------------------
	float VectorAnimationComponent::getPoseSignature() const
	{
		float signature = 0.0f;
		for(VectorMesh::Vertex const & vertex : this->mVertices)
		{
			signature += std::fabs(vertex.position.x) +
				std::fabs(vertex.position.y);
		}
		return signature;
	}
	//---------------------------------------------------------
	bool VectorAnimationComponent::play(String const & clip)
	{
		if(!this->mEval.isBuilt())
		{
			return false;
		}
		if(clip.empty())
		{
			// resume the current clip (no name = just un-stop)
			this->mPlaying = true;
			this->mWasAtEnd = false;
			return true;
		}
		const int index = this->clipIndex(clip);
		if(index < 0)
		{
			return false;
		}
		// only mutate the evaluator here; the single per-frame upload site
		// (onUpdateComponent) pushes the new pose, so a script that plays a clip
		// never maps the dynamic buffer a second time this frame
		this->mEval.setClip(index);
		this->mPlaying = true;
		this->mWasAtEnd = false;
		this->mNeedsUpload = true;
		return true;
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::stop()
	{
		this->mPlaying = false;
	}
	//---------------------------------------------------------
	bool VectorAnimationComponent::setClip(String const & clip,
		float transitionSeconds)
	{
		if(!this->mEval.isBuilt())
		{
			return false;
		}
		const int index = this->clipIndex(clip);
		if(index < 0)
		{
			return false;
		}
		// a negative time means "use the default transition"; <= 0 is a hard cut
		const float seconds = (transitionSeconds < 0.0f)
			? this->mTransitionTime : transitionSeconds;
		if(seconds > 0.0f)
		{
			this->mEval.crossFadeTo(index, seconds);
		}
		else
		{
			this->mEval.setClip(index);
		}
		this->mWasAtEnd = false;
		this->mNeedsUpload = true;	// onUpdateComponent does the one upload
		return true;
	}
	//---------------------------------------------------------
	bool VectorAnimationComponent::crossFade(String const & clip, float seconds)
	{
		return this->setClip(clip, seconds < 0.0f ? 0.0f : seconds);
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::scrub(float timeSeconds)
	{
		if(!this->mEval.isBuilt())
		{
			return;
		}
		// re-seat the current clip at time 0 then advance to the requested time
		// (the evaluator has no absolute cursor setter; update wraps/clamps)
		const int index = this->mEval.currentClip();
		if(index < 0)
		{
			return;
		}
		this->mEval.setClip(index);
		if(timeSeconds > 0.0f)
		{
			this->mEval.update(timeSeconds);
		}
		this->mWasAtEnd = false;
		this->mNeedsUpload = true;	// onUpdateComponent does the one upload
	}
	//---------------------------------------------------------
	String VectorAnimationComponent::currentClip() const
	{
		if(!this->mEval.isBuilt())
		{
			return "";
		}
		const int index = this->mEval.currentClip();
		VectorAnimAsset::Document const & document = this->mEval.document();
		if(index < 0 || index >= static_cast<int>(document.clips.size()))
		{
			return "";
		}
		return document.clips[index].name;
	}
	//---------------------------------------------------------
	int VectorAnimationComponent::getClipCount() const
	{
		return static_cast<int>(this->mEval.document().clips.size());
	}
	//---------------------------------------------------------
	String VectorAnimationComponent::getClipNames() const
	{
		String names;
		VectorAnimAsset::Document const & document = this->mEval.document();
		for(std::size_t each = 0; each < document.clips.size(); ++each)
		{
			if(each > 0)
			{
				names += ",";
			}
			names += document.clips[each].name;
		}
		return names;
	}
	//---------------------------------------------------------
	float VectorAnimationComponent::currentFrame() const
	{
		return this->mEval.isBuilt() ? this->mEval.currentFrame() : 0.0f;
	}
	//---------------------------------------------------------
	bool VectorAnimationComponent::isAtEnd() const
	{
		return this->mEval.isBuilt() && this->mEval.isAtEnd();
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::setSpeed(float speed)
	{
		this->mSpeed = speed;
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::setAnimationReference(String const & animName)
	{
		if(animName.empty())
		{
			// a live mesh tears down; a detached component just clears the name
			if(this->mMesh)
			{
				this->removeAnimation();
			}
			else
			{
				this->mAnimName = "";
				this->mAnimAssetId = "";
			}
			return;
		}
		// a detached load (unit tests, tooling) only records the state; the mesh
		// needs the scene node the component gets on attachment
		if(this->mNode)
		{
			this->loadAnimation(animName);
			return;
		}
		this->mAnimName = animName;
		this->mAnimAssetId = AssetDatabase::referenceIdForValue(
			animName, "", AssetDatabase::REF_FILE_NAME);
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::setPlayingState(bool playing)
	{
		if(playing && this->mEval.isBuilt() && this->mEval.currentClip() < 0)
		{
			// nothing selected yet: start the initial clip
			this->applyInitialClip();
		}
		this->mPlaying = playing;
		if(playing)
		{
			this->mWasAtEnd = false;
		}
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::setTransitionTime(float seconds)
	{
		this->mTransitionTime = seconds < 0.0f ? 0.0f : seconds;
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::setTint(float red, float green, float blue,
		float alpha)
	{
		this->mTint = Color(red, green, blue, alpha);
		// the tint lives in the vertex colours - refill to apply it. A fresh
		// setMesh (not updateVertices) so this stays safe when a runtime also
		// uploads this frame: onUpdateComponent skips its dynamic upload for one
		// frame after any setMesh (mFreshBuild), so the buffer maps once. The
		// editor (which never ticks) shows the new tint immediately.
		if(this->mMesh)
		{
			this->buildVerticesFromPose();
			this->pushMesh();
		}
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::setScale(float scale)
	{
		this->mScale = scale;
		if(this->mNode)
		{
			// uniform scale on the rig's own node (z is flat, so z-scale is
			// harmless); the feather width stays rig-local and scales with it
			this->getNode()->setScale(Vec3(scale, scale, scale));
		}
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::setEdgeSoftness(float width)
	{
		this->mEdgeSoftness = width;
		if(this->mMesh)
		{
			// feather is baked geometry: a fresh setMesh (count may change)
			this->buildVerticesFromPose();
			this->pushMesh();
		}
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::setZOrder(int zOrder)
	{
		this->mZOrder = std::clamp(zOrder, ZORDER_MIN, ZORDER_MAX);
		if(this->mMesh)
		{
			this->mMesh->setZOrder(this->mZOrder);
		}
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::setAnimationVisible(bool visible)
	{
		this->mVisible = visible;
		if(this->mNode)
		{
			this->applyVisibility();
		}
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void VectorAnimationComponent::onAdd()
	{
		oAssert(!this->mMesh);
		oAssert(!this->mNode);
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent =
			componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);
		optr<RenderNode> node = transformComponent->createChildNode(
			componentOwner->getObjectID() + ".VectorAnimationComponent.sceneNode");
		oAssert(node);
		this->initSceneNodeGuard(node, componentOwner->getEventManager(), this);
		// an animation reference recorded while detached builds now the node exists
		if(!this->mAnimName.empty() && !this->mMesh)
		{
			this->loadAnimation(this->mAnimName);
		}
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::onRemove()
	{
		// content first, then the node (a node must outlive its content)
		this->mMesh.reset();
		this->mAnimName = "";
		this->mAnimAssetId = "";
		this->mEval = VectorAnimEval();
		this->mBuilt.clear();
		this->mScratchRegions.clear();
		this->mVertices.clear();
		this->mFreshBuild = false;
		this->deinitSceneNodeGuard();
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::onSetActive(bool activeInHierarchy)
	{
		if(this->mNode)
		{
			this->applyVisibility();
		}
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::onUpdateComponent(float deltaTime)
	{
		// dormant unless a rig is built (like ScriptComponent, only a
		// GameObject-ticking runtime reaches here - never the editor). This is
		// the SINGLE per-frame upload site: playback setters only mutate the
		// evaluator + flag mNeedsUpload, so the dynamic buffer maps at most once
		// per frame (the next backend forbids two maps between renders).
		if(!this->mMesh || !this->mEval.isBuilt())
		{
			return;
		}
		if(this->mPlaying)
		{
			// advance the playing clip(s) by the speed-scaled delta; the world
			// loop already feeds a time-scaled delta, so world.setTimeScale
			// composes here for free
			this->mEval.update(deltaTime * this->mSpeed);
			this->mNeedsUpload = true;
		}
		// a setMesh already mapped the buffer this frame (a load, a tint/feather
		// change): skip THIS dynamic upload one tick so the next backend never
		// maps it twice per frame (the SoftBodyDeform deferral discipline)
		if(this->mFreshBuild)
		{
			this->mFreshBuild = false;
			this->mNeedsUpload = false;
		}
		else if(this->mNeedsUpload)
		{
			this->uploadPose();
			this->mNeedsUpload = false;
		}

		// a non-looping clip that just reached its end fires the ended event
		// ONCE and holds the final pose (edge-detected so a listener that
		// re-plays sees a clean state)
		const bool atEnd = this->mEval.isAtEnd();
		if(this->mPlaying && atEnd && !this->mWasAtEnd)
		{
			this->mPlaying = false;
			this->fireEnded(this->currentClip());
		}
		this->mWasAtEnd = atEnd;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	void VectorAnimationComponent::applyInitialClip()
	{
		if(!this->mEval.isBuilt())
		{
			return;
		}
		// the reflected clip, else the rig's first clip (build() already seated
		// clip 0 at time 0, so an empty name just keeps that)
		int index = this->clipIndex(this->mClip);
		if(index < 0)
		{
			index = this->mEval.document().clips.empty() ? -1 : 0;
		}
		if(index >= 0)
		{
			this->mEval.setClip(index);
		}
		this->mWasAtEnd = false;
		// size the mesh to the resulting pose (topology + indices)
		this->buildVerticesFromPose();
		this->pushMesh();
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::buildVerticesFromPose()
	{
		// compose the evaluator's current LOCAL pose into world-space regions
		// (paint order, layer opacity folded into the fill alpha)
		this->mEval.writeRegions(this->mScratchRegions);

		// feather width: an explicit edgeSoftness, else a default proportional
		// to the pose bounds (constant visual weight at any authored scale). The
		// rig topology is fixed across frames, so building the same regions each
		// tick yields a stable vertex COUNT (only positions/colours move) - the
		// dynamic upload contract.
		const VectorTessellator::Bounds bounds =
			VectorTessellator::computeBounds(this->mScratchRegions);
		const float feather = (this->mEdgeSoftness > 0.0f)
			? this->mEdgeSoftness
			: VectorTessellator::defaultFeatherWidth(bounds);
		VectorTessellator::build(this->mScratchRegions, feather, this->mBuilt);

		// convert the POD mesh to facade vertices, multiplying the instance tint
		// into every fill/feather colour (the composed alpha already carries the
		// animated layer opacity)
		this->mVertices.resize(this->mBuilt.positions.size());
		for(std::size_t each = 0; each < this->mBuilt.positions.size(); ++each)
		{
			VectorTessellator::Point const & point = this->mBuilt.positions[each];
			VectorTessellator::Colour const & colour = this->mBuilt.colours[each];
			VectorMesh::Vertex & vertex = this->mVertices[each];
			vertex.position = Vec2(point.x, point.y);
			vertex.colour = Color(colour.r * this->mTint.r,
				colour.g * this->mTint.g, colour.b * this->mTint.b,
				colour.a * this->mTint.a);
		}
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::pushMesh()
	{
		if(!this->mMesh)
		{
			return;
		}
		this->mMesh->setMesh(
			this->mVertices.empty() ? NULL : this->mVertices.data(),
			this->mVertices.size(),
			this->mBuilt.indices.empty() ? NULL : this->mBuilt.indices.data(),
			this->mBuilt.indices.size());
		this->applyStateToMesh();
		// setMesh mapped the (dynamic) buffer this frame: the next backend forbids
		// a second map before the frame renders, so defer the first refill a tick
		this->mFreshBuild = true;
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::uploadPose()
	{
		if(!this->mMesh)
		{
			return;
		}
		// the single dynamic upload (onUpdateComponent is the only caller, after
		// its mFreshBuild guard). Rebuild the pose then push through the DYNAMIC
		// fast path, unless the topology changed (a rare degenerate frame the
		// tessellator can not fill) - which needs a fresh setMesh + index buffer.
		const std::size_t previousCount = this->mVertices.size();
		this->buildVerticesFromPose();
		// updateVertices needs the SAME count as the last setMesh; a topology
		// change (a rare degenerate frame the tessellator can not fill) instead
		// needs a fresh setMesh (new index buffer)
		if(this->mVertices.size() != previousCount)
		{
			this->pushMesh();
			return;
		}
		this->mMesh->updateVertices(
			this->mVertices.empty() ? NULL : this->mVertices.data(),
			this->mVertices.size());
	}
	//---------------------------------------------------------
	int VectorAnimationComponent::clipIndex(String const & clip) const
	{
		if(!this->mEval.isBuilt() || clip.empty())
		{
			return -1;
		}
		return this->mEval.findClip(clip);
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::applyStateToMesh()
	{
		if(this->mMesh)
		{
			this->mMesh->setZOrder(this->mZOrder);
		}
		if(this->mNode)
		{
			this->getNode()->setScale(
				Vec3(this->mScale, this->mScale, this->mScale));
		}
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::applyVisibility()
	{
		oAssert(this->mNode);
		GameObject* componentOwner = this->getComponentOwner();
		const bool ownerActive =
			!componentOwner || componentOwner->isActiveInHierarchy();
		// only over the rig's OWN node (child GameObjects gate themselves)
		this->setVisible(this->mVisible && ownerActive);
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::fireEnded(String const & clip)
	{
		// the owned event (a listener bound to the owner hears it), matching the
		// SpriteAnimationEndedEvent pattern; the name is copied into the payload
		this->mEventData->setValue(clip);
		GameObject* componentOwner = this->getComponentOwner();
		if(componentOwner)
		{
			componentOwner->triggerEvent(
				Event(VectorAnimationEndedEvent, this->mEventData));
		}
		// mirror onto the script event bus as "animation.ended" (a {clip=}
		// payload any script can subscribe to) - the gui/physics one-liner
		ScriptEventPayload payload;
		payload.setString("clip", clip);
		ScriptEventBus::getSingleton().emit("animation.ended", payload);
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		// reflection-driven NAMED serialization: playback settings + tint/scale/
		// zOrder/visibility THEN the animation AssetRef last (its stable id rides
		// the record for rename survival, and its setter builds the rig from the
		// state set just above). The live playback cursor (current clip / time /
		// crossfade) does NOT round-trip - a loaded rig replays from its clip
		// property (the Sprite/AnimationComponent precedent)
		SceneSerializer::saveComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	void VectorAnimationComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		SceneSerializer::loadComponentProperties(ar, *this);
		if(this->mNode)
		{
			this->applyVisibility();
		}
	}
	//---------------------------------------------------------
	OOBJECT_IMPL(VectorAnimationComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(loadAnimation)
		OFUNC(removeAnimation)
		OFUNCCR(getAnimationName)
		OFUNC(hasAnimation)
		OFUNC(getTriangleCount)
		OFUNC(getVertexCount)
		OFUNC(getPoseSignature)
		// playback drive (Lua self.anim + native): a script plays/blends clips,
		// scrubs, queries state; the getters feed selfchecks/MCP
		OFUNC(play)
		OFUNC(stop)
		OFUNC(setClip)
		OFUNC(crossFade)
		OFUNC(scrub)
		OFUNC(isPlaying)
		OFUNC(currentClip)
		OFUNC(getClipCount)
		OFUNC(getClipNames)
		OFUNC(currentFrame)
		OFUNC(isAtEnd)
		OFUNC(setSpeed)
		OFUNC(getSpeed)
		// reflected schema: playback settings + scalar/colour state THEN the
		// animation reference last (its setter builds the rig from the state set
		// just above - the clip/speed/playing the reflection wrote)
		OPROPERTY("clip", Orkige::PropertyKind::String, getClipProperty, setClipProperty, Orkige::PROP_NONE)
		OPROPERTY("speed", Orkige::PropertyKind::Float, getSpeed, setSpeed, Orkige::PROP_NONE)
		OPROPERTY("playing", Orkige::PropertyKind::Bool, isPlaying, setPlayingState, Orkige::PROP_NONE)
		OPROPERTY("transitionTime", Orkige::PropertyKind::Float, getTransitionTime, setTransitionTime, Orkige::PROP_NONE)
		OPROPERTY("tint", Orkige::PropertyKind::Color, getTint, setTintColor, Orkige::PROP_NONE)
		OPROPERTY("scale", Orkige::PropertyKind::Float, getScale, setScale, Orkige::PROP_NONE)
		OPROPERTY("edgeSoftness", Orkige::PropertyKind::Float, getEdgeSoftness, setEdgeSoftness, Orkige::PROP_NONE)
		OPROPERTY("zOrder", Orkige::PropertyKind::Int, getZOrder, setZOrder, Orkige::PROP_NONE)
		OPROPERTY("visible", Orkige::PropertyKind::Bool, isAnimationVisible, setAnimationVisible, Orkige::PROP_NONE)
		OPROPERTY_REF("animation", Orkige::PropertyKind::AssetRef, "vectoranim", getAnimationName, setAnimationReference, Orkige::PROP_NONE)

		// self.anim / world.get(id):getAnim... hand Lua a WEAK handle: locks per
		// call, raises an honest error naming the owner once gone. @see TransformComponent.
		OWEAKHANDLE_BEGIN(Orkige::VectorAnimationComponent, "VectorAnimationComponentHandle", "component handle", "component")
			OWEAKHANDLE_BASEMETHOD(loadAnimation)
			OWEAKHANDLE_BASEMETHOD(removeAnimation)
			OWEAKHANDLE_BASEMETHOD(getAnimationName)
			OWEAKHANDLE_BASEMETHOD(hasAnimation)
			OWEAKHANDLE_BASEMETHOD(getTriangleCount)
			OWEAKHANDLE_BASEMETHOD(getVertexCount)
			OWEAKHANDLE_BASEMETHOD(getPoseSignature)
			OWEAKHANDLE_BASEMETHOD(play)
			OWEAKHANDLE_BASEMETHOD(stop)
			OWEAKHANDLE_BASEMETHOD(setClip)
			OWEAKHANDLE_BASEMETHOD(crossFade)
			OWEAKHANDLE_BASEMETHOD(scrub)
			OWEAKHANDLE_BASEMETHOD(isPlaying)
			OWEAKHANDLE_BASEMETHOD(currentClip)
			OWEAKHANDLE_BASEMETHOD(getClipCount)
			OWEAKHANDLE_BASEMETHOD(getClipNames)
			OWEAKHANDLE_BASEMETHOD(currentFrame)
			OWEAKHANDLE_BASEMETHOD(isAtEnd)
			OWEAKHANDLE_BASEMETHOD(setSpeed)
			OWEAKHANDLE_BASEMETHOD(getSpeed)
		OWEAKHANDLE_END
	OOBJECT_END
}
