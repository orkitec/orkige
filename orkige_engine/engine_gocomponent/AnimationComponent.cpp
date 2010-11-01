/********************************************************************
	created:	Monday 2010/08/30 at 16:03
	filename: 	AnimationComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_gocomponent/AnimationComponent.h"
#include "engine_gocomponent/ModelComponent.h"
#include <core_game/GameObject.h>
#include "engine_gocomponent/TransformComponent.h"
#include "engine_graphic/Engine.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	AnimationComponent::AnimationComponent()
	{
		this->addDependency<ModelComponent>();
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
			hasAnims = this->animationStates->getAnimationStateIterator().hasMoreElements();
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
		Ogre::ConstEnabledAnimationStateIterator it = this->animationStates->getEnabledAnimationStateIterator();
		while(it.hasMoreElements())
		{
			Ogre::AnimationState * state = it.peekNext();

			if(this->handleMotion || this->handleRotation)
			{
				this->handleMotionRotation(state, timeDelta);
			}

			state->addTime(timeDelta);

			it.moveNext();
		}
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void AnimationComponent::onAdd()
	{
		this->registerEvent(Engine::FrameStartedEvent,	&AnimationComponent::onFrameStarted,	this);
		this->registerEvent(ModelComponent::ModelRemovedEvent,	&AnimationComponent::onModelRemoved,	this);
		this->registerEvent(ModelComponent::ModelSetEvent,		&AnimationComponent::onModelSet,		this);

		this->handleMotion = false;
		this->handleRotation = false;
		this->extractMotion = false;
		this->extractRotation = false;

		this->getAnimationsFromModel();
	}
	//---------------------------------------------------------
	void AnimationComponent::onRemove()
	{
		this->unregisterEvent(Engine::FrameStartedEvent);
		this->unregisterEvent(ModelComponent::ModelRemovedEvent);
		this->unregisterEvent(ModelComponent::ModelSetEvent);
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	bool AnimationComponent::onFrameStarted(Event const & event)
	{
		if(this->hasAnimations())
		{
			optr<FrameEventData> data = event.getDataPtr<FrameEventData>();

			if(!this->hasPlayingAnimations())
			{
				bool playDefaultAnimation = this->playAnimation(this->defaultAnimation, true);
				oAssert(playDefaultAnimation);
			}
			this->updateAnimations(data->timeSinceLastFrame);
		}
		return false;
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
	Ogre::Entity* AnimationComponent::getAnimableModel()
	{
		GameObject* owner = this->getComponentOwner();
		oAssert(owner);
		optr<ModelComponent> modelComponent = owner->getComponent<ModelComponent>().lock();
		oAssert(modelComponent);
		Ogre::Entity* model = modelComponent->getModel();

		if(model && model->hasSkeleton())
		{
			return model;
		}
		return NULL;
	}
	//---------------------------------------------------------
	void AnimationComponent::getAnimationsFromModel()
	{
		this->defaultAnimation.clear();
		this->availableAnimations.clear();
		this->boneNames.clear();
		this->animationStates = NULL;
		if(Ogre::Entity* model = this->getAnimableModel())
		{
			model->setDisplaySkeleton(true);
			this->animationStates = model->getAllAnimationStates();
			if(this->animationStates)
			{
				Ogre::AnimationStateIterator it = this->animationStates->getAnimationStateIterator();
				while(it.hasMoreElements())
				{
					String animationName = it.peekNextKey();
					if(this->defaultAnimation.empty())
						this->defaultAnimation = animationName;

					Ogre::AnimationState* animationState = it.peekNextValue();
					animationState->setEnabled(false);
					animationState->setTimePosition(0.f);
					this->availableAnimations.push_back(animationName);
					it.moveNext();
				}
			}
			Ogre::Skeleton* skeleton = model->getSkeleton();
			Ogre::Skeleton::BoneIterator boneIt = skeleton->getBoneIterator();
			while(boneIt.hasMoreElements())
			{
				Ogre::Bone* bone = boneIt.getNext();
				this->boneNames.push_back(bone->getName());
				if(this->motionBone.empty())
					this->motionBone = bone->getName();
			}
		}
	}
	//---------------------------------------------------------
	void AnimationComponent::handleMotionRotation(Ogre::AnimationState * state, float timeDelta)
	{
		if(Ogre::Entity* model = this->getAnimableModel())
		{
			Ogre::Bone * bone = model->getSkeleton()->getBone(this->motionBone/*"Spineroot"*/);

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
					for(int i = 0 ; i <track->getNumKeyFrames(); i++)
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
