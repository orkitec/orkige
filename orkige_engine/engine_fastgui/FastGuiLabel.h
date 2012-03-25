/********************************************************************
	created:	Friday 2010/10/29 at 18:16
	filename: 	FastGuiLabel.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
#ifndef __FastGuiLabel_h__29_10_2010__18_16_51__
#define __FastGuiLabel_h__29_10_2010__18_16_51__

#include "engine_fastgui/FastGuiWidget.h"

namespace Orkige
{
	class ORKIGE_ENGINE_DLL FastGuiLabel : public FastGuiWidget
	{
		OOBJECT(FastGuiLabel, FastGuiWidget);
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
		Gorilla::Caption* caption;	//!< holds the text
	private:
		//--- Methods -----------------------------------------------
	public:
		FastGuiLabel(String const & id, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, String const & atlas, uint z, bool scaled);
		virtual ~FastGuiLabel();

		virtual void setPosition(Ogre::Real left, Ogre::Real top);
		virtual void setSize(Ogre::Real width, Ogre::Real height);
		virtual Ogre::Vector2 getSize();
		virtual Ogre::Vector2 getPosition();
		//! set label text
		void setText(String const & text);
		//! set text alignment inside the label
		void setAlignment(LabelAlignment alignment);
		//! get gorilla Caption
		inline Gorilla::Caption* getCaption();
	protected:
	private:
	};
	//---------------------------------------------------------------
	inline Gorilla::Caption* FastGuiLabel::getCaption()
	{
		return this->caption;
	}
}

#endif //__FastGuiLabel_h__29_10_2010__18_16_51__