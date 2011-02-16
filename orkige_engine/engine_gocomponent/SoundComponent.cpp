/********************************************************************
	created:	Monday 2010/09/06 at 12:25
	filename: 	SoundComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
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
	}
	//---------------------------------------------------------
	SoundComponent::~SoundComponent()
	{
	}
	//---------------------------------------------------------
	bool SoundComponent::addSound(String const & id, String const & fileName, bool loop)
	{
		if(this->attachedSoundObjects.find(id) == this->attachedSoundObjects.end())
		{
			GameObject* componentOwner = this->getComponentOwner();
			oAssert(componentOwner);
			optr<TransformComponent> transformComponent = componentOwner->getComponent<TransformComponent>().lock();
			oAssert(transformComponent);
			Orkige::SoundSourcePtr sound = SoundManager::getSingleton().createSound( id ,fileName, loop, transformComponent->getPosition() );
			sound->setRolloffFactor(0.01f);
			this->attachedSoundObjects[id] = sound;
#ifdef ORKIGE_OGGSOUNDMANAGER
			transformComponent->attachObject(sound);
#endif
			return true;
		}
		return false;
	}
	//---------------------------------------------------------
	bool SoundComponent::play(String const & id)
	{
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
#ifndef ORKIGE_OGGSOUNDMANAGER
		this->registerEvent(Engine::FrameStartedEvent, &SoundComponent::onFrameStarted, this);
#endif
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
	bool SoundComponent::onFrameStarted(Event const & event)
	{
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
		return false;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(SoundComponent)
		GAMEOBJECTCOMPONENT()
	OOBJECT_END
}
