/********************************************************************
	created:	Monday 2010/09/06 at 12:25
	filename: 	SoundComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/

#include "engine_gocomponent/SoundComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include <core_game/GameObject.h>
#include "engine_graphic/Engine.h"
#include "engine_sound/SoundManager.h"
#include "engine_sound/SoundSource.h"

#include "engine_module/EnginePrerequisites.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	SoundComponent::SoundComponent() 
	{
		this->addDependency<TransformComponent>();
#ifndef ORKIGE_OGGSOUNDMANAGER
		this->setWantsUpdates(true);
#endif
	}
	//---------------------------------------------------------
	SoundComponent::~SoundComponent()
	{
	}
	//---------------------------------------------------------
	bool SoundComponent::addSound(String const & id, String const & fileName, bool loop,bool no3D)
	{
		if ( !SoundManager::getSingleton().isinitialised())
		{
			return false;
		}
		if(this->attachedSoundObjects.find(id) == this->attachedSoundObjects.end())
		{
			GameObject* componentOwner = this->getComponentOwner();
			oAssert(componentOwner);
			optr<TransformComponent> transformComponent = componentOwner->getComponent<TransformComponent>().lock();
			oAssert(transformComponent);
			Orkige::SoundSourcePtr sound = SoundManager::getSingleton().createSound( id, fileName, loop, transformComponent->getPosition() );
			sound->setRolloffFactor(20.0);
			//sound->setVolume(50);
			sound->setReferenceDistance(200);
			sound->setMaxDistance(250);
			

			sound->disable3D(no3D);
			this->attachedSoundObjects[id] = sound;
			if (!no3D)
			{
#ifdef ORKIGE_OGGSOUNDMANAGER
				transformComponent->attachObject(sound);
#endif
			}
			return true;
		}
		return false;
	}
	//---------------------------------------------------------
	bool SoundComponent::play(String const & id)
	{
		if ( !SoundManager::getSingleton().isinitialised())
		{
			return false;
		}
		SoundSourceMap::iterator it = this->attachedSoundObjects.find(id);
		if(it == this->attachedSoundObjects.end())
		{
			return false;
		}
#ifdef ORKIGE_OGGSOUNDMANAGER
		Orkige::SoundSourcePtr source = it->second;
#else
		optr<SoundSource> source = it->second.lock();
#endif
		if(!source)
		{
			return false;
		}
		source->play();
		return source->isPlaying();
	}
	//---------------------------------------------------------
	bool SoundComponent::stop(String const & id)
	{
		if ( !SoundManager::getSingleton().isinitialised())
		{
			return false;
		}
		SoundSourceMap::iterator it = this->attachedSoundObjects.find(id);
		if(it == this->attachedSoundObjects.end())
		{
			return false;
		}
#ifdef ORKIGE_OGGSOUNDMANAGER
		Orkige::SoundSourcePtr source = it->second;
#else
		optr<SoundSource> source = it->second.lock();
#endif
		if(!source)
		{
			return false;
		}
		source->stop();
		return !source->isPlaying();
	}
	//---------------------------------------------------------
	bool SoundComponent::stopAllSounds()
	{
		if ( !SoundManager::getSingleton().isinitialised())
		{
			return false;
		}
		SoundSourceMap::iterator it;
		SoundSourceMap::iterator itEnd = this->attachedSoundObjects.end();
		foreach(SoundSourceMap::value_type const & vt, this->attachedSoundObjects)
		{
			//SoundManager::getSingleton().destroySound(vt.first);
#ifdef ORKIGE_OGGSOUNDMANAGER
			Orkige::SoundSourcePtr source = vt.second;
#else
			optr<SoundSource> source = vt.second.lock();
#endif
			if(!source)
			{
				return false;
			}
			source->stop();

		}

		return false;
	}

	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void SoundComponent::onAdd()
	{

	}
	//---------------------------------------------------------
	void SoundComponent::onRemove()
	{
		this->unRegisterAllEvents();
		foreach(SoundSourceMap::value_type const & vt, this->attachedSoundObjects)
		{
			SoundManager::getSingleton().destroySound(vt.first);
		}
		this->attachedSoundObjects.clear();
	}
	//---------------------------------------------------------
	void SoundComponent::onUpdateComponent(float deltaTime)
	{
		if ( !SoundManager::getSingleton().isinitialised())
		{
			return ;
		}
#ifndef ORKIGE_OGGSOUNDMANAGER
		if(!this->attachedSoundObjects.empty())
		{
			GameObject* componentOwner = this->getComponentOwner();
			oAssert(componentOwner);
			optr<TransformComponent> transformComponent = componentOwner->getComponent<TransformComponent>().lock();
			oAssert(transformComponent);
			std::vector<SoundSourceMap::iterator> sourcesToDelete;
			for(SoundSourceMap::iterator it = this->attachedSoundObjects.begin(), itend = this->attachedSoundObjects.end(); it != itend; ++it)
			{
				optr<SoundSource> source = it->second.lock();

				if(source)
				{
					if(source->isPlaying())
					{
						source->setPosition(transformComponent->getPosition());
					}
				}
				else
				{
					sourcesToDelete.push_back(it);
				}
			}
			if(!sourcesToDelete.empty())
			{
				foreach(SoundSourceMap::iterator const & it, sourcesToDelete)
				{
					this->attachedSoundObjects.erase(it);
				}
			}
		}
#endif //ORKIGE_OGGSOUNDMANAGER
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(SoundComponent)
		GAMEOBJECTCOMPONENT()
	OOBJECT_END
}
