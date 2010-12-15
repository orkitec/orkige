/********************************************************************
	created:	Monday 2010/09/06 at 12:20
	filename: 	SoundComponent.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
*********************************************************************/
#ifndef __SoundComponent_h__6_9_2010__12_20_07__
#define __SoundComponent_h__6_9_2010__12_20_07__

#include <core_game/GameObjectComponent.h>
#include "engine_sound/SoundSource.h"

namespace Orkige
{
	//! handles attached SoundSource's to GameObject's
	class ORKIGE_DLL SoundComponent : public GameObjectComponent
	{
		OOBJECT(SoundComponent,GameObjectComponent)	
		//--- Types -------------------------------------------------
	public:
	protected:
		typedef std::map<String, woptr<SoundSource> > SoundSourceMap;	//!< registry type for attched SoundSource's
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
		bool addSound(String const & id, String const & fileName, bool loop = false);
		//! play a attached sound
		bool play(String const & sid);
		//! stop a attached sound
		bool stop(String const & sid);
	protected:
		//! component override gets called after the component is attached to a GameObject
		virtual void onAdd();
		//! component override gets called before the component is removed from a GameObject
		virtual void onRemove();
		//! updates sound positions
		bool onFrameStarted(Event const & event);
	private:
	};
	//---------------------------------------------------------------
}

#endif //__SoundComponent_h__6_9_2010__12_20_07__
