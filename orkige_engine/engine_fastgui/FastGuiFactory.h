/********************************************************************
	created:	Wednesday 2010/10/27 at 13:09
	filename: 	FastGuiFactory.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
#ifndef __FastGuiFactory_h__27_10_2010__13_09_02__
#define __FastGuiFactory_h__27_10_2010__13_09_02__

#include "engine_fastgui/FastGuiWidget.h"
#include "engine_fastgui/FastGuiDecorWidget.h"
#include "engine_fastgui/FastGuiButton.h"
#include "engine_fastgui/FastGuiTextbox.h"
#include "engine_fastgui/FastGuiCheckBox.h"
#include "engine_fastgui/FastGuiSelectMenu.h"
#include "engine_fastgui/FastGuiDragDropButton.h"
#include "engine_fastgui/FastGuiProgressBar.h"
#include "engine_fastgui/FastGuiButtonBlink.h"
#include "engine_fastgui/FastGuiSlider.h"
#include <core_util/StringUtil.h>

#include <OgreConfigFile.h>


namespace Orkige
{
	class ORKIGE_ENGINE_DLL FastGuiFactory : public Ogre::ConfigFile
	{
		//--- Types -------------------------------------------------
	public:
	protected:
		//! support struct that holds some values that many widgets have in common
		//! @see getBaseWidgetSettings
		struct BasicWidgetSettings
		{
			String atlas;
			String sprite;
			String text;
			uint defaultGlyphIndex;
			Ogre::Vector2 position;
			Ogre::Vector2 size;
			uint z;
			FastGuiView::Alignment alignment;
		};
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		String resourceGroup;
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		FastGuiFactory();
		//! destructor
		virtual ~FastGuiFactory();
		//! create a simple decor widget
		virtual woptr<FastGuiDecorWidget> createDecorWidget(String const & id, String const & spriteName, Ogre::Vector2 const & position, Ogre::Vector2 const & size, String const & atlas, uint z);
		//! create a simple label
		virtual woptr<FastGuiLabel> createLabel(String const & id, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, String const & atlas, uint z, bool scaled);
		//! create a simple Textbox
		virtual woptr<FastGuiTextbox> createTextbox(String const & id, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, String const & atlas, uint z, bool scaled);
		//! create a simple Button
		virtual woptr<FastGuiButton> createButton(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, FastGuiLabel::LabelAlignment textAlignment = FastGuiLabel::LA_CENTER, Ogre::Vector2 const & size = Ogre::Vector2::ZERO, String const & atlas = StringUtil::BLANK, uint z = 0, bool _nostate = false, FastGuiButtonBlink::ButtonBlinkState blinkState = FastGuiButtonBlink::BBLINK_NONE);
		//! create a simple CheckBox
		virtual woptr<FastGuiCheckBox> createCheckBox(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, FastGuiLabel::LabelAlignment textAlignment = FastGuiLabel::LA_CENTER, Ogre::Vector2 const & size = Ogre::Vector2::ZERO, String const & atlas = StringUtil::BLANK, uint z = 0, bool useCheckbox = false);
		//! create a simple decor SelectMenu
		virtual woptr<FastGuiSelectMenu> createSelectMenu(String const & id, String const & buttonId, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position,FastGuiLabel::LabelAlignment textAlignment = FastGuiLabel::LA_TOP, Ogre::Vector2 const & size = Ogre::Vector2::ZERO, String const & atlas = StringUtil::BLANK, uint z = 0);
		//! create a Drag and Drop Button
		virtual woptr<FastGuiDragDropButton> createDragDropButton(String const & id, String const & spriteName, unsigned char defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, FastGuiLabel::LabelAlignment textAlignment = FastGuiLabel::LA_CENTER, Ogre::Vector2 const & size = Ogre::Vector2::ZERO, String const & atlas = StringUtil::BLANK, unsigned char z = 0);
		//! create a simple ProgressBar
		virtual woptr<FastGuiProgressBar> createProgressBar(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, FastGuiLabel::LabelAlignment textAlignment = FastGuiLabel::LA_CENTER, Ogre::Vector2 const & size = Ogre::Vector2::ZERO, String const & atlas = StringUtil::BLANK, uint z = 0);
		//! create a simple decor SelectMenu
		virtual woptr<FastGuiSlider> createSlider(String const & id, String const & buttonId, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position,FastGuiLabel::LabelAlignment textAlignment = FastGuiLabel::LA_TOP, Ogre::Vector2 const & size = Ogre::Vector2::ZERO, String const & atlas = StringUtil::BLANK, uint z = 0);

		
		virtual void load(String const & filename);
	protected:
		//! overridable mothod for loading global settings
		virtual void onLoadGlobalSettings(SettingsMultiMap* settings);
		//! overridable mothod for loading a widget with given typename and settings
		virtual void onLoadWidget(String const & widgetType, SettingsMultiMap* settings);
		//! utility to load common ui settings @see BasicWidgetSettings
		BasicWidgetSettings getBaseWidgetSettings(SettingsMultiMap* settings);
		//! overridable to load a DecorWidget
		virtual void onLoadDecorWidget(String const & id, SettingsMultiMap* settings);
		//! overridable to load a Label
		virtual void onLoadLabel(String const & id, SettingsMultiMap* settings);
		//! overridable to load a Textbox
		virtual void onLoadTextbox(String const & id, SettingsMultiMap* settings);
		//! overridable to load a Button
		virtual void onLoadButton(String const & id, SettingsMultiMap* settings);
		//! overridable to load a CheckBox
		virtual void onLoadCheckBox(String const & id, SettingsMultiMap* settings);
		//! overridable to load a DragDropButton
		virtual void onLoadDragDropButton(String const & id, SettingsMultiMap* settings);
		//! overridable to load a SelectMenu
		virtual void onLoadSelectMenu(String const & id, SettingsMultiMap* settings);
		//! overridable to load a ProgressBar
		virtual void onLoadProgressBar(String const & id, SettingsMultiMap* settings);
		//! overridable to load a Slider
		virtual void onLoadSlider(String const & id, SettingsMultiMap* settings);
	private:
	};
	//---------------------------------------------------------------
}

#endif //__FastGuiFactory_h__27_10_2010__13_09_02__