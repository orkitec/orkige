/********************************************************************
	created:	Monday 2010/08/30 at 16:03
	filename: 	AnimationComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_gocomponent/AnimationComponent.h"
#include <core_script/ScriptRuntime.h>	// OSCRIPT_HANDLE: ScriptComponentAccess registry
#include "engine_gocomponent/ModelComponent.h"
#include "engine_gocomponent/ComponentPropertyReflect.h"
#include <core_game/GameObject.h>
#include <core_game/SceneSerializer.h>
#include "engine_gocomponent/TransformComponent.h"

#ifdef ORKIGE_RENDER_CLASSIC
// ROOT-MOTION BACKDOOR (a deliberate design decision, see
// Docs/render-abstraction.md): extracting root motion digs into
// Ogre::Bone / NodeAnimationTrack / TransformKeyFrame - far below the
// facade's scene-graph level and without a 1:1 shape on Ogre-Next or
// Filament. This is THE single sanctioned include of the private backend
// header outside engine_render_classic/ and engine_graphic/Engine.cpp:
// RenderBackend::ogreEntity hands the backdoor the wrapped entity. On
// other backends handleMotionRotation is an inert no-op until a real
// cross-backend need adds a facade bone API.
#include "engine_render_classic/ClassicBackend.h"
#endif //ORKIGE_RENDER_CLASSIC

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(AnimationComponent, AnimationEndedEvent);
	IMPL_OWNED_EVENTTYPE(AnimationComponent, AnimationsLoaded);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	AnimationComponent::AnimationComponent()
	{

		this->addDependency<ModelComponent>();
		this->eventData = onew(new StringUtil::StringObject(StringUtil::BLANK));
		this->setWantsUpdates(true);
		// the reflected state has defined values even on a DETACHED component
		// (no onAdd runs there - the reflection round-trip tests exercise it)
		this->speed = 1.f;
		this->primaryTime = 0.f;
		this->primaryLoop = true;
		this->paused = false;
		this->pendingRestore = false;
		this->restorePaused = false;
	}
	//---------------------------------------------------------
	AnimationComponent::~AnimationComponent()
	{
	}
	//---------------------------------------------------------
	bool AnimationComponent::hasAnimations()
	{
		// mirrors the loaded model (kept in step by the ModelSet/Removed events)
		return !this->availableAnimations.empty();
	}
	//---------------------------------------------------------
	bool AnimationComponent::hasPlayingAnimations()
	{
		bool hasAnims = false;
		if(optr<MeshInstance> mesh = this->getAnimableMesh())
		{
			hasAnims = !mesh->getEnabledAnimations().empty();
		}
		return hasAnims;
	}
	//---------------------------------------------------------
	bool AnimationComponent::playAnimation(String const & anim, bool loop)
	{
		optr<MeshInstance> mesh = this->getAnimableMesh();
		if(mesh && mesh->hasAnimation(anim))
		{
			mesh->setAnimationTime(anim, 0.f);	//set animation to start
			mesh->setAnimationEnabled(anim, true);
			mesh->setAnimationLoop(anim, loop);

			//save positions for calculating movement deltas (root motion)
			optr<TransformComponent> transform = this->getComponentOwner()->getComponent<TransformComponent>().lock();
			Quat currentOrientation = transform->getOrientation();
			currentOrientation.normalise();
			this->initialStateTransforms[anim] = SimpleTransform(transform->getPosition(), currentOrientation, transform->getScale());

			// the serialized playback intent (kept coherent even without a tick)
			this->primaryClip = anim;
			this->primaryLoop = loop;
			this->primaryTime = 0.f;

			return true;
		}
		return false;
	}
	//---------------------------------------------------------
	bool AnimationComponent::stopAnimation(String const & anim)
	{
		optr<MeshInstance> mesh = this->getAnimableMesh();
		if(mesh && mesh->hasAnimation(anim))
		{
			mesh->setAnimationTime(anim, 0.f);	//set animation to start
			mesh->setAnimationEnabled(anim, false);
			return true;
		}
		return false;
	}
	//---------------------------------------------------------
	bool AnimationComponent::setAnimationTime(String const & anim, float seconds)
	{
		optr<MeshInstance> mesh = this->getAnimableMesh();
		if(mesh && mesh->hasAnimation(anim))
		{
			mesh->setAnimationTime(anim, seconds);
			return true;
		}
		return false;
	}
	//---------------------------------------------------------
	bool AnimationComponent::crossFadeTo(String const & anim, float durationSeconds)
	{
		optr<MeshInstance> mesh = this->getAnimableMesh();
		if(!mesh || !mesh->hasAnimation(anim))
		{
			return false;
		}
		// the outgoing clip is the first enabled one that isn't the target
		String from;
		for(String const & name : mesh->getEnabledAnimations())
		{
			if(name != anim)
			{
				from = name;
				break;
			}
		}
		// bring the target in from time 0 (looping like playAnimation defaults)
		mesh->setAnimationTime(anim, 0.f);
		mesh->setAnimationEnabled(anim, true);
		mesh->setAnimationLoop(anim, true);
		// the target becomes the primary clip for a scene save (looping)
		this->primaryClip = anim;
		this->primaryLoop = true;
		this->primaryTime = 0.f;
		if(from.empty() || durationSeconds <= 0.f)
		{
			// nothing to blend from (or an instant switch): snap to the target
			if(!from.empty())
			{
				mesh->setAnimationEnabled(from, false);
			}
			mesh->setAnimationWeight(anim, 1.f);
			this->blending = false;
			return true;
		}
		mesh->setAnimationWeight(from, 1.f);
		mesh->setAnimationWeight(anim, 0.f);
		this->blendFrom = from;
		this->blendTo = anim;
		this->blendDuration = durationSeconds;
		this->blendElapsed = 0.f;
		this->blending = true;
		return true;
	}
	//---------------------------------------------------------
	void AnimationComponent::setDefaultAnimation(String const & anim)
	{
		optr<MeshInstance> mesh = this->getAnimableMesh();
		if(mesh && mesh->hasAnimation(this->defaultAnimation))
		{
			mesh->setAnimationEnabled(this->defaultAnimation, false);
		}
		this->defaultAnimation = anim;
	}
	//---------------------------------------------------------
	void AnimationComponent::updateAnimations(float timeDelta)
	{
		optr<MeshInstance> mesh = this->getAnimableMesh();
		if(!mesh)
		{
			return;
		}
		StringVector endedAnimations;
		for(String const & animationName : mesh->getEnabledAnimations())
		{
			if(mesh->hasAnimationEnded(animationName))
			{
				endedAnimations.push_back(animationName);
			}
			else
			{
				if(this->handleMotion || this->handleRotation)
				{
					this->handleMotionRotation(animationName, timeDelta * this->speed);
				}

				mesh->addAnimationTime(animationName, timeDelta * this->speed);
			}
		}

		for(String const & animationName : endedAnimations)
		{
			mesh->setAnimationEnabled(animationName, false);
			this->eventData->setValue(animationName);
			this->getComponentOwner()->triggerEvent(Event(AnimationEndedEvent, this->eventData));
		}
		// track the live primary clip + phase so a scene save can resume here
		this->refreshPrimaryClipState(mesh);
	}
	//---------------------------------------------------------
	void AnimationComponent::refreshPrimaryClipState(optr<MeshInstance> const & mesh)
	{
		// mid-blend the incoming clip is the one to resume; otherwise the first
		// enabled clip is the primary (single-clip playback, the common case)
		String primary = this->blending ? this->blendTo : String();
		if(primary.empty())
		{
			StringVector enabled = mesh->getEnabledAnimations();
			if(!enabled.empty())
			{
				primary = enabled.front();
			}
		}
		this->primaryClip = primary;
		this->primaryTime = primary.empty()
			? 0.f : mesh->getAnimationTime(primary);
		// primaryLoop is set when a clip starts (playAnimation/crossFadeTo)
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void AnimationComponent::onAdd()
	{
		this->registerEvent(ModelComponent::ModelRemovedEvent,	&AnimationComponent::onModelRemoved,	this);
		this->registerEvent(ModelComponent::ModelSetEvent,		&AnimationComponent::onModelSet,		this);

		this->handleMotion = false;
		this->handleRotation = false;
		this->extractMotion = false;
		this->extractRotation = false;
		this->speed = 1.f;
		this->blending = false;
		this->blendDuration = 0.f;
		this->blendElapsed = 0.f;
		this->primaryClip.clear();
		this->primaryTime = 0.f;
		this->primaryLoop = true;
		this->paused = false;
		this->pendingRestore = false;
		this->restorePaused = false;
		this->getAnimationsFromModel();
	}
	//---------------------------------------------------------
	void AnimationComponent::onRemove()
	{
		this->unregisterEvent(ModelComponent::ModelRemovedEvent);
		this->unregisterEvent(ModelComponent::ModelSetEvent);
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	void AnimationComponent::onUpdateComponent(float deltaTime)
	{
		// resume a loaded playback state on the first tick the mesh is ready
		// (this runs ONLY in a ticking runtime - the editor never reaches here,
		// so an edit-mode scene load stays at the bind pose)
		if(this->pendingRestore)
		{
			this->applyPlaybackRestore();
			if(!this->pendingRestore && this->restorePaused)
			{
				// the clip is now posed at its saved phase - freeze it there
				this->pause();
				return;
			}
		}
		if(this->hasAnimations())
		{
			if(!this->hasPlayingAnimations())
			{
				bool playDefaultAnimation = this->playAnimation(this->defaultAnimation, true);
				oAssert(playDefaultAnimation);
			}
			// advance a live crossfade: ramp the outgoing weight down and the
			// incoming up over the blend duration (wall-time, unscaled), then
			// drop the outgoing clip when the blend completes
			if(this->blending)
			{
				this->blendElapsed += deltaTime;
				float progress = this->blendDuration > 0.f
					? this->blendElapsed / this->blendDuration : 1.f;
				if(progress >= 1.f)
				{
					progress = 1.f;
				}
				if(optr<MeshInstance> mesh = this->getAnimableMesh())
				{
					mesh->setAnimationWeight(this->blendFrom, 1.f - progress);
					mesh->setAnimationWeight(this->blendTo, progress);
					if(progress >= 1.f)
					{
						mesh->setAnimationEnabled(this->blendFrom, false);
						mesh->setAnimationWeight(this->blendTo, 1.f);
					}
				}
				if(progress >= 1.f)
				{
					this->blending = false;
				}
			}
			this->updateAnimations(deltaTime);
		}
	}
	//---------------------------------------------------------
	bool AnimationComponent::onModelRemoved(Event const & event)
	{
		this->availableAnimations.clear();
		this->defaultAnimation.clear();
		this->motionBone.clear();
		this->boneNames.clear();
		// the live playback is gone with the mesh; keep primaryClip as the
		// resume intent so a swap back to an equivalent rig re-applies it
		return true;
	}
	//---------------------------------------------------------
	bool AnimationComponent::onModelSet(Event const & event)
	{
		this->availableAnimations.clear();
		this->defaultAnimation.clear();
		this->motionBone.clear();
		this->boneNames.clear();
		this->getAnimationsFromModel();
		return true;
	}
	//---------------------------------------------------------
	optr<MeshInstance> AnimationComponent::getAnimableMesh()
	{
		GameObject* owner = this->getComponentOwner();
		oAssert(owner);
		optr<ModelComponent> modelComponent = owner->getComponent<ModelComponent>().lock();
		oAssert(modelComponent);
		return modelComponent->getMeshInstance();
	}
	//---------------------------------------------------------
	void AnimationComponent::getAnimationsFromModel()
	{
		this->defaultAnimation.clear();
		this->availableAnimations.clear();
		this->boneNames.clear();
		optr<MeshInstance> mesh = this->getAnimableMesh();
		if(!mesh)
		{
			return;
		}
		StringVector animationNames = mesh->getAnimationNames();
		if(!animationNames.empty())
		{
			for(String const & animationName : animationNames)
			{
				if(this->defaultAnimation.empty())
					this->defaultAnimation = animationName;

				mesh->setAnimationEnabled(animationName, false);
				mesh->setAnimationTime(animationName, 0.f);
				this->availableAnimations.push_back(animationName);
			}
			// an animated character's bounds must follow its swinging limbs so
			// culling stays honest (a no-op where the backend already does this)
			mesh->setAnimatedBounds(true);
			this->getComponentOwner()->triggerEvent(Event(AnimationsLoaded));
		}
// ORKIGE_SANCTIONED_OGRE_BEGIN(root-motion-backdoor) - lint gate, see Util/ogre_containment.json
#ifdef ORKIGE_RENDER_CLASSIC
		// bone inventory for the root-motion backdoor (see the include note)
		if(Ogre::Entity const * model = RenderBackend::ogreEntity(mesh))
		{
			if(Ogre::Skeleton* skeleton = model->getSkeleton())
			{
				// OGRE 14: getBoneIterator() is gone, range-for over getBones()
				for(Ogre::Bone* bone : skeleton->getBones())
				{
					this->boneNames.push_back(bone->getName());
					if(this->motionBone.empty())
						this->motionBone = bone->getName();
				}
			}
		}
#endif //ORKIGE_RENDER_CLASSIC
// ORKIGE_SANCTIONED_OGRE_END
	}
	//---------------------------------------------------------
	void AnimationComponent::handleMotionRotation(String const & animationName, float timeDelta)
	{
// ORKIGE_SANCTIONED_OGRE_BEGIN(root-motion-backdoor) - lint gate, see Util/ogre_containment.json
#ifdef ORKIGE_RENDER_CLASSIC
		// ROOT-MOTION BACKDOOR - classic-only by design (see the
		// include note at the top of this file)
		optr<MeshInstance> mesh = this->getAnimableMesh();
		Ogre::Entity const * model = RenderBackend::ogreEntity(mesh);
		if(!model)
		{
			return;
		}
		Ogre::SkeletonInstance* skeleton = model->getSkeleton();
		if(!skeleton)
		{
			return;
		}
		Ogre::Bone * bone = skeleton->getBone(this->motionBone/*"Spineroot"*/);
		if(bone)
		{
			Ogre::Animation* anim = skeleton->getAnimation(animationName);
			Ogre::TransformKeyFrame interPolatedKeyframe(0,0);

			const float animationTime = mesh->getAnimationTime(animationName);
			const float animationLength = mesh->getAnimationLength(animationName);

			unsigned short bonehandle = bone->getHandle();
			if(anim->hasNodeTrack(bonehandle))
			{
				Ogre::NodeAnimationTrack* track = anim->getNodeTrack(bonehandle);
				Ogre::TransformKeyFrame* startKf = track->getNodeKeyFrame(0);
				Ogre::TransformKeyFrame* endKf = track->getNodeKeyFrame(track->getNumKeyFrames()-1);
				foreach(KeyFrameBackup & kfBack, backuppedKeyframes[animationName][bonehandle])
				{
					Ogre::TransformKeyFrame* oldKf = track->getNodeKeyFrame(kfBack.index);
					oldKf->setScale(kfBack.scale);
					if(this->handleRotation)
						oldKf->setRotation(kfBack.rotation);
					oldKf->setTranslate(kfBack.translate);
				}
				this->backuppedKeyframes[animationName][bonehandle].clear();


				track->getInterpolatedKeyFrame(animationTime, &interPolatedKeyframe);

				optr<TransformComponent> transform = this->getComponentOwner()->getComponent<TransformComponent>().lock();

				if(animationTime >= animationLength || animationTime + timeDelta >= animationLength || interPolatedKeyframe.getTime() == endKf->getTime())
				{
					Quat currentOrientation = transform->getOrientation();
					currentOrientation.normalise();
					this->initialStateTransforms[animationName] = SimpleTransform(transform->getPosition(), currentOrientation, transform->getScale());
				}
				else
				{
					SimpleTransform initialTransform = this->initialStateTransforms[animationName];
					transform->setScale(initialTransform.scale * interPolatedKeyframe.getScale());
					if(this->handleRotation)
						transform->setOrientation(initialTransform.rotation * /*bone->getInitialOrientation() * */interPolatedKeyframe.getRotation());
					transform->setPosition(initialTransform.translate + /*bone->getInitialPosition() + */(interPolatedKeyframe.getTranslate()-startKf->getTranslate()));
				}
				for(unsigned short i = 0 ; i <track->getNumKeyFrames(); i++)
				{
					Ogre::TransformKeyFrame* kf = track->getNodeKeyFrame(i);
					this->backuppedKeyframes[animationName][bonehandle].push_back(
						KeyFrameBackup(i, kf->getTranslate(), kf->getRotation(), kf->getScale()));
					kf->setScale(startKf->getScale());
					if(this->handleRotation)
						kf->setRotation(startKf->getRotation());
					kf->setTranslate(startKf->getTranslate());

				}
			}
		}
// ORKIGE_SANCTIONED_OGRE_END
#else //ORKIGE_RENDER_CLASSIC
		// no facade bone/keyframe API by design - inert
		(void)animationName;
		(void)timeDelta;
#endif //ORKIGE_RENDER_CLASSIC
	}
	//---------------------------------------------------------
	void AnimationComponent::setPlaybackClip(String const & clip)
	{
		this->primaryClip = clip;
		if(!clip.empty())
		{
			// arm the resume; the live mesh is driven on the first runtime tick
			this->pendingRestore = true;
		}
	}
	//---------------------------------------------------------
	void AnimationComponent::setPlaybackTime(float seconds)
	{
		this->primaryTime = seconds < 0.f ? 0.f : seconds;
	}
	//---------------------------------------------------------
	void AnimationComponent::setPlaybackLoop(bool loop)
	{
		this->primaryLoop = loop;
	}
	//---------------------------------------------------------
	void AnimationComponent::setPaused(bool paused)
	{
		this->restorePaused = paused;
		if(this->pendingRestore)
		{
			// defer: applyPlaybackRestore poses the clip first, then the tick
			// applies this pause so it freezes at the saved phase (@see
			// onUpdateComponent). The `paused` mirror already carries the intent.
			this->paused = paused;
			return;
		}
		if(paused)
		{
			this->pause();
		}
		else
		{
			this->resume();
		}
	}
	//---------------------------------------------------------
	void AnimationComponent::applyPlaybackRestore()
	{
		if(this->primaryClip.empty())
		{
			// nothing was playing when the scene was saved: let the normal
			// auto-play-default path take over
			this->pendingRestore = false;
			return;
		}
		optr<MeshInstance> mesh = this->getAnimableMesh();
		if(!mesh || !mesh->hasAnimation(this->primaryClip))
		{
			return;	// the model is not ready yet - stay armed to retry
		}
		// resume EXACTLY the saved clip: silence whatever auto-enabled, then
		// enable the saved one at its saved phase/loop/full weight
		for(String const & name : mesh->getEnabledAnimations())
		{
			mesh->setAnimationEnabled(name, false);
		}
		mesh->setAnimationEnabled(this->primaryClip, true);
		mesh->setAnimationLoop(this->primaryClip, this->primaryLoop);
		mesh->setAnimationWeight(this->primaryClip, 1.f);
		mesh->setAnimationTime(this->primaryClip, this->primaryTime);
		// the root-motion baseline, like playAnimation
		if(optr<TransformComponent> transform =
			this->getComponentOwner()->getComponent<TransformComponent>().lock())
		{
			Quat currentOrientation = transform->getOrientation();
			currentOrientation.normalise();
			this->initialStateTransforms[this->primaryClip] = SimpleTransform(
				transform->getPosition(), currentOrientation,
				transform->getScale());
		}
		this->blending = false;
		this->pendingRestore = false;
	}
	//---------------------------------------------------------
	void AnimationComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		// reflection-driven NAMED serialization: the primary clip, its phase,
		// the loop flag, the playback speed and the pause state are written by
		// name off the declared schema, so a scene saved mid-animation resumes
		SceneSerializer::saveComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	void AnimationComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		// the setters record the resume intent (arming pendingRestore); the live
		// mesh is driven on the first runtime tick (@see onUpdateComponent)
		SceneSerializer::loadComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	OOBJECT_IMPL(AnimationComponent)
		GAMEOBJECTCOMPONENT()
		OFUNCIR(getAvailableAnimations)
		OFUNCIR(getBoneNames)
		OFUNCCR(getDefaultAnimation)
		OFUNC(setDefaultAnimation)
		// clip playback control reachable from Lua (self.animation:...) and MCP:
		// play/stop a named clip, seek it to a phase offset, and scale playback
		// speed - the skeletal counterparts of the crossFadeTo blend below
		OFUNC(playAnimation)
		OFUNC(stopAnimation)
		OFUNC(setAnimationTime)
		OFUNC(setSpeed)
		OFUNC(getSpeed)
		OFUNC(getHandleMotion)
		OFUNC(getHandleRotation)
		OFUNC(getExtractMotion)
		OFUNC(getExtractRotation)
		OFUNC(setHandleMotion)
		OFUNC(setHandleRotation)
		OFUNC(setExtractMotion)
		OFUNC(setExtractRotation)
		OFUNCCR(getMotionBone)
		OFUNC(setMotionBone)
		OFUNC(crossFadeTo)
		OFUNC(isCrossFading)
		OFUNC(getCrossFadeProgress)
		// reflected PLAYBACK STATE: the primary clip, its phase in seconds, the
		// loop flag, the playback speed and the pause state. Order-independent
		// (matched by name on load) and every field drives a component setter -
		// the ONE schema the Inspector, scene serialization, the debug protocol
		// and MCP all consume. clipTime/speed serialize the mid-animation phase;
		// the resume is applied on the first runtime tick (@see onUpdateComponent).
		OPROPERTY("clip", Orkige::PropertyKind::String, getPlaybackClip, setPlaybackClip, Orkige::PROP_NONE)
		OPROPERTY("clipTime", Orkige::PropertyKind::Float, getPlaybackTime, setPlaybackTime, Orkige::PROP_NONE)
		OPROPERTY("clipLoop", Orkige::PropertyKind::Bool, getPlaybackLoop, setPlaybackLoop, Orkige::PROP_NONE)
		OPROPERTY("speed", Orkige::PropertyKind::Float, getSpeed, setSpeed, Orkige::PROP_NONE)
		OPROPERTY("paused", Orkige::PropertyKind::Bool, getPaused, setPaused, Orkige::PROP_NONE)
		// self.animation / world.getAnimation(id) hand Lua a WEAK handle: locks
		// per call, raises an honest error naming the owner once gone. This is
		// the clip-drive surface a SCRIPT reaches (the OFUNC lines above bind
		// the direct usertype; a script only ever holds a handle). @see
		// TransformComponent / ModelComponent.
		OWEAKHANDLE_BEGIN(Orkige::AnimationComponent, "AnimationComponentHandle", "component handle", "component")
			OWEAKHANDLE_BASEMETHOD(playAnimation)
			OWEAKHANDLE_BASEMETHOD(stopAnimation)
			OWEAKHANDLE_BASEMETHOD(setAnimationTime)
			OWEAKHANDLE_BASEMETHOD(setSpeed)
			OWEAKHANDLE_BASEMETHOD(getSpeed)
			OWEAKHANDLE_BASEMETHOD(crossFadeTo)
			OWEAKHANDLE_BASEMETHOD(isCrossFading)
			OWEAKHANDLE_BASEMETHOD(getCrossFadeProgress)
			OWEAKHANDLE_BASEMETHOD(setDefaultAnimation)
			OWEAKHANDLE_BASEMETHOD(hasAnimations)
			OWEAKHANDLE_BASEMETHOD(hasPlayingAnimations)
		OWEAKHANDLE_END
		// ONE declaration wires self.animation + world.getAnimation(id) +
		// getComponent("animation") off the ScriptComponentAccess registry
		OSCRIPT_HANDLE("animation", true, "getAnimation")
	OOBJECT_END


	//---------------------------------------------------------
}
