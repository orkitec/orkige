/**************************************************************
	created:	2010/08/07 at 1:35
	filename: 	IngameConsole.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_graphic/IngameConsole.h"
#include <core_script/ScriptRuntime.h>
#include <core_util/foreach.h>
#include <core_event/GlobalEventManager.h>
#include "engine_module/EnginePrerequisitesClassic.h"
#include <OgreOverlay.h>
#include <OgreOverlayManager.h>
#include "engine_graphic/Engine.h"
#include "engine_input/InputManager.h"
#include "engine_util/StringUtil.h"

//@TODO: cleanup
namespace Orkige
{
	IMPL_OSINGLETON(IngameConsole);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
#define CONSOLE_LINE_LENGTH 150
#define CONSOLE_LINE_COUNT 15
	//---------------------------------------------------------
	IngameConsole::IngameConsole()
	{
		this->start_line = 0;
		this->cursor_pos = 0;
		this->pythonMode = false;
		this->visible = false;
		this->initialized = false;
		this->cursor = "|";
		this->history_pos = 0;
		this->height = 0;
		this->blink = 0;
	}
	//---------------------------------------------------------
	IngameConsole::~IngameConsole()
	{
		std::ofstream out("console.history", std::ios::out | std::ios::trunc);

		foreach(String str,this->history)
		{
			out << str <<std::endl;
		}
		this->shutdown();
	}
	//---------------------------------------------------------
	void IngameConsole::init()
	{
		// OGRE 14: getSceneManagerIterator() became getSceneManagers()
		if(Ogre::Root::getSingleton().getSceneManagers().empty())
			OGRE_EXCEPT( Ogre::Exception::ERR_INTERNAL_ERROR, "No this->scene manager found!", "init" );

		if(this->initialized)
			OGRE_EXCEPT( Ogre::Exception::ERR_INTERNAL_ERROR, "Console already this->initialized!", "init" );

		std::ifstream in("console.history");
		if (in)
		{
			while (in) 
			{
				String buf;
				std::getline(in, buf);

				if (in) 
					this->history.push_back(buf);
			}
			this->history_pos = this->history.size();
		}

		this->scene=Ogre::Root::getSingleton().getSceneManagers().begin()->second;

		// Create background rectangle covering the whole screen
		this->rect = new Ogre::Rectangle2D(true);
		this->rect->setCorners(-1, 1, 1, 1-this->height);
		// OGRE 14: SimpleRenderable::setMaterial takes the MaterialPtr directly
		this->rect->setMaterial(Ogre::MaterialManager::getSingleton().getByName("console/background"));
		this->rect->setRenderQueueGroup(Ogre::RENDER_QUEUE_OVERLAY);
		this->rect->setBoundingBox(Ogre::AxisAlignedBox(-100000.0*Ogre::Vector3::UNIT_SCALE, 100000.0*Ogre::Vector3::UNIT_SCALE));
		node = this->scene->getRootSceneNode()->createChildSceneNode("#Console");
		node->attachObject(this->rect);

		this->textbox=Ogre::OverlayManager::getSingleton().createOverlayElement("TextArea", "ConsoleText");
		this->textbox->setCaption("hello");
		this->textbox->setMetricsMode(Ogre::GMM_RELATIVE);
		this->textbox->setPosition(0,0);
		this->textbox->setParameter("font_name", "Console");
		this->textbox->setParameter("colour_top", "1 0.8 1");
		this->textbox->setParameter("colour_bottom", "1 0.9 1");
		this->textbox->setParameter("char_height", "0.015");

		this->overlay=Ogre::OverlayManager::getSingleton().create("Console");	
		this->overlay->setZOrder(500);
		this->overlay->add2D((Ogre::OverlayContainer*)this->textbox);
		this->overlay->show();
		this->textbox->hide();
		

		Ogre::LogManager::getSingleton().getDefaultLog()->addListener(this);
		this->keyListener = createEventListenerPtr(&IngameConsole::onKeyPressed,this);
		GlobalEventManager::getSingleton().bind(Engine::FrameStartedEvent,&IngameConsole::onFrameStarted,this);

		this->addCommand("help", printDefaultConsoleHelp);
		this->initialized = true;
	}
	//---------------------------------------------------------
	void IngameConsole::shutdown()
	{
		Ogre::LogManager::getSingleton().getDefaultLog()->removeListener(this);
		if(!this->initialized)
			return;
		delete this->rect;
		this->overlay->clear();
		//delete node;
		//delete this->textbox;
		//delete this->overlay;
	}
	//---------------------------------------------------------
	void IngameConsole::print(const String &text)
	{
		//subdivide it into this->lines
		const char *str = text.c_str();
		int start = 0, count = 0;
		std::size_t	len = text.length();
		String line;
		for(std::size_t	c = 0; c < len; c++)
		{
			if(str[c] == '\n' || line.length() >= CONSOLE_LINE_LENGTH)
			{
				this->lines.push_back(line);
				line = "";
			}
			if(str[c] != '\n')
				line += str[c];
		}
		if(line.length())
			this->lines.push_back(line);
		if(this->lines.size() > CONSOLE_LINE_COUNT)
			this->start_line = this->lines.size()-CONSOLE_LINE_COUNT;
		else
			this->start_line = 0;
		this->update_overlay = true;
	}
	//---------------------------------------------------------
	void IngameConsole::setVisible(bool visible)
	{
		if(!this->initialized)
			OGRE_EXCEPT( Ogre::Exception::ERR_INTERNAL_ERROR, "Console is not this->initialized!", "setVisible" );

		if(this->visible == visible)
			return;

		if(visible)
			GlobalEventManager::getSingleton().addListener(this->keyListener,InputManager::KeyPressedEvent);//add the listener 
		else
			GlobalEventManager::getSingleton().delListener(this->keyListener,InputManager::KeyPressedEvent);//del the listener 

		this->visible = visible;
	}
	//---------------------------------------------------------
	void IngameConsole::switchVisible()
	{
		this->setVisible(!this->visible);
	}
	//---------------------------------------------------------
	void IngameConsole::addCommand(const String &command, IngameConsole::ConsoleCommand func)
	{
		this->commands[command] = func;
	}
	//---------------------------------------------------------
	void IngameConsole::removeCommand(const String &command)
	{
		this->commands.erase(this->commands.find(command));
	}
	//---------------------------------------------------------
	Orkige::StringVector IngameConsole::getCommandNames()
	{
		Orkige::StringVector names;
		foreach(ConsoleCommandRegistry::value_type const & vt, this->commands)
		{
			names.push_back(vt.first);
		}
		return names;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	bool IngameConsole::onKeyPressed(Event const & event)
	{
		optr<KeyEventData> data = event.getDataPtr<KeyEventData>();

		if(data->key == KeyEventData::KC_RETURN && this->prompt.length())
		{
			this->print(this->prompt);

			//split the parameter list (no boost: Ogre::StringUtil does the job)
			Orkige::StringVector params = Ogre::StringUtil::split(this->prompt, "\t ");

			if(this->pythonMode == false && (params[0] == "script" || params[0] == "python"))
			{
				//script mode only opens when a scripting runtime is live
				if(ScriptRuntime::available())
					this->pythonMode = true;
			}
			else if(this->pythonMode == true && params[0] == "exit")
			{
				this->pythonMode = false;
			}
			else if(this->pythonMode)
			{
				const ScriptRuntime::Result result =
					ScriptRuntime::getSingleton().runString(this->prompt);
				if(!result.success)
					this->print("error: " + result.error);
				else foreach(String const & value, result.returnValues)
					this->print(value);
			}
			else
			{
				//try to execute the command
				ConsoleCommandRegistry::iterator i;
				for(i=this->commands.begin();i!=this->commands.end();i++)
				{
					if((*i).first==params[0])
					{
						if((*i).second)
							(*i).second(params);
						break;
					}
				}
			}

			//
			this->history.push_back(this->prompt);
			this->history_pos=this->history.size();
			this->prompt = "";
			this->cursor_pos = 0;
		}
		else if(data->key == KeyEventData::KC_DELETE)
		{
			if(this->cursor_pos<this->prompt.length())
			{
				this->prompt=(this->prompt.substr(0,this->cursor_pos)+this->prompt.substr(this->cursor_pos+1,this->prompt.length()));
			}
		}
		else if(data->key == KeyEventData::KC_BACK)
		{
			if(this->cursor_pos > 0)
			{
				std::size_t	from = 0;
				if(this->cursor_pos > 1)
					from = this->cursor_pos-1;
				this->prompt = (this->prompt.substr(0,from)+this->prompt.substr(this->cursor_pos,this->prompt.length()));
				this->cursor_pos--;
			}
		}
		else if(data->key == KeyEventData::KC_PGUP)
		{
			if(this->start_line > 0)
				this->start_line--;
		}
		else if(data->key == KeyEventData::KC_PGDOWN)
		{
			if(this->start_line < this->lines.size())
				this->start_line++;
		}
		else if(data->key == KeyEventData::KC_LSHIFT || data->key == KeyEventData::KC_RSHIFT)
		{
			//don't do anything on shift
		}
		else if (data->key == KeyEventData::KC_UP)
		{
			if(this->history_pos > 0)
			{
				this->history_pos--;
				this->prompt = this->history[this->history_pos];
				this->cursor_pos = this->prompt.length();
			}
		}
		else if (data->key == KeyEventData::KC_DOWN)
		{
			if(this->history.size() > 0 && this->history_pos < this->history.size()-1)
			{
				this->history_pos++;
				this->prompt = this->history[this->history_pos];
				this->cursor_pos=this->prompt.length();
			}
			else
			{
				this->prompt = "";
				this->cursor_pos = 0;
				this->history_pos = this->history.size();
			}

		}
		else if (data->key == KeyEventData::KC_LEFT)
		{
			if(this->cursor_pos > 0)
			{
				this->cursor_pos--;
			}
		}
		else if (data->key == KeyEventData::KC_RIGHT)
		{
			if(this->cursor_pos < this->prompt.length())
			{
				this->cursor_pos++;
			}
		}
		else if (data->key == KeyEventData::KC_SPACE)
		{
			if(this->prompt.length() > 0)
			{
				this->prompt = (this->prompt.substr(0,this->cursor_pos)+" "+this->prompt.substr(this->cursor_pos,this->prompt.length()));
			}
			else
			{
				this->prompt += " ";
			}
			this->cursor_pos++;
		}
		else
		{
			static String legalchars ="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890+!\"#%&/()=?[]\\*-_.:,; ";
			String input = InputManager::getSingleton().getAsString(data->key);
			StringUtil::to_lower(input);
			if(legalchars.find(input) != String::npos)
			{
				if(this->prompt.length() > 0)
				{
					this->prompt = (this->prompt.substr(0,this->cursor_pos)+input+this->prompt.substr(this->cursor_pos,this->prompt.length()));
				}
				else
				{
					this->prompt += input;
				}

				this->cursor_pos++;
			}
		}

		this->update_overlay = true;
		return false;
	}
	//---------------------------------------------------------
	bool IngameConsole::onFrameStarted(Event const & event)
	{
		if(this->visible && this->height < 1)
		{
			optr<FrameEventData> data = event.getDataPtr<FrameEventData>();
			this->height += data->timeSinceLastFrame*2;
			this->textbox->show();
			if(this->height >= 1)
			{
				this->height = 1;
			}
			this->textbox->setPosition(0, (this->height-1)*0.5f);
			this->rect->setCorners(-1, 1+this->height, 1, 1-this->height);
		}
		else if(!this->visible && this->height > 0)
		{
			optr<FrameEventData> data = event.getDataPtr<FrameEventData>();
			this->height -= data->timeSinceLastFrame*2;
			if(this->height <= 0)
			{
				this->height = 0;
				this->textbox->hide();
			}
			this->textbox->setPosition(0, (this->height-1)*0.5f);
			this->rect->setCorners(-1, 1+this->height, 1, 1-this->height);
		}
		else if(this->visible && this->blink > 0.5)
		{
			if(this->cursor == "|")
				this->cursor = ".";
			else
				this->cursor = "|";

			this->blink = 0;
			this->update_overlay = true;
		}
		else if(this->visible)
		{
			optr<FrameEventData> data = event.getDataPtr<FrameEventData>();
			this->blink += data->timeSinceLastFrame;
		}

		if(this->update_overlay)
		{
			String text;
			std::list<String>::iterator i,start,end;

			//make sure is in range
			if(this->start_line > this->lines.size())
				this->start_line = this->lines.size();

			int lcount = 0;
			start = this->lines.begin();
			for(unsigned int c=0; c < this->start_line; c++)
				start++;
			end = start;
			for(int c=0; c < CONSOLE_LINE_COUNT; c++)
			{
				if(end == this->lines.end())
					break;
				end++;
			}
			for(i=start; i!=end; i++)
				text += (*i)+"\n";

			//add the this->prompt
			String tempPrompt = this->cursor;
			if(this->prompt.length() > 0)
				tempPrompt = (this->prompt.substr(0,this->cursor_pos)+this->cursor+this->prompt.substr(this->cursor_pos,this->prompt.length()));	

			String prefix = ":]";

			if(this->pythonMode)
				prefix = String(ScriptRuntime::backendName()) + ":>";

			text += prefix+tempPrompt;


			this->textbox->setCaption(StringUtil::convertToUTF(text));
			this->update_overlay = false;
		}
		return false;
	}
	//---------------------------------------------------------
	void IngameConsole::messageLogged(const String& message, Ogre::LogMessageLevel lml, bool maskDebug, const String &logName, bool& skipThisMessage)
	{
		if(skipThisMessage) return;
		this->print(logName + ": " + message);
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(IngameConsole)
		OSINGLETON()
		OFUNC(setVisible)
		OFUNC(switchVisible)
	OOBJECT_END
}
