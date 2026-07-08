/**************************************************************
	created:	2011/11/03 at 1:15
	filename: 	MyGuiManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/

#include "engine_mygui/MyGuiManager.h"
#include "engine_graphic/Engine.h"
#include "engine_base/Localisation.h"

namespace Orkige
{
	IMPL_OSINGLETON(MyGuiManager);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	MyGuiManager::MyGuiManager(Orkige::String const & _core, unsigned short activeViewport)
	{
		this->ogrePlatform = onew(new MyGUI::OgrePlatform());
		this->ogrePlatform->initialise(Orkige::Engine::getSingleton().getRenderWindow(activeViewport), Orkige::Engine::getSingleton().getSceneManager());
		this->ogrePlatform->getRenderManagerPtr()->setActiveViewport(activeViewport);

		this->myGui = onew(new MyGUI::Gui());

		this->myGui->initialise(_core);

		MyGUI::LanguageManager::getInstance().setCurrentLanguage(Localisation::getSingleton().getCurrentLocale());
	}
	//---------------------------------------------------------
	MyGuiManager::~MyGuiManager()
	{
		this->myGui->shutdown();
		this->ogrePlatform->shutdown();
	}
	//---------------------------------------------------------
	void MyGuiManager::enableInputEvents()
	{
		this->registerEvent(Orkige::InputManager::KeyPressedEvent,				&MyGuiManager::onKeyPressed,			this);
		this->registerEvent(Orkige::InputManager::KeyReleasedEvent,				&MyGuiManager::onKeyReleased,			this);
#ifdef ORKIGE_IPHONE
		this->registerEvent(Orkige::InputManager::TouchPressedEvent,			&MyGuiManager::onTouchPressed,			this);
		this->registerEvent(Orkige::InputManager::TouchReleasedEvent,			&MyGuiManager::onTouchReleased,			this);
		this->registerEvent(Orkige::InputManager::TouchMovedEvent,				&MyGuiManager::onTouchMoved,			this);
#else
		this->registerEvent(Orkige::InputManager::MousePressedEvent,			&MyGuiManager::onMousePressed,			this);
		this->registerEvent(Orkige::InputManager::MouseReleasedEvent,			&MyGuiManager::onMouseReleased,			this);
		this->registerEvent(Orkige::InputManager::MouseMovedEvent,				&MyGuiManager::onMouseMoved,			this);
#endif
	}
	//---------------------------------------------------------
	void MyGuiManager::disableInputEvents()
	{
		this->unregisterEvent(Orkige::InputManager::KeyPressedEvent);
		this->unregisterEvent(Orkige::InputManager::KeyReleasedEvent);
#ifdef ORKIGE_IPHONE
		this->unregisterEvent(Orkige::InputManager::TouchPressedEvent);
		this->unregisterEvent(Orkige::InputManager::TouchReleasedEvent);
		this->unregisterEvent(Orkige::InputManager::TouchMovedEvent);
#else
		this->unregisterEvent(Orkige::InputManager::MousePressedEvent);
		this->unregisterEvent(Orkige::InputManager::MouseReleasedEvent);
		this->unregisterEvent(Orkige::InputManager::MouseMovedEvent);
#endif
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------	
	//---------------------------------------------------------
#if MYGUI_PLATFORM == MYGUI_PLATFORM_WIN32
	MyGUI::Char translateWin32Text(MyGUI::KeyCode kc)
	{
		static WCHAR deadKey = 0;

		BYTE keyState[256];
		HKL  layout = GetKeyboardLayout(0);
		if ( GetKeyboardState(keyState) == 0 )
			return 0;

		int code = *((int*)&kc);
		unsigned int vk = MapVirtualKeyEx((UINT)code, 3, layout);
		if ( vk == 0 )
			return 0;

		WCHAR buff[3] = { 0, 0, 0 };
		int ascii = ToUnicodeEx(vk, (UINT)code, keyState, buff, 3, 0, layout);
		if (ascii == 1 && deadKey != '\0' )
		{
			// A dead key is stored and we have just converted a character key
			// Combine the two into a single character
			WCHAR wcBuff[3] = { buff[0], deadKey, '\0' };
			WCHAR out[3];

			deadKey = '\0';
			if (FoldStringW(MAP_PRECOMPOSED, (LPWSTR)wcBuff, 3, (LPWSTR)out, 3))
				return out[0];
		}
		else if (ascii == 1)
		{
			// We have a single character
			deadKey = '\0';
			return buff[0];
		}
		else if (ascii == 2)
		{
			// Convert a non-combining diacritical mark into a combining diacritical mark
			// Combining versions range from 0x300 to 0x36F; only 5 (for French) have been mapped below
			// http://www.fileformat.info/info/unicode/block/combining_diacritical_marks/images.htm
			switch (buff[0])
			{
			case 0x5E: // Circumflex accent: â
				deadKey = 0x302;
				break;
			case 0x60: // Grave accent: à
				deadKey = 0x300;
				break;
			case 0xA8: // Diaeresis: ü
				deadKey = 0x308;
				break;
			case 0xB4: // Acute accent: é
				deadKey = 0x301;
				break;
			case 0xB8: // Cedilla: ç
				deadKey = 0x327;
				break;
			default:
				deadKey = buff[0];
				break;
			}
		}

		return 0;
	}
#endif
	//---------------------------------------------------------
	bool MyGuiManager::onKeyPressed(Orkige::Event const & event)
	{
		optr<KeyEventData> data = event.getDataPtr<KeyEventData>();
		MyGUI::Char text = (MyGUI::Char)data->text;
		MyGUI::KeyCode key = MyGUI::KeyCode::Enum(data->key);
		int scan_code = key.toValue();

		if (scan_code > 70 && scan_code < 84)
		{
			static MyGUI::Char nums[13] = { 55, 56, 57, 45, 52, 53, 54, 43, 49, 50, 51, 48, 46 };
			text = nums[scan_code-71];
		}
		else if (key == MyGUI::KeyCode::Divide)
		{
			text = '/';
		}
		else
		{
#if MYGUI_PLATFORM == MYGUI_PLATFORM_WIN32
			text = translateWin32Text(key);
#endif
		}

		MyGUI::InputManager::getInstance().injectKeyPress(key, text);
		return false;
	}
	//---------------------------------------------------------
	bool MyGuiManager::onKeyReleased(Orkige::Event const & event)
	{
		optr<KeyEventData> data = event.getDataPtr<KeyEventData>();
		MyGUI::KeyCode key = MyGUI::KeyCode::Enum(data->key);
		MyGUI::InputManager::getInstance().injectKeyRelease(key);
		return false;
	}
	//---------------------------------------------------------
	bool MyGuiManager::onMousePressed(Orkige::Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();
		MyGUI::InputManager::getInstance().injectMousePress(data->absX,data->absY, MyGUI::MouseButton::Enum(data->button));
		return false;
	}
	//---------------------------------------------------------
	bool MyGuiManager::onMouseReleased(Orkige::Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();
		MyGUI::InputManager::getInstance().injectMouseRelease(data->absX,data->absY, MyGUI::MouseButton::Enum(data->button));
		return false;
	}
	//---------------------------------------------------------
	bool MyGuiManager::onMouseMoved(Orkige::Event const & event)
	{
		optr<MouseEventData> data = event.getDataPtr<MouseEventData>();
		MyGUI::InputManager::getInstance().injectMouseMove(data->absX,data->absY,data->absZ);
		return false;
	}
	//---------------------------------------------------------
	bool MyGuiManager::onTouchPressed(Orkige::Event const & event)
	{
		optr<TouchEventData> data = event.getDataPtr<TouchEventData>();
		if(data->sequenceId < 3)
		{
			MyGUI::InputManager::getInstance().injectMousePress(data->absX,data->absY, MyGUI::MouseButton::Enum(data->sequenceId));
		}
		return false;
	}
	//---------------------------------------------------------
	bool MyGuiManager::onTouchReleased(Orkige::Event const & event)
	{
		optr<TouchEventData> data = event.getDataPtr<TouchEventData>();
		if(data->sequenceId < 3)
		{
			MyGUI::InputManager::getInstance().injectMouseRelease(data->absX,data->absY, MyGUI::MouseButton::Enum(data->sequenceId));
		}
		return false;
	}
	//---------------------------------------------------------
	bool MyGuiManager::onTouchMoved(Orkige::Event const & event)
	{
		optr<TouchEventData> data = event.getDataPtr<TouchEventData>();
		if(data->sequenceId == 0)
		{
			MyGUI::InputManager::getInstance().injectMouseMove(data->absX,data->absY,data->absZ);
		}
		return false;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(MyGuiManager)
	OOBJECT_END
}