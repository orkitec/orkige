/********************************************************************
	created:	Monday 2010/09/06 at 12:20
	filename: 	SoundComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
#ifndef __SoundComponent_h__6_9_2010__12_20_07__
#define __SoundComponent_h__6_9_2010__12_20_07__

#include <core_game/GameObjectComponent.h>
#include "engine_sound/SoundSource.h"

namespace Orkige
{
	//! handles attached SoundSource's to GameObject's
	class ORKIGE_ENGINE_DLL SoundComponent : public GameObjectComponent
	{
		OOBJECT(SoundComponent,GameObjectComponent)	
		//--- Types -------------------------------------------------
	public:
	protected:
		typedef std::map<String, Orkige::SoundSourcePtr > SoundSourceMap;	//!< registry type for attched SoundSource's
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		SoundSourceMap attachedSoundObjects;							//!< attached SoundSource's
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		SoundComponent();
		//! destructor
		virtual ~SoundComponent();
		//! adds (and creates if needed) a SoundSource
		bool addSound(String const & id, String const & fileName, bool loop = false,bool no3D = false);
		//! play a attached sound
		bool play(String const & sid);
		//! stop a attached sound
		bool stop(String const & sid);
		//! stop all sounds from this component
		bool stopAllSounds();
		//--- MIXER (per attached sound; effective gain = volume * group
		//--- volume, master on top - see engine_sound/SoundManager.h) ---
		//! set an attached sound's own volume (0..1 - gains above 1 would
		//! clamp silently at the pinned AL_MAX_GAIN)
		bool setVolume(String const & sid, float volume);
		//! an attached sound's own volume (1 when the sound is unknown)
		float getVolume(String const & sid);
		//! move an attached sound into a mixer group (default "sfx")
		bool setGroup(String const & sid, String const & group);
		//! an attached sound's mixer group ("" when the sound is unknown)
		String getGroup(String const & sid);
	protected:
		//! component override gets called after the component is attached to a GameObject
		virtual void onAdd();
		//! component override gets called before the component is removed from a GameObject
		virtual void onRemove();
		//! overridable to update the component
		virtual void onUpdateComponent(float deltaTime);
		//! deactivated GameObjects stop all their sounds (updates are gated
		//! centrally, so nothing restarts them until reactivation plays again)
		virtual void onSetActive(bool activeInHierarchy);
		//--- SERIALIZATION ---
		//! @warning attached sounds do not round-trip yet (logs a warning)
		virtual void save(optr<IArchive> const & ar);
		//! @warning attached sounds do not round-trip yet
		virtual void load(optr<IArchive> const & ar);
	private:
	};
	//---------------------------------------------------------------
}

#endif //__SoundComponent_h__6_9_2010__12_20_07__
