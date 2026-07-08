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
                this->setWantsUpdates(true);
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
                        // the source follows the transform via onUpdateComponent; no3D
                        // sources simply never move (the historical OgreOggSound-era
                        // attachObject path is gone with the facade node reshape)
                        Orkige::SoundSourcePtr sound = SoundManager::getSingleton().createSound( id, fileName, loop, transformComponent->getPosition() );
                        this->attachedSoundObjects[id] = sound;
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
                Orkige::SoundSourcePtr source = it->second;
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
                Orkige::SoundSourcePtr source = it->second;
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

                foreach(SoundSourceMap::value_type const & vt, this->attachedSoundObjects)
                {
                        //SoundManager::getSingleton().destroySound(vt.first);
                        Orkige::SoundSourcePtr source = vt.second;
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
                if(!this->attachedSoundObjects.empty())
                {
                        GameObject* componentOwner = this->getComponentOwner();
                        oAssert(componentOwner);
                        optr<TransformComponent> transformComponent = componentOwner->getComponent<TransformComponent>().lock();
                        oAssert(transformComponent);
                        std::vector<SoundSourceMap::iterator> sourcesToDelete;
                        for(SoundSourceMap::iterator it = this->attachedSoundObjects.begin(), itend = this->attachedSoundObjects.end(); it != itend; ++it)
                        {
                                Orkige::SoundSourcePtr source = it->second;

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
        }
        //---------------------------------------------------------
        // @TODO(scene format v2): serialize the attached sound names - until
        // then a saved scene only restores an empty SoundComponent
        void SoundComponent::save(optr<IArchive> const & ar)
        {
                OParent::save(ar);
                oDebugMsg("scene",0,"SoundComponent: attached sounds are not serialized yet");
        }
        //---------------------------------------------------------
        void SoundComponent::load(optr<IArchive> const & ar)
        {
                OParent::load(ar);
        }
        //---------------------------------------------------------
        //--- private: --------------------------------------------
        //---------------------------------------------------------
        OOBJECT_IMPL(SoundComponent)
                GAMEOBJECTCOMPONENT()
        OOBJECT_END
}
