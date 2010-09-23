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
#include <OISMultiTouch.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_graphic/Engine.h"
#include "engine_util/StringUtil.h"
#ifdef ORKIGE_IPHONE
#   ifdef __OBJC__
#       import <UIKit/UIKit.h>
#   endif
#endif


#ifdef ORKIGE_IPHONE
@interface OrkigeGestureView : UIView <UIAccelerometerDelegate>
{
	Ogre::Vector3 acc;
}
@end
#endif
	
namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(InputManager, KeyPressedEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, KeyReleasedEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, MousePressedEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, MouseReleasedEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, MouseMovedEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, TouchPressedEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, TouchReleasedEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, TouchMovedEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, TouchCancelledEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, GestureBeganEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, GestureEndedEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, GestureCancelledEvent);
	IMPL_OWNED_EVENTTYPE(InputManager, AccelerationEvent);

	IMPL_OSINGLETON(InputManager);
	//! hidden inputmanager translates OIS Input to Orkige input
	class InputManagerImpl 
		: public Singleton<InputManagerImpl>, public OIS::KeyListener, public OIS::MouseListener, public OIS::MultiTouchListener
	{
		DECL_OSINGLETON(InputManagerImpl)
	public:
		friend class InputManager;


		typedef OIS::MultiTouch		MultiTouch;			// multitouch device
		typedef OIS::Mouse			Mouse;			// mouse device
		typedef OIS::Keyboard		Keyboard;

		Event keyPressedEvent;
		Event keyReleasedEvent;
		Event mousePressedEvent;
		Event mouseReleasedEvent;
		Event mouseMovedEvent;
		Event touchPressedEvent;
		Event touchReleasedEvent;
		Event touchMovedEvent;
		Event touchCancelledEvent;
		Event gestureBeganEvent;
		Event gestureEndedEvent;
		Event gestureCancelledEvent;
		Event accelerationEvent;
		
		
		OIS::InputManager	*inputSystem;
		
		Keyboard			*keyboard;
		Mouse				*mouse;					// mouse device
		MultiTouch			*touch;
#ifdef ORKIGE_IPHONE
		OrkigeGestureView *gestureView;
#endif



		optr<KeyEventData> keyData;
		optr<MouseEventData> mouseData;
		optr<TouchEventData> touchData;
		optr<GestureEventData> gestureData;
		optr<AccelerationEventData> accelerationData;

		InputManagerImpl() 
			: keyPressedEvent(InputManager::KeyPressedEvent),
			keyReleasedEvent(InputManager::KeyReleasedEvent),
			mousePressedEvent(InputManager::MousePressedEvent),
			mouseReleasedEvent(InputManager::MouseReleasedEvent),
			mouseMovedEvent(InputManager::MouseMovedEvent),
			touchPressedEvent(InputManager::TouchPressedEvent),
			touchReleasedEvent(InputManager::TouchReleasedEvent),
			touchMovedEvent(InputManager::TouchMovedEvent),
			touchCancelledEvent(InputManager::TouchCancelledEvent),
			gestureBeganEvent(InputManager::GestureBeganEvent),
			gestureEndedEvent(InputManager::GestureEndedEvent),
			gestureCancelledEvent(InputManager::GestureCancelledEvent),
			accelerationEvent(InputManager::AccelerationEvent),
			inputSystem(NULL), keyboard(NULL), mouse(NULL), touch(NULL)
		{
			this->keyData = onew(new KeyEventData());
			this->mouseData = onew(new MouseEventData());
			this->touchData = onew(new TouchEventData());
			this->gestureData = onew(new GestureEventData());
			this->accelerationData = onew(new AccelerationEventData());

			this->keyPressedEvent.setData(this->keyData);
			this->keyReleasedEvent.setData(this->keyData);

			this->mousePressedEvent.setData(this->mouseData);
			this->mouseReleasedEvent.setData(this->mouseData);
			this->mouseMovedEvent.setData(this->mouseData);

			this->touchPressedEvent.setData(this->touchData);
			this->touchReleasedEvent.setData(this->touchData);
			this->touchMovedEvent.setData(this->touchData);
			this->touchCancelledEvent.setData(this->touchData);

			this->gestureBeganEvent.setData(this->gestureData);
			this->gestureEndedEvent.setData(this->gestureData);
			this->gestureCancelledEvent.setData(this->gestureData);

			this->accelerationEvent.setData((this->accelerationData));
#ifdef ORKIGE_IPHONE
			gestureView = 0;
#endif
		}
		~InputManagerImpl()
		{
#ifdef ORKIGE_IPHONE
			[gestureView release];
#endif
		}
		template<typename EventDataType>
		inline void transformInputToOrientation(EventDataType & data)
		{
			Ogre::Viewport * viewport = Engine::getSingleton().getViewort();
			oAssert(viewport);


			int w = viewport->getActualWidth();
			int h = viewport->getActualHeight();
			int absX = data->absX;
			int absY = data->absY;
			int relX = data->relX;
			int relY = data->relY;

			switch (viewport->getOrientationMode())
			{
			case Ogre::OR_DEGREE_0:   //OR_PORTRAIT
				break;
			case Ogre::OR_DEGREE_90:  //OR_LANDSCAPERIGHT
				data->absX = w - absY;
				data->absY = absX;
				data->relX = -relY;
				data->relY = relX;
				break;
			case Ogre::OR_DEGREE_180:
				data->absX = w - absX;
				data->absY = h - absY;
				data->relX = -relX;
				data->relY = -relY;
				break;
			case Ogre::OR_DEGREE_270: //OR_LANDSCAPELEFT
				data->absX = absY;
				data->absY = absX;
				data->relX = relY;
				data->relY = -relX;
				break;
			}
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
		inline void oisMouseToOrkige(const OIS::MultiTouchState &state)
		{
			this->mouseData->relX = state.X.rel;
			this->mouseData->relY = state.Y.rel;
			this->mouseData->relZ = state.Z.rel;
			this->mouseData->absX = state.X.abs;
			this->mouseData->absY = state.Y.abs;
			this->mouseData->absZ = state.Z.abs;
			this->transformInputToOrientation(this->mouseData);
		}
		inline void oisMultiTouchToOrkige(const OIS::MultiTouchState &state)
		{
			this->touchData->relX = state.X.rel;
			this->touchData->relY = state.Y.rel;
			this->touchData->relZ = state.Z.rel;
			this->touchData->absX = state.X.abs;
			this->touchData->absY = state.Y.abs;
			this->touchData->absZ = state.Z.abs;
			this->transformInputToOrientation(this->touchData);
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
		virtual bool touchMoved( const OIS::MultiTouchEvent &e )
		{
#ifdef ORKIGE_MULTITOUCH_TO_MOUSE
			if(this->mouseData->buttonDown(MouseEventData::MB_Left))
			{
				this->oisMouseToOrkige(e.state);
				GlobalEventManager::getSingleton().trigger(this->mouseMovedEvent);
			}		
#endif
			this->oisMultiTouchToOrkige(e.state);
			GlobalEventManager::getSingleton().trigger(this->touchReleasedEvent);
			return true;
		}
		virtual bool touchPressed( const OIS::MultiTouchEvent &e )
		{
#ifdef ORKIGE_MULTITOUCH_TO_MOUSE
			if(!this->mouseData->buttonDown(MouseEventData::MB_Left))
			{
				this->oisMouseToOrkige(e.state);
				this->mouseData->button = MouseEventData::MB_Left;
				this->mouseData->buttons |= 1 << MouseEventData::MB_Left; //turn the bit flag on
				GlobalEventManager::getSingleton().trigger(this->mousePressedEvent);
			}
#endif
			this->oisMultiTouchToOrkige(e.state);
			GlobalEventManager::getSingleton().trigger(this->touchReleasedEvent);
			return true;
		}
		virtual bool touchReleased( const OIS::MultiTouchEvent &e )
		{
#ifdef ORKIGE_MULTITOUCH_TO_MOUSE
			if(this->mouseData->buttonDown(MouseEventData::MB_Left))
			{
				this->oisMouseToOrkige(e.state);
				this->mouseData->button = MouseEventData::MB_Left;
				this->mouseData->buttons &= ~(1 << MouseEventData::MB_Left); //turn the bit flag off
				GlobalEventManager::getSingleton().trigger(this->mouseReleasedEvent);
			}
#endif
			this->oisMultiTouchToOrkige(e.state);
			GlobalEventManager::getSingleton().trigger(this->touchReleasedEvent);
			return true;
		}
		virtual bool touchCancelled( const OIS::MultiTouchEvent &e )
		{
			this->oisMultiTouchToOrkige(e.state);
			GlobalEventManager::getSingleton().trigger(this->touchCancelledEvent);
			return true;
		}
		inline void motionBegan(GestureEventData::GestureType type)
		{
			this->gestureData->type = type;
			GlobalEventManager::getSingleton().trigger(this->gestureBeganEvent);
		}
		inline void motionEnded(GestureEventData::GestureType type)
		{
			this->gestureData->type = type;
			GlobalEventManager::getSingleton().trigger(this->gestureEndedEvent);
		}
		inline void motionCancelled(GestureEventData::GestureType type)
		{
			this->gestureData->type = type;
			GlobalEventManager::getSingleton().trigger(this->gestureCancelledEvent);
		}
		inline void didAccelerate(Ogre::Vector3 const & acceleration)
		{
			this->accelerationData->relX = acceleration.x - this->accelerationData->absX;
			this->accelerationData->relY = acceleration.y - this->accelerationData->absY;
			this->accelerationData->relZ = acceleration.z - this->accelerationData->absZ;
			this->accelerationData->absX = acceleration.x;
			this->accelerationData->absY = acceleration.y;
			this->accelerationData->absZ = acceleration.z;
			GlobalEventManager::getSingleton().trigger(this->accelerationEvent);
		}
	};
	IMPL_OSINGLETON(InputManagerImpl);

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
			if( this->impl->touch ) 
			{
				this->impl->inputSystem->destroyInputObject( this->impl->touch );
				this->impl->touch = 0;
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
		if(this->impl->keyboard)
		{
			return this->impl->keyboard->getAsString(static_cast<OIS::KeyCode>(kc));
		}
		return StringUtil::BLANK;
	}
	//---------------------------------------------------------
	bool InputManager::isKeyDown(KeyEventData::KeyCode kc)
	{
		if(this->impl->keyboard)
		{
			return this->impl->keyboard->isKeyDown(static_cast<OIS::KeyCode>(kc));
		}
		return false;
	}
	//---------------------------------------------------------
	void InputManager::setWindowExtents( int width, int height ) 
	{
		if(this->impl->mouse)
		{
			const OIS::MouseState &mouseState = this->impl->mouse->getMouseState();
			mouseState.width  = width;
			mouseState.height = height;
		}
	}
	//---------------------------------------------------------
	optr<MouseEventData> const & InputManager::getMouseData() const
	{
		if(this->impl->mouse)
		{
			// Set mouse region (if window resizes, we should alter this to reflect as well)
			const OIS::MouseState &mouseState = this->impl->mouse->getMouseState();
			this->impl->oisMouseToOrkige(mouseState);
		}
		return impl->mouseData;
	}
	//---------------------------------------------------------
	optr<TouchEventData> const & InputManager::getLastTouchData() const
	{
		return impl->touchData;
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

			// Get window handle	
#if defined OIS_LINUX_PLATFORM
			Engine::getSingleton().getRenderWindow()->getCustomAttribute( "GLXWINDOW", &windowHnd );
#else
			Engine::getSingleton().getRenderWindow()->getCustomAttribute( "WINDOW", &windowHnd );
#endif


			paramList.insert(std::make_pair("WINDOW", StringUtil::Converter::toString(windowHnd)));

			// Create inputsystem
			this->impl->inputSystem = OIS::InputManager::createInputSystem( paramList );

#ifndef ORKIGE_IPHONE
			// If possible create a buffered keyboard
			this->impl->keyboard = static_cast<OIS::Keyboard*>( this->impl->inputSystem->createInputObject( OIS::OISKeyboard, true ) );
			this->impl->keyboard->setEventCallback( this->impl );

			// If possible create a buffered mouse
			this->impl->mouse = static_cast<OIS::Mouse*>( this->impl->inputSystem->createInputObject( OIS::OISMouse, true ) );
			this->impl->mouse->setEventCallback( this->impl );
#else
			// If possible create a buffered mouse
			this->impl->touch = static_cast<OIS::MultiTouch*>( this->impl->inputSystem->createInputObject( OIS::OISMultiTouch, true ) );
			this->impl->touch->setEventCallback( this->impl );

			this->impl->gestureView = [[OrkigeGestureView alloc] init];
			[[[UIApplication sharedApplication] keyWindow] addSubview:this->impl->gestureView];
			[[UIAccelerometer sharedAccelerometer] setUpdateInterval:(1.0 / 30)];
			[[UIAccelerometer sharedAccelerometer] setDelegate:this->impl->gestureView];
			//this->impl->gestureView.inputManager = impl;
			//this->impl->gestureView.acc = Ogre::Vector3::ZERO;
#endif
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
		if( this->impl->touch )
		{
			this->impl->touch->capture();
		}
#ifdef ORKIGE_IPHONE
		[this->impl->gestureView becomeFirstResponder];
#endif
	}
	//---------------------------------------------------------
	OOBJECT_IMPL(InputManager)
		OCONSTRUCTOR1(bool)
		OSINGLETON()
		OFUNC(enable)
		OFUNC(disable)
	OOBJECT_END
}


#ifdef ORKIGE_IPHONE
@implementation OrkigeGestureView

- (BOOL)canBecomeFirstResponder
{
	return YES;
}

- (void)dealloc 
{
	[super dealloc];
}

- (void)motionBegan:(UIEventSubtype)motion withEvent:(UIEvent *)event 
{
	if(event.type == UIEventTypeMotion && event.subtype == UIEventSubtypeMotionShake)
		Orkige::InputManagerImpl::getSingleton().motionBegan(Orkige::GestureEventData::GT_Shake);
		
		if ([super respondsToSelector:@selector(motionBegan:withEvent:)]) 
		{
			[super motionBegan:motion withEvent:event];
		}
}

- (void)motionEnded:(UIEventSubtype)motion withEvent:(UIEvent *)event 
{
	if(event.type == UIEventTypeMotion && event.subtype == UIEventSubtypeMotionShake)
		Orkige::InputManagerImpl::getSingleton().motionEnded(Orkige::GestureEventData::GT_Shake);
		
		if ([super respondsToSelector:@selector(motionEnded:withEvent:)]) 
		{
			[super motionEnded:motion withEvent:event];
		}
}

- (void)motionCancelled:(UIEventSubtype)motion withEvent:(UIEvent *)event 
{
	if(event.type == UIEventTypeMotion && event.subtype == UIEventSubtypeMotionShake)
		Orkige::InputManagerImpl::getSingleton().motionCancelled(Orkige::GestureEventData::GT_Shake);
		
		if ([super respondsToSelector:@selector(motionCancelled:withEvent:)]) 
		{
			[super motionCancelled:motion withEvent:event];
		}
}

#pragma mark Accelerator Event Handling
- (void)accelerometer:(UIAccelerometer *)accelerometer didAccelerate:(UIAcceleration *)acceleration 
{
	acc.x = acceleration.x;
	acc.y = acceleration.y;
	acc.z = acceleration.z;
	Orkige::InputManagerImpl::getSingleton().didAccelerate(acc);
}

@end
#endif

