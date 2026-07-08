/********************************************************************
	created:	Monday 2010/08/30 at 16:03
	filename: 	AnimationComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_gocomponent/AnimationComponent.h"
#include "engine_gocomponent/ModelComponent.h"
#include <core_game/GameObject.h>
#include "engine_gocomponent/TransformComponent.h"

#ifdef ORKIGE_RENDER_CLASSIC
// ROOT-MOTION BACKDOOR (decided design question #1 in
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
		if(this->hasAnimations())
		{
			if(!this->hasPlayingAnimations())
			{
				bool playDefaultAnimation = this->playAnimation(this->defaultAnimation, true);
				oAssert(playDefaultAnimation);
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
			this->getComponentOwner()->triggerEvent(Event(AnimationsLoaded));
		}
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
	}
	//---------------------------------------------------------
	void AnimationComponent::handleMotionRotation(String const & animationName, float timeDelta)
	{
#ifdef ORKIGE_RENDER_CLASSIC
		// ROOT-MOTION BACKDOOR - classic-only by decision #1 (see the
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
#else //ORKIGE_RENDER_CLASSIC
		// no facade bone/keyframe API by design (question #1) - inert
		(void)animationName;
		(void)timeDelta;
#endif //ORKIGE_RENDER_CLASSIC
	}
	//---------------------------------------------------------
	// @TODO(scene format v2): serialize the enabled animations, weights,
	// time positions and speed - until then a saved scene only restores an
	// empty AnimationComponent (the sibling ModelComponent restores the model)
	void AnimationComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		oDebugMsg("scene",0,"AnimationComponent: animation runtime state (enabled animations, weights, time positions) is not serialized yet");
	}
	//---------------------------------------------------------
	void AnimationComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
	}
	//---------------------------------------------------------
	OOBJECT_IMPL(AnimationComponent)
		GAMEOBJECTCOMPONENT()
		OFUNCIR(getAvailableAnimations)
		OFUNCIR(getBoneNames)
		OFUNCCR(getDefaultAnimation)
		OFUNC(setDefaultAnimation)
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
	OOBJECT_END


	//---------------------------------------------------------
}
