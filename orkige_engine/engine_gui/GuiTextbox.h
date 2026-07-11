/********************************************************************
	created:	Monday 2010/11/01 at 13:44
	filename: 	GuiTextbox.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
#ifndef __GuiTextbox_h__1_11_2010__13_44_54__
#define __GuiTextbox_h__1_11_2010__13_44_54__

#include "engine_gui/GuiWidget.h"

namespace Orkige
{
	class ORKIGE_ENGINE_DLL GuiTextbox : public GuiWidget
	{
		OOBJECT(GuiTextbox, GuiWidget);
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		UiMarkupText* markupText;
	private:
		//--- Methods -----------------------------------------------
	public:
		GuiTextbox(String const & id, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, String const & atlas, uint z, bool scaled);
		virtual ~GuiTextbox();

		virtual void setPosition(Ogre::Real left, Ogre::Real top);
		virtual void setSize(Ogre::Real width, Ogre::Real height);
		virtual Ogre::Vector2 getSize();
		virtual Ogre::Vector2 getPosition();
		//! set box text
		void setText(String const & text);
		//! get gorilla Caption
		inline UiMarkupText* getMarkupText();
		virtual void applyRenderTransform(Ui2DTransform const & transform);
		virtual void applyRenderAlpha(float alphaMultiplier);
	protected:
	private:
	};
	//---------------------------------------------------------------
	inline UiMarkupText* GuiTextbox::getMarkupText()
	{
		return this->markupText;
	}
	//---------------------------------------------------------------
}

#endif //__GuiTextbox_h__1_11_2010__13_44_54__