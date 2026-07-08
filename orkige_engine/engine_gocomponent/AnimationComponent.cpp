/********************************************************************
	created:	Monday 2010/08/30 at 16:03
	filename: 	AnimationComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_gocomponent/AnimationComponent.h"
#include "engine_gocomponent/ModelComponent.h"
#include <core_game/GameObject.h>
#include "engine_gocomponent/TransformComponent.h"
#include "engine_graphic/Engine.h"

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
		bool hasAnims = false;
		if(this->animationStates)
		{
			// OGRE 14: getAnimationStateIterator() is gone, getAnimationStates() returns the map
			hasAnims = !this->animationStates->getAnimationStates().empty();
		}
		return hasAnims;
	}
	//---------------------------------------------------------
	bool AnimationComponent::hasPlayingAnimations()
	{
		bool hasAnims = false;
		if(this->animationStates)
		{
			hasAnims = this->animationStates->hasEnabledAnimationState();
		}
		return hasAnims;
	}
	//---------------------------------------------------------
	bool AnimationComponent::playAnimation(String const & anim, bool loop)
	{
		if(this->hasAnimations())
		{
			if(this->animationStates->hasAnimationState(anim))
			{
				Ogre::AnimationState* animState = this->animationStates->getAnimationState(anim);
				animState->setTimePosition(0.f);//set animation to start
				animState->setEnabled(true);
				animState->setLoop(loop);

				//save positions for calculatin movement deltas
				optr<TransformComponent> transform = this->getComponentOwner()->getComponent<TransformComponent>().lock();
				Ogre::Quaternion currentOrientation = transform->getOrientation();
				currentOrientation.normalise();
				this->initialStateTransforms[anim] = SimpleTransform(transform->getPosition(), currentOrientation, transform->getScale());

				return true;
			}
		}
		return false;
	}
	//---------------------------------------------------------
	bool AnimationComponent::stopAnimation(String const & anim)
	{
		if(this->hasAnimations())
		{
			if(this->animationStates->hasAnimationState(anim))
			{
				Ogre::AnimationState* animState = this->animationStates->getAnimationState(anim);
				animState->setTimePosition(0.f);//set animation to start
				animState->setEnabled(false);
				return true;
			}
		}
		return false;
	}
	//---------------------------------------------------------
	void AnimationComponent::setDefaultAnimation(String const & anim)
	{
		if(this->hasAnimations())
		{
			if(this->animationStates->hasAnimationState(this->defaultAnimation))
			{
				Ogre::AnimationState* animState = this->animationStates->getAnimationState(this->defaultAnimation);
				animState->setEnabled(false);
			}
		}
		this->defaultAnimation = anim;
	}
	//---------------------------------------------------------
	void AnimationComponent::updateAnimations(float timeDelta)
	{
		oAssert(this->animationStates);
		std::vector<Ogre::AnimationState*> endedAnimations;
		// OGRE 14: getEnabledAnimationStateIterator() is gone, range-for over the list
		for(Ogre::AnimationState * state : this->animationStates->getEnabledAnimationStates())
		{
			if(state->hasEnded())
			{
				endedAnimations.push_back(state);
			}
			else
			{
				if(this->handleMotion || this->handleRotation)
				{
					this->handleMotionRotation(state, timeDelta * this->speed);
				}

				state->addTime(timeDelta * this->speed);
			}
		}
		
		foreach(Ogre::AnimationState* state, endedAnimations)
		{
			state->setEnabled(false);
			this->eventData->setValue(state->getAnimationName());
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
		this->animationStates = NULL;
		return true;
	}
	//---------------------------------------------------------
	bool AnimationComponent::onModelSet(Event const & event)
	{
		this->availableAnimations.clear();
		this->defaultAnimation.clear();
		this->motionBone.clear();
		this->boneNames.clear();
		this->animationStates = NULL;
		this->getAnimationsFromModel();
		return true;
	}
	//---------------------------------------------------------
	Ogre::Entity const * AnimationComponent::getAnimableModel()
	{
		GameObject* owner = this->getComponentOwner();
		oAssert(owner);
		optr<ModelComponent> modelComponent = owner->getComponent<ModelComponent>().lock();
		oAssert(modelComponent);
		Ogre::Entity const * model = modelComponent->getModel();
		return model;
	}
	//---------------------------------------------------------
	void AnimationComponent::getAnimationsFromModel()
	{
		this->defaultAnimation.clear();
		this->availableAnimations.clear();
		this->boneNames.clear();
		this->animationStates = NULL;
		if(Ogre::Entity const * model = this->getAnimableModel())
		{
			//model->setDisplaySkeleton(true);
			this->animationStates = model->getAllAnimationStates();
			if(this->animationStates)
			{
				// OGRE 14: getAnimationStateIterator() is gone, range-for over the map
				for(Ogre::AnimationStateMap::value_type const & animEntry : this->animationStates->getAnimationStates())
				{
					String const & animationName = animEntry.first;
					if(this->defaultAnimation.empty())
						this->defaultAnimation = animationName;

					Ogre::AnimationState* animationState = animEntry.second;
					animationState->setEnabled(false);
					animationState->setTimePosition(0.f);
					this->availableAnimations.push_back(animationName);
				}
				this->getComponentOwner()->triggerEvent(Event(AnimationsLoaded));
			}
			Ogre::Skeleton* skeleton = model->getSkeleton();
			if(skeleton)
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
		
	}
	//---------------------------------------------------------
	void AnimationComponent::handleMotionRotation(Ogre::AnimationState * state, float timeDelta)
	{
		if(Ogre::Entity const * model = this->getAnimableModel())
		{
			Ogre::Skeleton* skeleton = model->getSkeleton();
			if(skeleton)
			{
				Ogre::Bone * bone = skeleton->getBone(this->motionBone/*"Spineroot"*/);
				if(bone)
				{
					oAssert(bone);

					String const & animationName = state->getAnimationName();
					Ogre::SkeletonInstance* skeleton = model->getSkeleton();
					Ogre::Animation* anim = skeleton->getAnimation(animationName);
					Ogre::TransformKeyFrame interPolatedKeyframe(0,0);

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


						track->getInterpolatedKeyFrame(state->getTimePosition(), &interPolatedKeyframe);

						optr<TransformComponent> transform = this->getComponentOwner()->getComponent<TransformComponent>().lock();

						if(state->getTimePosition() >= state->getLength() || state->getTimePosition() + timeDelta >= state->getLength() || interPolatedKeyframe.getTime() == endKf->getTime())
						{
							Ogre::Quaternion currentOrientation = transform->getOrientation();
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
							this->backuppedKeyframes[animationName][bonehandle].push_back(KeyFrameBackup(i, kf));
							kf->setScale(startKf->getScale());
							if(this->handleRotation)
								kf->setRotation(startKf->getRotation());
							kf->setTranslate(startKf->getTranslate());

						}
					}
				}
			}
		}
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
