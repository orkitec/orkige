/********************************************************************
	created:	Friday 2010/08/06 at 19:01
	filename: 	GuiFactory.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef __GuiFactory_h__6_8_2010__19_01_19__
#define __GuiFactory_h__6_8_2010__19_01_19__

#include "engine_gui/Widget.h"
#include "engine_gui/Button.h"
#include "engine_gui/SelectMenu.h"
#include "engine_gui/Label.h"
#include "engine_gui/Slider.h"
#include "engine_gui/CheckBox.h"
#include "engine_gui/TextBox.h"
#include "engine_gui/ParamsPanel.h"
#include "engine_gui/DecorWidget.h"
#include "engine_gui/ProgressBar.h"
#include "engine_gui/Separator.h"
#include "engine_gui/Dialog.h"
#include "engine_gui/OkDialog.h"
#include "engine_gui/YesNoDialog.h"

namespace Orkige
{
	/** \addtogroup Gui
	*  @{ */
	//! Default Gui factory
	class GuiFactory : public Interface
	{
		OOBJECT(GuiFactory, Interface);
		friend class GuiManager;
		//--- Types -------------------------------------------------
	public:
		//! @brief Default Dialogtypes
		//! @see Dialog::DialogType
		enum DialogTypes
		{
			DT_OK,
			DT_YESNO
		};
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		GuiFactory();
		//! destructor
		virtual ~GuiFactory();

		/*! \addtogroup GuiElementFactory
		*  factory methods for gui elements
		*  @{
		*/
		//! create a Button
		virtual Button* createButton(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, String const & caption, Ogre::Real width = 0);
		//! create a TextBox
		virtual TextBox* createTextBox(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width, Ogre::Real height);
		//! create a SelectMenu
		virtual SelectMenu* createThickSelectMenu(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width, unsigned int maxItemsShown, const Ogre::StringVector& items = Ogre::StringVector());
		//! create a SelectMenu
		virtual SelectMenu* createLongSelectMenu(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width, Ogre::Real boxWidth, unsigned int maxItemsShown, const Ogre::StringVector& items = Ogre::StringVector());
		//! create a SelectMenu
		virtual SelectMenu* createLongSelectMenu(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real boxWidth, unsigned int maxItemsShown, const Ogre::StringVector& items = Ogre::StringVector());
		//! create a Label
		virtual Label* createLabel(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width = 0);
		//! create a Separator
		virtual Separator* createSeparator(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, Ogre::Real width = 0);
		//! create a Slider
		virtual Slider* createThickSlider(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width, Ogre::Real valueBoxWidth, Ogre::Real minValue, Ogre::Real maxValue, unsigned int snaps);
		//! create a Slider
		virtual Slider* createLongSlider(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width, Ogre::Real trackWidth, Ogre::Real valueBoxWidth, Ogre::Real minValue, Ogre::Real maxValue, unsigned int snaps);
		//! create a Slider
		virtual Slider* createLongSlider(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real trackWidth, Ogre::Real valueBoxWidth, Ogre::Real minValue, Ogre::Real maxValue, unsigned int snaps);
		//! create a ParamsPanel
		virtual ParamsPanel* createParamsPanel(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, Ogre::Real width, unsigned int lines);
		//! create a ParamsPanel
		virtual ParamsPanel* createParamsPanel(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, Ogre::Real width, const Ogre::StringVector& paramNames);
		//! create a CheckBox
		virtual CheckBox* createCheckBox(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width = 0);
		//! create a DecorWidget
		virtual DecorWidget* createDecorWidget(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, String const & templateName);
		//! create a ProgressBar
		virtual ProgressBar* createProgressBar(Widget::TrayLocation trayLoc, String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width, Ogre::Real commentBoxWidth);
		/*! @} */
	protected:
		//! create a Dialog
		virtual Dialog* createDialog(Dialog::DialogType type, String const & name, String const & materialGroup, const Ogre::DisplayString& caption, const Ogre::DisplayString& text, const Ogre::DisplayString& button1text,const Ogre::DisplayString& button2text);
	private:
	};
	/** @} End of "addtogroup Gui"*/
	//---------------------------------------------------------------
}

#endif //__GuiFactory_h__6_8_2010__19_01_19__