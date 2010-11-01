/********************************************************************
	created:	Monday 2010/11/01 at 13:44
	filename: 	FastGuiTextbox.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
*********************************************************************/
#ifndef __FastGuiTextbox_h__1_11_2010__13_44_54__
#define __FastGuiTextbox_h__1_11_2010__13_44_54__

#include "engine_fastgui/FastGuiWidget.h"

namespace Orkige
{
	class FastGuiTextbox : public FastGuiWidget
	{
		OOBJECT(FastGuiTextbox, FastGuiWidget);
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Gorilla::MarkupText* markupText;
	private:
		//--- Methods -----------------------------------------------
	public:
		FastGuiTextbox(String const & id, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, String const & atlas, uint z);
		virtual ~FastGuiTextbox();

		virtual void setPosition(Ogre::Real left, Ogre::Real top);
		virtual void setSize(Ogre::Real width, Ogre::Real height);
		virtual Ogre::Vector2 getSize();
		virtual Ogre::Vector2 getPosition();
		//! set box text
		void setText(String const & text);
		//! get gorilla Caption
		inline Gorilla::MarkupText* getMarkupText();
	protected:
	private:
	};
	//---------------------------------------------------------------
	inline Gorilla::MarkupText* FastGuiTextbox::getMarkupText()
	{
		return this->markupText;
	}
	//---------------------------------------------------------------
}

#endif //__FastGuiTextbox_h__1_11_2010__13_44_54__