/**************************************************************
	created:	2010/08/30 at 11:05
	filename: 	InputManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/

#include "engine_input/InputManager.h"
#include <OISInputManager.h>
#include <OISKeyboard.h>
#include <OISMouse.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_graphic/Engine.h"

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(InputManager, KeyPressedEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, KeyReleasedEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, MousePressedEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, MouseReleasedEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, MouseMovedEvent);

	IMPL_OSINGLETON(InputManager);
	//! hidden inputmanager translates OIS Input to Orkige input
	class InputManagerImpl : public OIS::KeyListener, public OIS::MouseListener
	{
		friend class InputManager;

#if OGRE_PLATFORM == OGRE_PLATFORM_IPHONE
		//typedef OIS::MultiTouch		Mouse;			// multitouch device
		typedef OIS::Mouse			Mouse;			// mouse device
#else
		typedef OIS::Mouse			Mouse;			// mouse device
#endif
		typedef OIS::Keyboard		Keyboard;

		OIS::InputManager	*inputSystem;
		Keyboard			*keyboard;
		Mouse				*mouse;					// mouse device

		Event keyPressedEvent;
		Event keyReleasedEvent;
		Event mousePressedEvent;
		Event mouseReleasedEvent;
		Event mouseMovedEvent;

		optr<KeyEventData> keyData;
		optr<MouseEventData> mouseData;

		InputManagerImpl() 
			: keyPressedEvent(InputManager::KeyPressedEvent),
			keyReleasedEvent(InputManager::KeyReleasedEvent),
			mousePressedEvent(InputManager::MousePressedEvent),
			mouseReleasedEvent(InputManager::MouseReleasedEvent),
			mouseMovedEvent(InputManager::MouseMovedEvent),
			inputSystem(NULL), keyboard(NULL), mouse(NULL)
		{
			this->keyData = onew(new KeyEventData());
			this->mouseData = onew(new MouseEventData());

			this->keyPressedEvent.setData(this->keyData);
			this->keyReleasedEvent.setData(this->keyData);

			this->mousePressedEvent.setData(this->mouseData);
			this->mouseReleasedEvent.setData(this->mouseData);
			this->mouseMovedEvent.setData(this->mouseData);
		}
		inline void oisMouseToOrkige(const OIS::MouseState &state)
		{
			this->mouseData->buttons = state.buttons;
			this->mouseData->relX = state.X.rel;
			this->mouseData->relY = state.Y.rel;
			this->mouseData->relZ = state.Z.rel;
			this->mouseData->absX = state.X.abs;
			this->mouseData->absY = state.Y.abs;
			this->mouseData->absZ = state.Z.abs;
		}
		virtual bool keyPressed( const OIS::KeyEvent &e )
		{
			this->keyData->key = static_cast<KeyEventData::KeyCode>(e.key);
			this->keyData->text = e.text;
			GlobalEventManager::getSingleton().trigger(this->keyPressedEvent);
			return true;
		}
		virtual bool keyReleased( const OIS::KeyEvent &e )
		{
			this->keyData->key = static_cast<KeyEventData::KeyCode>(e.key);
			this->keyData->text = e.text;
			GlobalEventManager::getSingleton().trigger(this->keyReleasedEvent);
			return true;
		}
		virtual bool mouseMoved( const OIS::MouseEvent &e )
		{
			this->oisMouseToOrkige(e.state);
			GlobalEventManager::getSingleton().trigger(this->mouseMovedEvent);
			return true;
		}
		virtual bool mousePressed( const OIS::MouseEvent &e, OIS::MouseButtonID id )
		{
			this->oisMouseToOrkige(e.state);
			this->mouseData->button = static_cast<MouseEventData::MouseButtonID>(id);
			GlobalEventManager::getSingleton().trigger(this->mousePressedEvent);
			return true;
		}
		virtual bool mouseReleased( const OIS::MouseEvent &e, OIS::MouseButtonID id )
		{
			this->oisMouseToOrkige(e.state);
			this->mouseData->button = static_cast<MouseEventData::MouseButtonID>(id);
			GlobalEventManager::getSingleton().trigger(this->mouseReleasedEvent);
			return true;
		}
	};
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	InputManager::InputManager(bool shareMouse) 		
	{
		this->frameListener = GlobalEventManager::getSingleton().bind(Engine::FrameStartedEvent,&InputManager::onFrameStarted,this);
		this->impl = new InputManagerImpl();
		this->sharedMouse = shareMouse;
		this->initialise();
	}
	//---------------------------------------------------------
	InputManager::~InputManager( void ) 
	{
		if( this->impl->inputSystem )
		{
			if( this->impl->keyboard ) 
			{
				this->impl->inputSystem->destroyInputObject( this->impl->keyboard );
				this->impl->keyboard = 0;
			}
			if( this->impl->mouse ) 
			{
				this->impl->inputSystem->destroyInputObject( this->impl->mouse );
				this->impl->mouse = 0;
			}
			this->impl->inputSystem->destroyInputSystem(this->impl->inputSystem);
			this->impl->inputSystem = 0;
		}

		delete this->impl;
		this->impl = NULL;
	}
	//---------------------------------------------------------
	bool InputManager::enable()
	{
		return GlobalEventManager::getSingleton().addListener(this->frameListener,Engine::FrameStartedEvent);
	}
	//---------------------------------------------------------
	bool InputManager::disable()
	{
		return GlobalEventManager::getSingleton().delListener(this->frameListener,Engine::FrameStartedEvent);
	}
	//---------------------------------------------------------
	String const & InputManager::getAsString(KeyEventData::KeyCode kc)
	{
		oAssert(this->impl->keyboard);
		return this->impl->keyboard->getAsString(static_cast<OIS::KeyCode>(kc));
	}
	//---------------------------------------------------------
	bool InputManager::isKeyDown(KeyEventData::KeyCode kc)
	{
		oAssert(this->impl->keyboard);
		return this->impl->keyboard->isKeyDown(static_cast<OIS::KeyCode>(kc));
	}
	//---------------------------------------------------------
	void InputManager::setWindowExtents( int width, int height ) 
	{
		const OIS::MouseState &mouseState = this->impl->mouse->getMouseState();
		mouseState.width  = width;
		mouseState.height = height;
	}
	//---------------------------------------------------------
	optr<MouseEventData> const & InputManager::getMouseData() const
	{
		// Set mouse region (if window resizes, we should alter this to reflect as well)
		const OIS::MouseState &mouseState = this->impl->mouse->getMouseState();
		this->impl->oisMouseToOrkige(mouseState);
		return impl->mouseData;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	bool InputManager::onFrameStarted(Event const & event)
	{
		this->capture();
		return false;
	}
	//---------------------------------------------------------
	void InputManager::initialise() 
	{
		if( ! this->impl->inputSystem )
		{
			// Setup basic variables
			OIS::ParamList paramList;    
			size_t windowHnd = 0;
			std::ostringstream windowHndStr;

			// Get window handle
#if defined OIS_WIN32_PLATFORM
			Engine::getSingleton().getRenderWindow()->getCustomAttribute( "WINDOW", &windowHnd );
#elif defined OIS_LINUX_PLATFORM
			Engine::getSingleton().getRenderWindow()->getCustomAttribute( "GLXWINDOW", &windowHnd );
#endif

			// Fill parameter list
			windowHndStr << (unsigned int) windowHnd;

			// Create inputsystem
			this->impl->inputSystem = OIS::InputManager::createInputSystem( windowHnd);

			// If possible create a buffered keyboard
			this->impl->keyboard = static_cast<OIS::Keyboard*>( this->impl->inputSystem->createInputObject( OIS::OISKeyboard, true ) );
			this->impl->keyboard->setEventCallback( this->impl );

			// If possible create a buffered mouse
			this->impl->mouse = static_cast<OIS::Mouse*>( this->impl->inputSystem->createInputObject( OIS::OISMouse, true ) );
			this->impl->mouse->setEventCallback( this->impl );

			// Get window size
			unsigned int width, height, depth;
			int left, top;
			Engine::getSingleton().getRenderWindow()->getMetrics( width, height, depth, left, top );

			// Set mouse region
			this->setWindowExtents( width, height );
		}
	}
	//---------------------------------------------------------
	void InputManager::capture( void ) 
	{
		if( this->impl->keyboard )
		{
			this->impl->keyboard->capture();
		}
		if( this->impl->mouse )
		{
			this->impl->mouse->capture();
		}
	}
	//---------------------------------------------------------
	OOBJECT_IMPL(InputManager)
		OCONSTRUCTOR1(bool)
		OSINGLETON()
		OFUNC(enable)
		OFUNC(disable)
	OOBJECT_END
}
