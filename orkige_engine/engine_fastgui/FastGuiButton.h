/********************************************************************
	created:	Friday 2010/10/29 at 18:16
	filename: 	FastGuiButton.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
#ifndef __FastGuiButton_h__29_10_2010__18_16_18__
#define __FastGuiButton_h__29_10_2010__18_16_18__

#include "engine_fastgui/FastGuiDecorWidget.h"
#include "engine_fastgui/FastGuiLabel.h"

namespace Orkige
{
	class FastGuiButton : public FastGuiWidget
	{
		OOBJECT(FastGuiButton, FastGuiWidget);
		//--- Types -------------------------------------------------
	public:
		//! @brief triggered when a button is released
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(ButtonHitEvent);

		//! enumerator values for button states
		enum ButtonState   
		{
			BS_UP,
			BS_OVER,
			BS_DOWN
		};
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		optr<FastGuiLabel> label;			//!< current button text
		optr<FastGuiDecorWidget> decor;		//!< current button image
		ButtonState state;					//!< current button state
		String baseSpriteName;				//!< base name of the button state sprite;
		bool nostate;
	private:
		//--- Methods -----------------------------------------------
	public:
		FastGuiButton(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, FastGuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z,bool _nostate = false);
		virtual ~FastGuiButton();

		//! get current ButtonState
		const ButtonState& getState();

		virtual void setPosition(Ogre::Real left, Ogre::Real top);
		virtual void setSize(Ogre::Real width, Ogre::Real height);
		virtual Ogre::Vector2 getSize();
		virtual Ogre::Vector2 getPosition();

		virtual void onCursorPressed(Ogre::Vector2 const & cursorPos);
		virtual void onCursorReleased(Ogre::Vector2 const & cursorPos);
		virtual void onCursorMoved(Ogre::Vector2 const & cursorPos);

		//! get text holding ui element
		inline woptr<FastGuiLabel> getLabel();
		//! get image ui element
		inline woptr<FastGuiDecorWidget> getDecor();
		//! get button text
		String getCaption();
		//! set button text
		void setCaption(String const & text);
	protected:
		//! set current ButtonState 
		void setState(const ButtonState& bs);
	private:
	};
	//---------------------------------------------------------------
	inline woptr<FastGuiLabel> FastGuiButton::getLabel()
	{
		return this->label;
	}
	//---------------------------------------------------------------
	inline woptr<FastGuiDecorWidget> FastGuiButton::getDecor()
	{
		return this->decor;
	}
}

#endif //__FastGuiButton_h__29_10_2010__18_16_18__