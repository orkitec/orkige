/**************************************************************
	created:	2010/08/30 at 11:05
	filename: 	InputManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/

#include "engine_input/InputManager.h"
#include <OISInputManager.h>
#include <OISKeyboard.h>
#include <OISMouse.h>
#include <OISMultiTouch.h>
#include <OISException.h>
#include "engine_module/EnginePrerequisites.h"
#include "engine_graphic/Engine.h"
#include "engine_util/StringUtil.h"


#ifdef ORKIGE_ENABLE_TUIO
	#include "tuio/tuio/TuioListener.h"
	#include "tuio/tuio/TuioClient.h"
	#include "tuio/tuio/TuioObject.h"
	#include "tuio/tuio/TuioCursor.h"
	#include "tuio/tuio/TuioPoint.h"
	#include "tuio/tuio/TuioContainer.h"
#endif

#ifdef ORKIGE_IPHONE
#   ifdef __OBJC__
#       import <UIKit/UIKit.h>
#   endif
#include <iphone/iPhoneInputManager.h>
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
#ifdef ORKIGE_ENABLE_TUIO
		, public TUIO::TuioListener
#endif
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
#ifdef ORKIGE_ENABLE_TUIO
		TUIO::TuioClient *tuioClient;
#endif
		
#ifdef ORKIGE_IPHONE
		OrkigeGestureView *gestureView;
#endif



		optr<KeyEventData> keyData;
		optr<MouseEventData> mouseData;
		optr<TouchEventData> touchData;
		optr<GestureEventData> gestureData;
		optr<AccelerationEventData> accelerationData;

		std::vector<int> lastTouchPoints;

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

			for (int each = 0; each < OIS_MAX_NUM_TOUCHES * 3; each++)
			{
				this->lastTouchPoints.push_back(-1);
			}
#ifdef ORKIGE_ENABLE_TUIO
  			this->tuioClient = new TUIO::TuioClient(3333);
  			this->tuioClient->addTuioListener(this);
  			this->tuioClient->connect();
#endif

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
			static float contentScalingFactor = 1.f;
#ifdef ORKIGE_IPHONE
#if __IPHONE_4_0
			static UIView* view = nil;
			if(view == nil)
			{
				if([[[UIDevice currentDevice] systemVersion] floatValue] >= 4.0)
				{
					Engine::getSingleton().getRenderWindow()->getCustomAttribute( "VIEW", &view );
					contentScalingFactor = [view contentScaleFactor];
				}
			}
#endif
#endif
			unsigned int width, height, depth;
			int left, top;
			Engine::getSingleton().getRenderWindow()->getMetrics( width, height, depth, left, top );

			int h = width;
			int w = height;
			int absX = int(data->absX*contentScalingFactor);
			int absY = int(data->absY*contentScalingFactor);
			int relX = int(data->relX*contentScalingFactor);
			int relY = int(data->relY*contentScalingFactor);

			//oDebugMsg("core", 0, "Input: x:" << absX <<  " y:" << absY);
			switch (Engine::getSingleton().getViewport()->getOrientationMode())
			{
			case Ogre::OR_DEGREE_0:   //OR_PORTRAIT
                data->absX = absX;
                data->absY = absY;
                data->relX = relX;
                data->relY = relY;
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
				data->absY = w - absX;
				data->relX = relY;
				data->relY = -relX;
				break;
			}
		}

		inline int closestSquenceId(const OIS::MultiTouchState &state)
		{
			unsigned int closestDistance = 999999;
			unsigned int currentDistance = 999999;
			int closestSequence = -1;

			for (int each = 0; each < (int)this->lastTouchPoints.size(); each += 3)
			{
				if (this->lastTouchPoints.at(each) != -1 )
				{
					currentDistance = (this->lastTouchPoints.at(each) - state.X.abs) * (this->lastTouchPoints.at(each) - state.X.abs);
					currentDistance += (this->lastTouchPoints.at(each + 1) - state.Y.abs) * (this->lastTouchPoints.at(each + 1) - state.Y.abs);
					currentDistance += (this->lastTouchPoints.at(each + 2) - state.Z.abs) * (this->lastTouchPoints.at(each + 2) - state.Z.abs);

					if (currentDistance < closestDistance)
					{
						closestSequence = each;
						closestDistance = currentDistance;
					}
				}
			}
			return closestSequence / 3;
		}
		

		inline int getTouchSquenceId(const OIS::MultiTouchState &state)
		{
			if ( state.touchIsType(OIS::MT_Pressed) )
			{
				// new sequence

				for (int each = 0; each < (int)this->lastTouchPoints.size(); each += 3)
				{
					if (this->lastTouchPoints.at(each) == -1 )
					{
						this->lastTouchPoints.at(each) = state.X.abs;
						this->lastTouchPoints.at(each + 1) = state.Y.abs;
						this->lastTouchPoints.at(each + 2) = state.Z.abs;
						return (each / 3);		//this is the ID
					}
				}
				return -1;
			}
			else if ( state.touchIsType(OIS::MT_Released) || (state.touchIsType(OIS::MT_Cancelled)) )
			{
				// find the sequence and "release" the vector entries

				int closestSequenceId = this->closestSquenceId(state);
				this->lastTouchPoints.at(closestSequenceId * 3) = -1;
				this->lastTouchPoints.at(closestSequenceId * 3 + 1) = -1;
				this->lastTouchPoints.at(closestSequenceId * 3 + 2) = -1;
				return closestSequenceId;
			}
			else if ( state.touchIsType(OIS::MT_Moved) )
			{
				// find the sequence
				return this->closestSquenceId(state);
			}
			return -1;
		}
	

		inline void oisMouseToOrkige(const OIS::MouseState &state)
		{
			Ogre::Real scaleFactorX = 1.0f;
			Ogre::Real scaleFactorY = 1.0f;
			/*if(Engine::getSingleton().getCamera()->getProjectionType() == Ogre::PT_ORTHOGRAPHIC)
			{
				scaleFactorX = (Ogre::Real)(Engine::getSingleton().getCamera()->getOrthoWindowHeight()) / (Ogre::Real)(Engine::getSingleton().getViewport()->getActualHeight());
				scaleFactorY = (Ogre::Real)(Engine::getSingleton().getCamera()->getOrthoWindowWidth()) / (Ogre::Real)(Engine::getSingleton().getViewport()->getActualWidth());
			}*/
			
			this->mouseData->buttons = state.buttons;
			this->mouseData->relX = state.X.rel / scaleFactorX;
			this->mouseData->relY = state.Y.rel / scaleFactorY;
			this->mouseData->relZ = state.Z.rel / scaleFactorY;
			this->mouseData->absX = state.X.abs / scaleFactorX;
			this->mouseData->absY = state.Y.abs / scaleFactorY;
			this->mouseData->absZ = state.Z.abs / scaleFactorY;
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
			this->touchData->sequenceId = this->getTouchSquenceId(state);
			this->transformInputToOrientation(this->touchData);
		}
	
#ifdef ORKIGE_ENABLE_TUIO
		virtual void addTuioObject(TUIO::TuioObject *tobj)
		{
			int test = 1;
		}
		virtual void updateTuioObject(TUIO::TuioObject *tobj)
		{
			int test = 1; 
		}
		virtual void removeTuioObject(TUIO::TuioObject *tobj)
		{
			int test = 1;
		}
		virtual void addTuioCursor(TUIO::TuioCursor *tcur)
		{
			if (tcur->getCursorID() != 1)
				return;
			//this->tuioMultiTouchToOrkige(tcur);
				std::list<TUIO::TuioPoint> path = tcur->getPath();
				if (path.size()>1) 
				{
					path.pop_back();
				}
				
				TUIO::TuioPoint last_point = path.back();

	

				this->touchData->relX = (int)((tcur->getX()-last_point.getX())*1980);
				this->touchData->relY = (int)((tcur->getY()-last_point.getY())*1080) ;
				this->touchData->relZ = 0;
				this->touchData->absX = (int)(tcur->getX()*1980);
				this->touchData->absY = (int)(tcur->getY()*1080);
				this->touchData->absZ = (int)(tcur->getY()*1080);

				this->touchData->sequenceId = tcur->getCursorID();
				this->transformInputToOrientation(this->touchData);
			

				GlobalEventManager::getSingleton().queueEvent(oBadPointer(&this->touchPressedEvent));
		}
		virtual void updateTuioCursor(TUIO::TuioCursor *tcur)
		{
			if (tcur->getCursorID() != 1)
				return;
			//std::list<TUIO::TuioPoint> path = tcur->getPath();
			//if (path.size()>1) 
			//{
			//	path.pop_back();
			//}
			//TUIO::TuioPoint last_point = path.back();


			int distanceX = Ogre::Math::Abs(this->touchData->absX - ((int)(tcur->getX()*1980))) ;
			int distanceY = Ogre::Math::Abs(this->touchData->absY - ((int)(tcur->getY()*1080))) ;

			bool sendEv= false ;
			if (distanceX > 14)
			{
				this->touchData->relX = (int)((tcur->getX())*1980)-this->touchData->absX;
				this->touchData->absX = (int)(tcur->getX()*1980);
				sendEv= true;
			}
			else
			{
				this->touchData->relX = 0;
			}

			if (distanceY > 14)
			{
				this->touchData->relY = (int)((tcur->getY())*1080)-this->touchData->absY ;
				this->touchData->absY = (int)(tcur->getY()*1080);
				sendEv= true;
			}
			else
			{
				this->touchData->relY = 0;
			}
			
			oDebugMsg("core", 0, "distances (X,Y)= " <<distanceX<<" , "<<distanceY);

			/*this->touchData->relX = (int)((tcur->getX()-last_point.getX())*1980);
			this->touchData->relY = (int)((tcur->getY()-last_point.getY())*1080) ;
			this->touchData->relZ = 0;
			this->touchData->absX = (int)(tcur->getX()*1980);
			this->touchData->absY = (int)(tcur->getY()*1080);
			this->touchData->absZ = (int)(tcur->getY()*1080);*/

			if(sendEv)
			{
				this->touchData->sequenceId = tcur->getCursorID();
				this->transformInputToOrientation(this->touchData);
			
				GlobalEventManager::getSingleton().queueEvent(oBadPointer(&this->touchMovedEvent));
			}
				
		}
		virtual void removeTuioCursor(TUIO::TuioCursor *tcur)
		{



			std::list<TUIO::TuioPoint> path = tcur->getPath();
			if (path.size()>1) 
			{
				path.pop_back();
			}
			TUIO::TuioPoint last_point = path.back();


			this->touchData->relX = (int)((tcur->getX()-last_point.getX())*1980);
			this->touchData->relY = (int)((tcur->getY()-last_point.getY())*1080) ;
			this->touchData->relZ = 0;
			this->touchData->absX = (int)(tcur->getX()*1980);
			this->touchData->absY = (int)(tcur->getY()*1080);
			this->touchData->absZ = (int)(tcur->getY()*1080);

			this->touchData->sequenceId = tcur->getCursorID();
			this->transformInputToOrientation(this->touchData);
			
			GlobalEventManager::getSingleton().queueEvent(oBadPointer(&this->touchReleasedEvent));
		}
		virtual void refresh(TUIO::TuioTime ftime)
		{
			
		}

#endif
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
			GlobalEventManager::getSingleton().trigger(this->touchMovedEvent);
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
			GlobalEventManager::getSingleton().trigger(this->touchPressedEvent);
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
			//GlobalEventManager::getSingleton().trigger(this->accelerationEvent);
		}
	};
}

#ifdef __cplusplus
extern "C" {
#endif
	void OrkigeBrowserPluginSendKeyPressed(Orkige::KeyEventData::KeyCode key, unsigned int text)
	{
		Orkige::InputManagerImpl::getSingleton().keyData->key = key;
		Orkige::InputManagerImpl::getSingleton().keyData->text = text;
		Orkige::GlobalEventManager::getSingleton().trigger(Orkige::InputManagerImpl::getSingleton().keyPressedEvent);
	}
	void OrkigeBrowserPluginSendKeyReleased(Orkige::KeyEventData::KeyCode key, unsigned int text)
	{
		Orkige::InputManagerImpl::getSingleton().keyData->key = key;
		Orkige::InputManagerImpl::getSingleton().keyData->text = text;
		Orkige::GlobalEventManager::getSingleton().trigger(Orkige::InputManagerImpl::getSingleton().keyReleasedEvent);
	}
	void OrkigeBrowserPluginSendMouseMoved(int x, int y)
	{
		int oldX = Orkige::InputManagerImpl::getSingleton().mouseData->absX;
		int oldY = Orkige::InputManagerImpl::getSingleton().mouseData->absY;
		Orkige::InputManagerImpl::getSingleton().mouseData->absX = x;
		Orkige::InputManagerImpl::getSingleton().mouseData->absY = y;
		Orkige::InputManagerImpl::getSingleton().mouseData->relX = x - oldX;
		Orkige::InputManagerImpl::getSingleton().mouseData->relY = y - oldY;
		Orkige::GlobalEventManager::getSingleton().trigger(Orkige::InputManagerImpl::getSingleton().mouseMovedEvent);
	}
	void OrkigeBrowserPluginSendMousePressed(Orkige::MouseEventData::MouseButtonID buttonID, int x, int y)
	{
		Orkige::InputManagerImpl::getSingleton().mouseData->button = buttonID;
		int oldX = Orkige::InputManagerImpl::getSingleton().mouseData->absX;
		int oldY = Orkige::InputManagerImpl::getSingleton().mouseData->absY;
		Orkige::InputManagerImpl::getSingleton().mouseData->absX = x;
		Orkige::InputManagerImpl::getSingleton().mouseData->absY = y;
		Orkige::InputManagerImpl::getSingleton().mouseData->relX = x - oldX;
		Orkige::InputManagerImpl::getSingleton().mouseData->relY = y - oldY;
		Orkige::GlobalEventManager::getSingleton().trigger(Orkige::InputManagerImpl::getSingleton().mousePressedEvent);
	}
	void OrkigeBrowserPluginSendMouseReleased(Orkige::MouseEventData::MouseButtonID buttonID, int x, int y)
	{
		Orkige::InputManagerImpl::getSingleton().mouseData->button = buttonID;
		int oldX = Orkige::InputManagerImpl::getSingleton().mouseData->absX;
		int oldY = Orkige::InputManagerImpl::getSingleton().mouseData->absY;
		Orkige::InputManagerImpl::getSingleton().mouseData->absX = x;
		Orkige::InputManagerImpl::getSingleton().mouseData->absY = y;
		Orkige::InputManagerImpl::getSingleton().mouseData->relX = x - oldX;
		Orkige::InputManagerImpl::getSingleton().mouseData->relY = y - oldY;
		Orkige::GlobalEventManager::getSingleton().trigger(Orkige::InputManagerImpl::getSingleton().mouseReleasedEvent);
	}
#ifdef __cplusplus
}
#endif

namespace Orkige
{
	IMPL_OSINGLETON(InputManagerImpl);

	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	InputManager::InputManager(bool shareMouse, bool enableNativeInput)
	{
		this->frameListener = GlobalEventManager::getSingleton().bind(Engine::FrameStartedEvent,&InputManager::onFrameStarted,this);
		this->impl = new InputManagerImpl();
		this->sharedMouse = shareMouse;
		if(enableNativeInput)
		{
			this->initialise();
		}
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
#ifdef ORKIGE_IPHONE
#   ifdef __OBJC__
	//---------------------------------------------------------
	UIView* InputManager::getInputDelegate()
	{
		return static_cast<UIView*>(static_cast<OIS::iPhoneInputManager*>(this->impl->inputSystem)->_getDelegate());
	}
#   endif
#endif

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

			String const & externalTopLevelWindowHandle = Engine::getSingleton().getTopLevelWindowHandle();

			if(externalTopLevelWindowHandle.empty())//Default Window
			{
				size_t windowHnd = 0;

				// Get window handle
//#if defined OIS_LINUX_PLATFORM        // this is no longer needed as far as I know (pe)
//				Engine::getSingleton().getRenderWindow()->getCustomAttribute( "GLXWINDOW", &windowHnd );
//#else
				Engine::getSingleton().getRenderWindow()->getCustomAttribute( "WINDOW", &windowHnd );
//#endif

				paramList.insert( std::make_pair( "WINDOW", StringUtil::Converter::toString(windowHnd) ) );

				if(this->sharedMouse)
				{
					// note: on win32 the hardware and buffered software mouse position differs a centimeter.
					// it will become congruent once you move the pointer over the right or bottom window border.
					paramList.insert(std::make_pair("w32_mouse",	"DISCL_FOREGROUND"));
					paramList.insert(std::make_pair("w32_mouse",	"DISCL_NONEXCLUSIVE"));
				}
			}
			else//wxWindow or something else
			{
				paramList.insert(std::make_pair("WINDOW",		externalTopLevelWindowHandle ) );
				paramList.insert(std::make_pair("w32_keyboard", "DISCL_FOREGROUND"));
				paramList.insert(std::make_pair("w32_keyboard", "DISCL_NONEXCLUSIVE"));
				paramList.insert(std::make_pair("w32_mouse",	"DISCL_FOREGROUND"));
				paramList.insert(std::make_pair("w32_mouse",	"DISCL_NONEXCLUSIVE"));

			}

			oDebugMsg("core", 0, "creating InputSystem");
			// Create inputsystem
			try
			{
				this->impl->inputSystem = OIS::InputManager::createInputSystem( paramList );
			}
			catch (OIS::Exception const & e)
			{
				oDebugMsg("core", 0, "Error creating InputSystem: " << e.eText);
				return;
			}
			catch (...)
			{
				oDebugMsg("core", 0, "Error creating InputSystem: " << "UNKNOWN_EXCEPTION");
				return;
			}


#ifndef ORKIGE_IPHONE
			oDebugMsg("core", 0, "creating Keyboard");
			try
			{
				// If possible create a buffered keyboard
				this->impl->keyboard = static_cast<OIS::Keyboard*>( this->impl->inputSystem->createInputObject( OIS::OISKeyboard, true ) );
				if(this->impl->keyboard)
					this->impl->keyboard->setEventCallback( this->impl );
			}
			catch (OIS::Exception const & e)
			{
				oDebugMsg("core", 0, "Error creating OISKeyboard: " << e.eText);
			}
			catch (...)
			{
				oDebugMsg("core", 0, "Error creating OISKeyboard: " << "UNKNOWN_EXCEPTION");
				return;
			}

			oDebugMsg("core", 0, "creating Mouse");
			try
			{
				// If possible create a buffered mouse
				this->impl->mouse = static_cast<OIS::Mouse*>( this->impl->inputSystem->createInputObject( OIS::OISMouse, true ) );
				if(this->impl->mouse)
					this->impl->mouse->setEventCallback( this->impl );
			}
			catch (OIS::Exception const & e)
			{
				oDebugMsg("core", 0, "Error creating OISMouse: " << e.eText);
			}
			catch (...)
			{
				oDebugMsg("core", 0, "Error creating OISMouse: " << "UNKNOWN_EXCEPTION");
				return;
			}
#else
			// If possible create a buffered mouse
			this->impl->touch = static_cast<OIS::MultiTouch*>( this->impl->inputSystem->createInputObject( OIS::OISMultiTouch, true ) );
			this->impl->touch->setEventCallback( this->impl );

			//this->impl->gestureView = [[OrkigeGestureView alloc] init];
			//[[[UIApplication sharedApplication] keyWindow] addSubview:this->impl->gestureView];
			//[[UIAccelerometer sharedAccelerometer] setUpdateInterval:(1.0 / 30)];
			//[[UIAccelerometer sharedAccelerometer] setDelegate:this->impl->gestureView];
			//this->impl->gestureView.inputManager = impl;
			//this->impl->gestureView.acc = Ogre::Vector3::ZERO;
#endif
			// Get window size
			unsigned int width, height, depth;
			int left, top;
			Engine::getSingleton().getRenderWindow()->getMetrics( width, height, depth, left, top );

			// Set mouse region
			this->setWindowExtents( width, height );
			oDebugMsg("core", 0, "Input initialized! width, height, depth, left, top: " << width <<", "<< height<<", "<< depth<<", "<< left<<", "<< top);





		}
	}
	//---------------------------------------------------------
	void InputManager::capture( void )
	{
		try
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

		}
		catch (OIS::Exception const & e)
		{
			oDebugMsg("core", 0, "Error capturing Input: " << e.eText);
		}
		catch (Ogre::Exception const & e)
		{
			oDebugMsg("core", 0, "Error capturing Input: " << e.getDescription());
			throw e;
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

