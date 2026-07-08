/********************************************************************
created:	Friday 2010/09/10 at 12:25
filename: 	ConsoleCommands.h
company:	kunst-stoff GmbH
author:		steffen.roemer

purpose:	
*********************************************************************/

#ifndef __KS__CONSOLECOMMANDS__h__
#define __KS__CONSOLECOMMANDS__h__

#include <engine_graphic/MovableText.h>
#include <engine_graphic/IngameConsole.h>
#include <engine_util/NodeUtil.h>
#include <engine_fastgui/FastGuiManager.h>
#include <core_game/GameStateManager.h>

namespace KS
{
	void hello_world(Orkige::StringVector const & params)
	{
		Orkige::IngameConsole::getSingleton().print("Hello World!");
	}

	void clearui(Orkige::StringVector const & params)
	{
		Orkige::FastGuiManager::getSingleton().getView("background").lock()->getScreen()->setVisible(false);
		Orkige::FastGuiManager::getSingleton().getView("fastgui_default").lock()->getScreen()->setVisible(false);
		Orkige::FastGuiManager::getSingleton().hideCursor();
		Orkige::FastGuiManager::getSingleton().hideStats();
	}

	void wireframe(Orkige::StringVector const & params)
	{
		if(params.size() == 2 && params[1] == "on")
		{
			Orkige::Engine::getSingleton().enableWireframeMode();
		}
		else
		{
			Orkige::Engine::getSingleton().disableWireframeMode();
		}
		
	}

	void print_types(Orkige::StringVector const & params)
	{
		Orkige::String types = "Known types: ";
		foreach(Orkige::TypeManager::value_type const & vt, Orkige::TypeManager::getSingleton())
		{
			types += vt.first;
			types += " ";
		}
		Orkige::IngameConsole::getSingleton().print(types);
	}

	static inline void addTextSceneNode(Ogre::SceneNode* sceneNode)
	{
		std::vector<Orkige::String> names;
		for(unsigned short i = 0; i < sceneNode->numAttachedObjects(); i++ )
		{
			try
			{
				Ogre::MovableObject* mo = sceneNode->getAttachedObject(i);
				Orkige::String name = mo->getName();
				if(!name.empty())
				{
					names.push_back(name);
				}
			}
			catch (...)
			{

			}
		}

		foreach(Orkige::String const & name, names)
		{
			Orkige::MovableText* msg = new Orkige::MovableText("TXT_" + name, name);
			msg->setTextAlignment(Orkige::MovableText::H_CENTER, Orkige::MovableText::V_ABOVE); // Center horizontally and display above the node
			msg->setAdditionalHeight( 0.f ); //msg->setAdditionalHeight( ei.getRadius() )
			sceneNode->attachObject(msg);
		}

		Ogre::Node::ChildNodeIterator it = sceneNode->getChildIterator();

		while(it.hasMoreElements())
		{
			Ogre::SceneNode* sn = static_cast<Ogre::SceneNode*>(it.peekNextValue());
			if(sn)
				addTextSceneNode(sn);

			it.moveNext();
		}
	}

	static inline void removeTextSceneNode(Ogre::SceneNode* sceneNode)
	{
		std::vector<Ogre::MovableObject*> textsToRemove;
		for(unsigned short i = 0; i < sceneNode->numAttachedObjects(); i++)
		{
			try
			{
				Ogre::MovableObject* mo = sceneNode->getAttachedObject(i);
				if(mo->getMovableType() == "MovableText")
				{
					textsToRemove.push_back(mo);
				}
			}
			catch (...)
			{

			}

		}

		for(std::size_t i = 0; i < textsToRemove.size(); i++)
		{
			Ogre::MovableObject* mo = textsToRemove[i];
			sceneNode->detachObject(mo);
			delete mo;
		}

		Ogre::Node::ChildNodeIterator it = sceneNode->getChildIterator();

		while(it.hasMoreElements())
		{
			Ogre::SceneNode* sn = static_cast<Ogre::SceneNode*>(it.peekNextValue());
			if(sn)
				removeTextSceneNode(sn);

			it.moveNext();
		}
	}

	void show_nodenames(Orkige::StringVector const & params)
	{
		if(params.size() == 2 && params[1] == "show")
		{
			removeTextSceneNode(Orkige::Engine::getSingleton().getSceneManager()->getRootSceneNode());
			addTextSceneNode(Orkige::Engine::getSingleton().getSceneManager()->getRootSceneNode());
		}
		else if(params.size() == 2 && params[1] == "hide")
		{
			removeTextSceneNode(Orkige::Engine::getSingleton().getSceneManager()->getRootSceneNode());
		}
		else
		{
			Orkige::IngameConsole::getSingleton().print("Wrong parameter use \"show\" or \"hide\"");
		}

	}
	struct FreeCamHandler : public Orkige::EventHandler
	{
		bool isOn;
		Ogre::SceneNode* oldCamAttach;
		Ogre::SceneNode* freeCamAttach;
		Ogre::Vector3 startPos;
		Ogre::Quaternion startOrient;
		FreeCamHandler() : isOn(false), oldCamAttach(NULL), freeCamAttach(NULL) {}
		bool on()
		{
			Ogre::Camera* cam = Orkige::Engine::getSingleton().getCamera();
			if(this->isOn && this->freeCamAttach != cam->getParentSceneNode())
			{
				cam->detachFromParent();
				this->freeCamAttach->attachObject(cam);
				this->freeCamAttach->resetToInitialState();
				return true;
			}
			if(this->isOn) return false;
			this->freeCamAttach = Orkige::Engine::getSingleton().getSceneManager()->createSceneNode("FreeCameraAttach");
			this->isOn = true;
			this->registerEvent(Orkige::Engine::FrameStartedEvent, &FreeCamHandler::onFrameStarted, this);
			this->registerEvent(Orkige::GameStateManager::GameStateChangedEvent, &FreeCamHandler::onGameStateChanged, this);

			this->startPos = cam->getPosition();
			this->startOrient = cam->getOrientation();
			this->oldCamAttach = cam->getParentSceneNode();
			cam->detachFromParent();
			this->freeCamAttach->attachObject(cam);
			return true;
		}
		bool off()
		{
			if(!this->isOn) return false;
			this->unregisterEvent(Orkige::Engine::FrameStartedEvent);
			this->unregisterEvent(Orkige::GameStateManager::GameStateChangedEvent);
			
			Ogre::Camera* cam = Orkige::Engine::getSingleton().getCamera();
			cam->detachFromParent();
			Orkige::NodeUtil::wipeSceneNode(this->freeCamAttach);
			cam->setPosition(this->startPos);
			cam->setOrientation(this->startOrient);
			if(this->oldCamAttach)
				this->oldCamAttach->attachObject(cam);
			this->isOn = false;
			return true;
		}
		bool onFrameStarted(Orkige::Event const & event)
		{
			if(this->isOn)
			{
				optr<Orkige::FrameEventData> data = event.getDataPtr<Orkige::FrameEventData>();
				float timeSinceLastFrame = data->timeSinceLastFrame;

				Ogre::Real speed = 5;
				Ogre::Vector3 trans, strafe, vec;
				Ogre::Quaternion quat;

				quat = this->freeCamAttach->getOrientation();

				vec = Ogre::Vector3(0.0,0.0,-1);
				trans = quat * vec;

				vec = Ogre::Vector3(1,0.0,0.0);
				strafe = quat * vec;

				this->freeCamAttach->pitch( Ogre::Degree(Orkige::InputManager::getSingleton().getMouseData()->relY * -speed) * timeSinceLastFrame );
				this->freeCamAttach->yaw( Ogre::Degree(Orkige::InputManager::getSingleton().getMouseData()->relX * -speed) * timeSinceLastFrame, Ogre::SceneNode::TS_WORLD );


				if (Orkige::InputManager::getSingleton().isKeyDown(Orkige::KeyEventData::KC_UP) || Orkige::InputManager::getSingleton().isKeyDown(Orkige::KeyEventData::KC_W))
					this->freeCamAttach->translate(trans * timeSinceLastFrame * speed*50);

				if (Orkige::InputManager::getSingleton().isKeyDown(Orkige::KeyEventData::KC_DOWN) || Orkige::InputManager::getSingleton().isKeyDown(Orkige::KeyEventData::KC_S))
					this->freeCamAttach->translate((trans * -1) * timeSinceLastFrame * speed*50);

				if (Orkige::InputManager::getSingleton().isKeyDown(Orkige::KeyEventData::KC_LEFT) || Orkige::InputManager::getSingleton().isKeyDown(Orkige::KeyEventData::KC_A))
					this->freeCamAttach->translate((strafe * -1) * timeSinceLastFrame * speed*50);

				if (Orkige::InputManager::getSingleton().isKeyDown(Orkige::KeyEventData::KC_RIGHT) || Orkige::InputManager::getSingleton().isKeyDown(Orkige::KeyEventData::KC_D))
					this->freeCamAttach->translate(strafe * timeSinceLastFrame * speed*50);
			}
			return false;
		}
		bool onGameStateChanged(Orkige::Event const & event)
		{
			this->oldCamAttach = NULL;
			this->off();
			return true;
		}
	};
	
	void free_camera(Orkige::StringVector const & params)
	{
		static FreeCamHandler freeCamHandler;
		if(params.size() == 2 && params[1] == "on")
		{
			if(freeCamHandler.on())
			{
				Orkige::IngameConsole::getSingleton().print("FreeCam activated!");
			}
			else
			{
				Orkige::IngameConsole::getSingleton().print("FreeCam already active!");
			}
		}
		else if(params.size() == 2 && params[1] == "off")
		{
			if(freeCamHandler.off())
			{
				Orkige::IngameConsole::getSingleton().print("FreeCam deactivated!");
			}
			else
			{
				Orkige::IngameConsole::getSingleton().print("FreeCam wasn't active!");
			}
		}
		else
		{
			Orkige::IngameConsole::getSingleton().print("Wrong parameter use \"on\" or \"off\"");
		}

	}

	void initConsoleCommands()
	{
		Orkige::IngameConsole::getSingleton().addCommand("wireframe", wireframe);
		Orkige::IngameConsole::getSingleton().addCommand("hello", hello_world);
		Orkige::IngameConsole::getSingleton().addCommand("types", print_types);
		Orkige::IngameConsole::getSingleton().addCommand("names", show_nodenames);
		Orkige::IngameConsole::getSingleton().addCommand("freecam", free_camera);
		Orkige::IngameConsole::getSingleton().addCommand("clearui", clearui);
	}
	//----------------------------------------------------
}
#endif //__KS__CONSOLECOMMANDS__h__