/********************************************************************
	created:	Monday 2010/08/30 at 15:58
	filename: 	AnimationComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __AnimationComponent_h__30_8_2010__15_58_30__
#define __AnimationComponent_h__30_8_2010__15_58_30__

#include <core_game/GameObjectComponent.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_render/MeshInstance.h"
#include "engine_util/StringUtil.h"

namespace Orkige
{
	//! @brief component which can manage Animation Playback on GameObject's
	//! @remarks playback runs
	//! on the facade MeshInstance animation surface of the sibling
	//! ModelComponent. The root-motion extraction (handleMotion/handleRotation
	//! + motionBone) digs into skeleton bones and keyframes - that stays a
	//! CLASSIC-ONLY backdoor by deliberate design: on other
	//! backends those switches are inert no-ops (and boneNames stays empty)
	//! until a real cross-backend root-motion need adds a facade bone API.
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
			inline SimpleTransform(Vec3 const & _translate, Quat const & _rotation, Vec3 const & _scale);
			Vec3 translate;
			Quat rotation;
			Vec3 scale;
		};
		struct KeyFrameBackup : public SimpleTransform
		{
			inline KeyFrameBackup(unsigned short _index, Vec3 const & _translate, Quat const & _rotation, Vec3 const & _scale);
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
		bool							handleMotion;
		bool							handleRotation;
		bool							extractMotion;
		bool							extractRotation;
		TransformRegistry				initialStateTransforms;
		KeyFrameBackupRegistry			backuppedKeyframes;
		optr<StringUtil::StringObject>	eventData;				//!< name of set or removed model
		float							speed;					//!< sets the speed of the animation
		//! the live crossfade: the outgoing/incoming clips blend by weight over
		//! blendDuration (@see crossFadeTo). blending is false when idle.
		String							blendFrom;
		String							blendTo;
		float							blendDuration;
		float							blendElapsed;
		bool							blending;
		//! the serialized PLAYBACK STATE: the primary playing clip, its phase in
		//! seconds and whether it loops - kept in step with the live mesh each
		//! tick (@see refreshPrimaryClipState) so a scene saved mid-animation
		//! round-trips. Empty clip = nothing playing (the loaded scene auto-plays
		//! the default). The reflected clip/clipTime/clipLoop/speed/paused
		//! properties drive these off the ONE registry.
		String							primaryClip;
		float							primaryTime;
		bool							primaryLoop;
		//! mirrors the pause state (getWantsUpdates is not const, so the
		//! reflected `paused` getter reads this instead)
		bool							paused;
		//! a loaded playback state waiting to apply once the sibling mesh is
		//! ready - applied on the FIRST runtime tick (never in the editor, which
		//! does not tick animations, so an edit-mode load stays at the bind pose)
		bool							pendingRestore;
		//! the pause state to apply AFTER a pending restore poses the clip (so a
		//! scene saved paused mid-clip resumes at its exact frozen phase)
		bool							restorePaused;
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		AnimationComponent();
		//! destructor
		virtual ~AnimationComponent();

		//! get list of available animation names
		inline StringList const & getAvailableAnimations();
		//! get list of bone names (classic backend only - empty elsewhere)
		inline StringList const & getBoneNames();
		//! get default anim name
		inline String const & getDefaultAnimation();
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
		//! @brief seek a clip to an absolute time in seconds - a phase offset
		//! (e.g. to stagger a crowd of otherwise-identical animations). A thin
		//! pass-through to the sibling mesh's animation surface (the same call
		//! playAnimation makes to rewind to 0); returns false when the clip is
		//! not present.
		bool setAnimationTime(String const & anim, float seconds);
		//! @brief blend from the currently playing clip to another over
		//! durationSeconds (weights ramp outgoing 1->0, incoming 0->1). With
		//! nothing playing or a non-positive duration it switches instantly.
		//! @return false when the target clip does not exist
		bool crossFadeTo(String const & anim, float durationSeconds);
		//! is a crossfade currently blending two clips?
		inline bool isCrossFading();
		//! crossfade progress 0..1 (0 when not blending)
		inline float getCrossFadeProgress();
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
		inline float getSpeed() const;

		//--- SERIALIZED PLAYBACK STATE (reflected clip/clipTime/clipLoop/paused) ---
		//! @brief the primary playing clip's name ("" = nothing playing). The
		//! reflected `clip` property; a scene save records it, a load resumes it.
		inline String const & getPlaybackClip() const;
		//! @brief record the clip to resume on load (arms the pending restore
		//! when non-empty); the live mesh is driven on the first runtime tick
		void setPlaybackClip(String const & clip);
		//! the primary clip's phase in seconds (the reflected `clipTime`)
		inline float getPlaybackTime() const;
		//! record the clip phase to resume on load
		void setPlaybackTime(float seconds);
		//! whether the primary clip loops (the reflected `clipLoop`)
		inline bool getPlaybackLoop() const;
		//! record the primary clip's loop flag to resume on load
		void setPlaybackLoop(bool loop);
		//! the pause state (the reflected `paused`; const mirror of the
		//! wantsUpdates gate pause()/resume() drive)
		inline bool getPaused() const;
		//! set the pause state (deferred past a pending restore so the resumed
		//! clip is posed before it freezes)
		void setPaused(bool paused);
	protected:
		//! component override gets called after the component is attached to a GameObject
		virtual void onAdd();
		//! component override gets called before the component is removed from a GameObject
		virtual void onRemove();
		//--- SERIALIZATION ---
		//! @brief write the reflected playback state (clip/clipTime/clipLoop/
		//! speed/paused) off the ONE property schema, so a scene saved
		//! mid-animation resumes on load (@see loadComponentProperties)
		virtual void save(optr<IArchive> const & ar);
		//! @brief read the reflected playback state; the resume is applied on the
		//! first runtime tick (the editor never ticks, so it stays dormant)
		virtual void load(optr<IArchive> const & ar);
	private:
		//! @brief snapshot the live primary playing clip + its phase into the
		//! serialized playback fields (called every tick from updateAnimations)
		void refreshPrimaryClipState(optr<MeshInstance> const & mesh);
		//! @brief resume the loaded playback state on the sibling mesh (enable
		//! the saved clip at its saved phase/loop); clears the pending flag once
		//! the mesh is ready, leaves it armed to retry otherwise
		void applyPlaybackRestore();
		//! overridable to update the component
		virtual void onUpdateComponent(float deltaTime);

		bool onModelRemoved(Event const & event);
		bool onModelSet(Event const & event);

		void getAnimationsFromModel();
		//! the sibling ModelComponent's mesh instance or NULL
		optr<MeshInstance> getAnimableMesh();
		//! @brief the root-motion backdoor: moves the owning transform by the
		//! motion bone's keyframe track and neutralizes the track in-place
		//! @remarks classic-only by design - a no-op elsewhere
		void handleMotionRotation(String const & animationName, float timeDelta);
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
	inline AnimationComponent::SimpleTransform::SimpleTransform(Vec3 const & _translate, Quat const & _rotation, Vec3 const & _scale)
		: translate(_translate), rotation(_rotation), scale(_scale)
	{
	}
	//---------------------------------------------------------------
	inline AnimationComponent::KeyFrameBackup::KeyFrameBackup(unsigned short _index, Vec3 const & _translate, Quat const & _rotation, Vec3 const & _scale)
		: AnimationComponent::SimpleTransform(_translate, _rotation, _scale), index(_index)
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
	inline bool AnimationComponent::isCrossFading()
	{
		return this->blending;
	}
	//---------------------------------------------------------------
	inline float AnimationComponent::getCrossFadeProgress()
	{
		if(!this->blending || this->blendDuration <= 0.0f)
		{
			return 0.0f;
		}
		float progress = this->blendElapsed / this->blendDuration;
		return progress > 1.0f ? 1.0f : progress;
	}
	//---------------------------------------------------------------
	inline void AnimationComponent::pause()
	{
		this->paused = true;
		this->setWantsUpdates(false);
	}
	//---------------------------------------------------------------
	inline void AnimationComponent::resume()
	{
		this->paused = false;
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
	inline float AnimationComponent::getSpeed() const
	{
		return this->speed;
	}
	//---------------------------------------------------------------
	inline String const & AnimationComponent::getPlaybackClip() const
	{
		return this->primaryClip;
	}
	//---------------------------------------------------------------
	inline float AnimationComponent::getPlaybackTime() const
	{
		return this->primaryTime;
	}
	//---------------------------------------------------------------
	inline bool AnimationComponent::getPlaybackLoop() const
	{
		return this->primaryLoop;
	}
	//---------------------------------------------------------------
	inline bool AnimationComponent::getPaused() const
	{
		return this->paused;
	}
}

#endif //__AnimationComponent_h__30_8_2010__15_58_30__
