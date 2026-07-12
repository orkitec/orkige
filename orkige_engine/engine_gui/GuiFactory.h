/********************************************************************
	created:	Wednesday 2010/10/27 at 13:09
	filename: 	GuiFactory.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
#ifndef __GuiFactory_h__27_10_2010__13_09_02__
#define __GuiFactory_h__27_10_2010__13_09_02__

#include "engine_gui/GuiWidget.h"
#include "engine_gui/GuiDecorWidget.h"
#include "engine_gui/GuiButton.h"
#include "engine_gui/GuiTextbox.h"
#include "engine_gui/GuiCheckBox.h"
#include "engine_gui/GuiSelectMenu.h"
#include "engine_gui/GuiDragDropButton.h"
#include "engine_gui/GuiProgressBar.h"
#include "engine_gui/GuiButtonBlink.h"
#include "engine_gui/GuiSlider.h"
#include "engine_gui/GuiTextEntry.h"
#include "engine_gui/GuiScrollView.h"
#include "engine_gui/GuiDropDown.h"
#include <core_util/StringUtil.h>

#include <OgreConfigFile.h>

#include <map>
#include <vector>


namespace Orkige
{
	class ORKIGE_ENGINE_DLL GuiFactory : public Ogre::ConfigFile
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
			GuiView::Alignment alignment;
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
		GuiFactory();
		//! destructor
		virtual ~GuiFactory();
		//! create a simple decor widget
		virtual woptr<GuiDecorWidget> createDecorWidget(String const & id, String const & spriteName, Ogre::Vector2 const & position, Ogre::Vector2 const & size, String const & atlas, uint z);
		//! create a simple label
		virtual woptr<GuiLabel> createLabel(String const & id, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, String const & atlas, uint z, bool scaled);
		//! create a simple Textbox
		virtual woptr<GuiTextbox> createTextbox(String const & id, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, String const & atlas, uint z, bool scaled);
		//! create a simple Button
		virtual woptr<GuiButton> createButton(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment = GuiLabel::LA_CENTER, Ogre::Vector2 const & size = Ogre::Vector2::ZERO, String const & atlas = StringUtil::BLANK, uint z = 0, bool _nostate = false, GuiButtonBlink::ButtonBlinkState blinkState = GuiButtonBlink::BBLINK_NONE);
		//! create a simple CheckBox
		virtual woptr<GuiCheckBox> createCheckBox(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment = GuiLabel::LA_CENTER, Ogre::Vector2 const & size = Ogre::Vector2::ZERO, String const & atlas = StringUtil::BLANK, uint z = 0, bool useCheckbox = false);
		//! create a simple decor SelectMenu
		virtual woptr<GuiSelectMenu> createSelectMenu(String const & id, String const & buttonId, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position,GuiLabel::LabelAlignment textAlignment = GuiLabel::LA_TOP, Ogre::Vector2 const & size = Ogre::Vector2::ZERO, String const & atlas = StringUtil::BLANK, uint z = 0);
		//! create a Drag and Drop Button
		virtual woptr<GuiDragDropButton> createDragDropButton(String const & id, String const & spriteName, unsigned char defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment = GuiLabel::LA_CENTER, Ogre::Vector2 const & size = Ogre::Vector2::ZERO, String const & atlas = StringUtil::BLANK, unsigned char z = 0);
		//! create a simple ProgressBar
		virtual woptr<GuiProgressBar> createProgressBar(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment = GuiLabel::LA_CENTER, Ogre::Vector2 const & size = Ogre::Vector2::ZERO, String const & atlas = StringUtil::BLANK, uint z = 0);
		//! create a simple decor SelectMenu
		virtual woptr<GuiSlider> createSlider(String const & id, String const & buttonId, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position,GuiLabel::LabelAlignment textAlignment = GuiLabel::LA_TOP, Ogre::Vector2 const & size = Ogre::Vector2::ZERO, String const & atlas = StringUtil::BLANK, uint z = 0);
		//! create a single-line TextEntry field (empty sprite/"none" = solid fill;
		//! placeholder shows when empty/unfocused; maxLength 0 = unlimited)
		virtual woptr<GuiTextEntry> createTextEntry(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & placeholder, Ogre::Vector2 const & position, Ogre::Vector2 const & size = Ogre::Vector2::ZERO, String const & atlas = StringUtil::BLANK, uint z = 0, uint maxLength = 0);
		//! create a scroll viewport (a clipping container). Author its content
		//! widgets with the SAME z and parent them under it; a layout child that
		//! is taller/wider than the viewport scrolls by drag / mouse wheel.
		virtual woptr<GuiScrollView> createScrollView(String const & id, Ogre::Vector2 const & position, Ogre::Vector2 const & size, String const & atlas, uint z);
		//! create a dropdown: a button that opens a scrollable option list on a
		//! light-dismiss modal. Set the options with setItems({...}); poll
		//! getSelectedIndex(). Use it for long option sets (the SelectMenu cycler
		//! covers short ones).
		virtual woptr<GuiDropDown> createDropDown(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment = GuiLabel::LA_CENTER, Ogre::Vector2 const & size = Ogre::Vector2::ZERO, String const & atlas = StringUtil::BLANK, uint z = 0);


		//! @brief load a declarative UI layout (.oui) at runtime: creates every
		//! widget it declares (anchors/pivots/offsets, groups, nine-slice,
		//! scroll) and parents them. The backend-neutral successor to load()
		//! (no Ogre material/localisation ties); text routes through StringTable.
		virtual void loadLayout(String const & filename);

		//! @brief hot-reload a `.oui` screen this factory previously loaded: re-read
		//! the fresh file, DESTROY the widgets/modals/toggle-groups the earlier
		//! load created and rebuild from the new content (clean cutover, no state
		//! merge). Clean-cutover safety: the fresh file is parsed FIRST, so a parse
		//! failure leaves the OLD screen intact and returns false with @p error set
		//! (nothing is torn down). @return true on a successful rebuild.
		bool reloadLayout(String const & filename, String & error);

		//! @brief the widget ids the last (re)load of @p filename created, in
		//! declaration order (empty when this factory never loaded it). Lets the
		//! GuiManager refresh a screen router's teardown set after a hot-reload.
		std::vector<String> const & layoutWidgetIds(String const & filename) const;

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
		//! overridable to load a TextEntry
		virtual void onLoadTextEntry(String const & id, SettingsMultiMap* settings);
	private:
		//! @brief what one loaded `.oui` source produced, so reloadLayout can tear
		//! exactly that screen down and rebuild it. Recorded from the doc as the
		//! layout builds (declaration order); a reload overwrites the entry.
		struct LoadedLayout
		{
			std::vector<String> widgetIds;			//!< every widget section id (+ modal scrims)
			std::vector<String> modalIds;			//!< [modal] section ids
			std::vector<String> toggleGroupIds;		//!< [togglegroup] section ids
		};
		//! per-.oui tracking keyed by the exact name passed to loadLayout
		std::map<String, LoadedLayout> loadedLayouts;
		//! @brief the shared read+parse+build path behind loadLayout/reloadLayout.
		//! @param isReload destroy this file's previously-tracked screen first.
		//! @return false (with @p error set) on a read/parse failure, having torn
		//! nothing down (clean cutover). loadLayout asserts on that; reloadLayout
		//! reports it.
		bool loadLayoutImpl(String const & filename, bool isReload, String & error);
	};
	//---------------------------------------------------------------
}

#endif //__GuiFactory_h__27_10_2010__13_09_02__