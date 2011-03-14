/********************************************************************
	created:	2010/11/02
	filename:	CcFastGuiFactory.h
	author:		philipp.engelhard
	notice:		
	copyright:	(c) 2010 kunst-stoff
*********************************************************************/

#include "engine_fastgui/FastGuiFactory.h"
#include "CcFastGuiCoolDownButton.h"
#include <engine_fastgui/FastGuiManager.h>
//#include <engine_fastgui/FastGuiLabel.h>
// #include "core_event/EventManager.h"
// #include "core_event/EventType.h"
// #include "core_event/EventListener.h"
// #include "core_event/EventHandler.h"
// #include <core_event/Event.h>

#ifndef __CcFastGuiFactory_h__
#define __CcFastGuiFactory_h__

namespace CC
{

	class CcFastGuiFactory : public Orkige::FastGuiFactory, public Orkige::EventHandler
	{
		//--- Types -------------------------------------------------
	public:
		typedef Orkige::FastGuiFactory super;
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		std::vector<woptr<CcFastGuiCoolDownButton> > coolDownButtons;
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		CcFastGuiFactory();
		//! destructor
		virtual ~CcFastGuiFactory();

		//! create a CoolDownButton
		woptr<CcFastGuiCoolDownButton> createCoolDownButton(Orkige::String const & id, Orkige::String const & spriteName, unsigned char defaultGlyphIndex, Orkige::String const & text, Ogre::Vector2 const & position, Orkige::FastGuiLabel::LabelAlignment textAlignment = Orkige::FastGuiLabel::LA_CENTER, Ogre::Vector2 const & size = Ogre::Vector2::ZERO, Orkige::String const & atlas = Orkige::StringUtil::BLANK, unsigned char z = 0);

	protected:
		bool onFrameStarted(Orkige::Event const & event);
		//! overrid of mothod for loading a widget with given typename and settings
		void onLoadWidget(Orkige::String const & widgetType, SettingsMultiMap* settings);
		//! overrid of load a CoolDownButton
		void onLoadCoolDownButton(Orkige::String const & id, SettingsMultiMap* settings);
		bool onFreezDragDrop(Orkige::Event const & event);
		bool onUnfreezDragDrop(Orkige::Event const & event);
	private:
	};
}

#endif //__CcFastGuiFactory_h__