/********************************************************************
	created:	Friday 2010/08/06 at 19:01
	filename: 	GuiFactory.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
*********************************************************************/

#include "engine_gui/GuiFactory.h"
#include "engine_gui/GuiManager.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	GuiFactory::GuiFactory()
	{

	}
	//---------------------------------------------------------
	GuiFactory::~GuiFactory()
	{

	}
	//---------------------------------------------------------
	Button* GuiFactory::createButton(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, String const & caption, Ogre::Real width)
	{
		Button* b = new Button(name, materialGroup, caption, width);
		GuiManager::getSingleton().moveWidgetToTray(b, trayLoc);
		return b;
	}
	//---------------------------------------------------------
	TextBox* GuiFactory::createTextBox(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width, Ogre::Real height)
	{
		TextBox* tb = new TextBox(name, materialGroup, caption, width, height);
		GuiManager::getSingleton().moveWidgetToTray(tb, trayLoc);
		return tb;
	}
	//---------------------------------------------------------
	SelectMenu* GuiFactory::createThickSelectMenu(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width, unsigned int maxItemsShown, const Ogre::StringVector& items)
	{
		SelectMenu* sm = new SelectMenu(name, materialGroup, caption, width, 0, maxItemsShown);
		GuiManager::getSingleton().moveWidgetToTray(sm, trayLoc);
		if (!items.empty()) sm->setItems(items);
		return sm;
	}
	//---------------------------------------------------------
	SelectMenu* GuiFactory::createLongSelectMenu(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, const Ogre::DisplayString& caption,
		Ogre::Real width, Ogre::Real boxWidth, unsigned int maxItemsShown, const Ogre::StringVector& items)
	{
		SelectMenu* sm = new SelectMenu(name, materialGroup, caption, width, boxWidth, maxItemsShown);
		GuiManager::getSingleton().moveWidgetToTray(sm, trayLoc);
		if (!items.empty()) sm->setItems(items);
		return sm;
	}
	//---------------------------------------------------------
	SelectMenu* GuiFactory::createLongSelectMenu(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real boxWidth, unsigned int maxItemsShown, const Ogre::StringVector& items)
	{
		return this->createLongSelectMenu(trayLoc, name, materialGroup, caption, 0, boxWidth, maxItemsShown, items);
	}
	//---------------------------------------------------------
	Label* GuiFactory::createLabel(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width)
	{
		Label* l = new Label(name, materialGroup, caption, width);
		GuiManager::getSingleton().moveWidgetToTray(l, trayLoc);
		return l;
	}
	//---------------------------------------------------------
	Separator* GuiFactory::createSeparator(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, Ogre::Real width)
	{
		Separator* s = new Separator(name, materialGroup, width);
		GuiManager::getSingleton().moveWidgetToTray(s, trayLoc);
		return s;
	}
	//---------------------------------------------------------
	Slider* GuiFactory::createThickSlider(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, const Ogre::DisplayString& caption,
		Ogre::Real width, Ogre::Real valueBoxWidth, Ogre::Real minValue, Ogre::Real maxValue, unsigned int snaps)
	{
		Slider* s = new Slider(name, materialGroup, caption, width, 0, valueBoxWidth, minValue, maxValue, snaps);
		GuiManager::getSingleton().moveWidgetToTray(s, trayLoc);
		return s;
	}
	//---------------------------------------------------------
	Slider* GuiFactory::createLongSlider(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width,
		Ogre::Real trackWidth, Ogre::Real valueBoxWidth, Ogre::Real minValue, Ogre::Real maxValue, unsigned int snaps)
	{
		if (trackWidth <= 0) trackWidth = 1;
		Slider* s = new Slider(name, materialGroup, caption, width, trackWidth, valueBoxWidth, minValue, maxValue, snaps);
		GuiManager::getSingleton().moveWidgetToTray(s, trayLoc);
		return s;
	}
	//---------------------------------------------------------
	Slider* GuiFactory::createLongSlider(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, const Ogre::DisplayString& caption,
		Ogre::Real trackWidth, Ogre::Real valueBoxWidth, Ogre::Real minValue, Ogre::Real maxValue, unsigned int snaps)
	{
		return this->createLongSlider(trayLoc, name, materialGroup, caption, 0, trackWidth, valueBoxWidth, minValue, maxValue, snaps);
	}
	//---------------------------------------------------------
	ParamsPanel* GuiFactory::createParamsPanel(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, Ogre::Real width, unsigned int lines)
	{
		ParamsPanel* pp = new ParamsPanel(name, materialGroup, width, lines);
		GuiManager::getSingleton().moveWidgetToTray(pp, trayLoc);
		return pp;
	}
	//---------------------------------------------------------
	ParamsPanel* GuiFactory::createParamsPanel(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, Ogre::Real width, const Ogre::StringVector& paramNames)
	{
		ParamsPanel* pp = new ParamsPanel(name, materialGroup, width, paramNames.size());
		pp->setAllParamNames(paramNames);
		GuiManager::getSingleton().moveWidgetToTray(pp, trayLoc);
		return pp;
	}
	//---------------------------------------------------------
	CheckBox* GuiFactory::createCheckBox(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width)
	{
		CheckBox* cb = new CheckBox(name, materialGroup, caption, width);
		GuiManager::getSingleton().moveWidgetToTray(cb, trayLoc);
		return cb;
	}
	//---------------------------------------------------------
	DecorWidget* GuiFactory::createDecorWidget(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, String const & templateName)
	{
		DecorWidget* dw = new DecorWidget(name, materialGroup, templateName);
		GuiManager::getSingleton().moveWidgetToTray(dw, trayLoc);
		return dw;
	}
	//---------------------------------------------------------
	ProgressBar* GuiFactory::createProgressBar(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, const Ogre::DisplayString& caption,
		Ogre::Real width, Ogre::Real commentBoxWidth)
	{
		ProgressBar* pb = new ProgressBar(name, materialGroup, caption, width, commentBoxWidth);
		GuiManager::getSingleton().moveWidgetToTray(pb, trayLoc);
		return pb;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	Dialog* GuiFactory::createDialog(Dialog::DialogType type, String const & name, String const & materialGroup, const Ogre::DisplayString& caption, const Ogre::DisplayString& text, const Ogre::DisplayString& button1text,const Ogre::DisplayString& button2text)
	{
		switch(type)
		{
		case DT_OK:	return new OkDialog(type, name, materialGroup, caption, text, button1text); break;
		case DT_YESNO:	return new YesNoDialog(type, name, materialGroup, caption, text, button1text, button2text); break;
		}
		return NULL;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(GuiFactory)
	OOBJECT_END
}