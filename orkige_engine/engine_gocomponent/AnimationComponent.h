/********************************************************************
	created:	Monday 2010/08/30 at 15:58
	filename: 	AnimationComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __AnimationComponent_h__30_8_2010__15_58_30__
#define __AnimationComponent_h__30_8_2010__15_58_30__

#include <core_game/GameObjectComponent.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_util/StringUtil.h"

namespace Orkige
{
	//! component which can manage Animation Playback on GameObject's
	class ORKIGE_ENGINE_DLL AnimationComponent : public GameObjectComponent
	{
		OOBJECT(AnimationComponent, GameObjectComponent)
		//--- Types -------------------------------------------------
	public:
		//! @brief triggered when a playing animation has reached the end and is not looping
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(AnimationEndedEvent);
		//! @brief triggered when the animations set is loaded means that the model is loaded 
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(AnimationsLoaded);
	protected:
	private:
		struct SimpleTransform
		{
			inline SimpleTransform();
			inline SimpleTransform(SimpleTransform const & other);
			inline SimpleTransform(Ogre::Vector3 const & _translate, Ogre::Quaternion const & _rotation, Ogre::Vector3 const & _scale);
			Ogre::Vector3 translate;
			Ogre::Quaternion rotation;
			Ogre::Vector3 scale;
		};
		struct KeyFrameBackup : public SimpleTransform
		{
			inline KeyFrameBackup(unsigned short _index, Ogre::TransformKeyFrame * kf);
			unsigned short index;
		};
		typedef std::vector<KeyFrameBackup> KeyFrameBackupVector;
		typedef std::map<String, SimpleTransform> TransformRegistry;
		typedef std::map<String, std::map<unsigned short, KeyFrameBackupVector> > KeyFrameBackupRegistry;
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		StringList						availableAnimations;
		StringList						boneNames;
		String							defaultAnimation;
		String							motionBone;
		Ogre::AnimationStateSet*		animationStates;
		bool							handleMotion;
		bool							handleRotation;
		bool							extractMotion;
		bool							extractRotation;
		TransformRegistry				initialStateTransforms;
		KeyFrameBackupRegistry			backuppedKeyframes;
		optr<StringUtil::StringObject>	eventData;				//!< name of set or removed model
		float							speed;					//!< sets the speed of the animation
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		AnimationComponent();
		//! destructor
		virtual ~AnimationComponent();

		//! get list of available animation names
		inline StringList const & getAvailableAnimations();
		//! get list of bone names
		inline StringList const & getBoneNames();
		//! get default anim name
		inline String const & getDefaultAnimation();
		//! get animation states
		inline Ogre::AnimationStateSet const * getAnimationStates() const;
		//! set default anim name
		void setDefaultAnimation(String const & anim);
		//! are animations available
		bool hasAnimations();
		//! are animations playing?
		bool hasPlayingAnimations();
		//! play a anim
		bool playAnimation(String const & anim, bool loop);
		//! stop a anim
		bool stopAnimation(String const & anim);
		//! update playing animations
		void updateAnimations(float timeDelta);
		//! should component handle motions in animations?
		inline bool getHandleMotion();
		//! should component handle rotations in animations?
		inline bool getHandleRotation();
		//! should motions be extracted from anim
		inline bool getExtractMotion();
		//! should rotations should be extracted from anim
		inline bool getExtractRotation();

		//! set motion handling
		inline void setHandleMotion(bool handleMotion);
		//! set rotation handling
		inline void setHandleRotation(bool handleRotation);
		//! set motion extraction
		inline void setExtractMotion(bool extractMotion);
		//! set rotation extraction
		inline void setExtractRotation(bool extractRotation);

		//! get bone for which motions should be handled/extracted
		inline String const & getMotionBone();
		//! set bone for which motions should be handled/extracted
		inline void setMotionBone(String const & boneName);

		//! pause playing animations
		inline void pause();
		//! resume playing animations
		inline void resume();
		//! is the AnimationComponent is paused 
		inline bool isPaused();

		//! set animations speed
		inline void setSpeed(float speed);
		//! get animation speed
		inline float getSpeed();
	protected:
		//! component override gets called after the component is attached to a GameObject
		virtual void onAdd();
		//! component override gets called before the component is removed from a GameObject
		virtual void onRemove();
		//--- SERIALIZATION ---
		//! @warning animation runtime state does not round-trip yet (logs a warning)
		virtual void save(optr<IArchive> const & ar);
		//! @warning animation runtime state does not round-trip yet
		virtual void load(optr<IArchive> const & ar);
	private:
		//! overridable to update the component
		virtual void onUpdateComponent(float deltaTime);

		bool onModelRemoved(Event const & event);
		bool onModelSet(Event const & event);

		void getAnimationsFromModel();
		Ogre::Entity const * getAnimableModel();
		void handleMotionRotation(Ogre::AnimationState * state, float timeDelta);
	};
	//---------------------------------------------------------------
	inline AnimationComponent::SimpleTransform::SimpleTransform()
	{
	}
	//---------------------------------------------------------------
	inline AnimationComponent::SimpleTransform::SimpleTransform(SimpleTransform const & other)
		: translate(other.translate), rotation(other.rotation), scale(other.scale)
	{
	}
	//---------------------------------------------------------------
	inline AnimationComponent::SimpleTransform::SimpleTransform(Ogre::Vector3 const & _translate, Ogre::Quaternion const & _rotation, Ogre::Vector3 const & _scale)
		: translate(_translate), rotation(_rotation), scale(_scale)
	{
	}
	//---------------------------------------------------------------
	inline AnimationComponent::KeyFrameBackup::KeyFrameBackup(unsigned short _index, Ogre::TransformKeyFrame * kf)
		: AnimationComponent::SimpleTransform(kf->getTranslate(), kf->getRotation(), kf->getScale()), index(_index)
	{
	}
	//---------------------------------------------------------------
	inline StringList const & AnimationComponent::getAvailableAnimations()
	{
		return this->availableAnimations;
	}
	//---------------------------------------------------------------
	inline StringList const & AnimationComponent::getBoneNames()
	{
		return this->boneNames;
	}
	//---------------------------------------------------------------
	inline String const & AnimationComponent::getDefaultAnimation()
	{
		return this->defaultAnimation;
	}
	//---------------------------------------------------------------
	inline Ogre::AnimationStateSet const * AnimationComponent::getAnimationStates() const 
	{
		return this->animationStates;
	}
	//---------------------------------------------------------------
	inline bool AnimationComponent::getHandleMotion()
	{
		return this->handleMotion;
	}
	//---------------------------------------------------------------
	inline bool AnimationComponent::getHandleRotation()
	{
		return this->handleRotation;
	}
	//---------------------------------------------------------------
	inline bool AnimationComponent::getExtractMotion()
	{
		return this->extractMotion;
	}
	//---------------------------------------------------------------
	inline bool AnimationComponent::getExtractRotation()
	{
		return this->extractRotation;
	}
	//---------------------------------------------------------------
	inline void AnimationComponent::setHandleMotion(bool handleMotion)
	{
		this->handleMotion = handleMotion;
	}
	//---------------------------------------------------------------
	inline void AnimationComponent::setHandleRotation(bool handleRotation)
	{
		this->handleRotation = handleRotation;
	}
	//---------------------------------------------------------------
	inline void AnimationComponent::setExtractMotion(bool extractMotion)
	{
		this->extractMotion = extractMotion;
	}
	//---------------------------------------------------------------
	inline void AnimationComponent::setExtractRotation(bool extractRotation)
	{
		this->extractRotation = extractRotation;
	}
	//---------------------------------------------------------------
	inline String const & AnimationComponent::getMotionBone()
	{
		return this->motionBone;
	}
	//---------------------------------------------------------------
	inline void AnimationComponent::setMotionBone(String const & boneName)
	{
		this->motionBone = boneName;
	}
	//---------------------------------------------------------------
	inline void AnimationComponent::pause()
	{
		this->setWantsUpdates(false);
	}
	//---------------------------------------------------------------
	inline void AnimationComponent::resume()
	{
		this->setWantsUpdates(true);
	}
	//---------------------------------------------------------------
	inline bool AnimationComponent::isPaused()
	{
		return !this->getWantsUpdates();
	}
	//---------------------------------------------------------------
	inline void AnimationComponent::setSpeed(float speed)
	{
		this->speed = speed;
	}
	//---------------------------------------------------------------
	inline float AnimationComponent::getSpeed()
	{
		return this->speed;
	}
}

#endif //__AnimationComponent_h__30_8_2010__15_58_30__
