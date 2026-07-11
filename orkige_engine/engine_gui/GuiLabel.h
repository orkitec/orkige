/********************************************************************
	created:	Friday 2010/10/29 at 18:16
	filename: 	GuiLabel.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
#ifndef __GuiLabel_h__29_10_2010__18_16_51__
#define __GuiLabel_h__29_10_2010__18_16_51__

#include "engine_gui/GuiWidget.h"

namespace Orkige
{
	class ORKIGE_ENGINE_DLL GuiLabel : public GuiWidget
	{
		OOBJECT(GuiLabel, GuiWidget);
		//--- Types -------------------------------------------------
	public:
		//! enumerator values for label text alignment
		enum LabelAlignment
		{
			LA_TOPLEFT = 0,
			LA_TOP,
			LA_TOPRIGHT,
			LA_LEFT,
			LA_CENTER,
			LA_RIGHT,
			LA_BOTTOMLEFT,
			LA_BOTTOM,
			LA_BOTTOMRIGHT,
		};
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		UiCaption* caption;	//!< holds the text
	private:
		//--- Methods -----------------------------------------------
	public:
		GuiLabel(String const & id, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, String const & atlas, uint z, bool scaled);
		virtual ~GuiLabel();

		virtual void setPosition(Ogre::Real left, Ogre::Real top);
		virtual void setSize(Ogre::Real width, Ogre::Real height);
		virtual Ogre::Vector2 getSize();
		virtual Ogre::Vector2 getPosition();
		//! set label text
		void setText(String const & text);
		//! set text alignment inside the label
		void setAlignment(LabelAlignment alignment);
		//! @brief set the text opacity 0..1 (keeps the current colour); used to
		//! dim a label when its owning widget is disabled
		void setAlpha(float alpha);
		//! get gorilla Caption
		inline UiCaption* getCaption();
	protected:
	private:
	};
	//---------------------------------------------------------------
	inline UiCaption* GuiLabel::getCaption()
	{
		return this->caption;
	}
}

#endif //__GuiLabel_h__29_10_2010__18_16_51__