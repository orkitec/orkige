/********************************************************************
	created:	Wednesday 2010/10/27 at 13:18
	filename: 	FastGuiDecorWidget.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
#ifndef __FastGuiDecorWidget_h__27_10_2010__13_18_38__
#define __FastGuiDecorWidget_h__27_10_2010__13_18_38__

#include "engine_fastgui/FastGuiWidget.h"

namespace Orkige
{
	class ORKIGE_ENGINE_DLL FastGuiDecorWidget : public FastGuiWidget
	{
		OOBJECT(FastGuiDecorWidget, FastGuiWidget);
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Gorilla::Rectangle* rect;
	private:
		//--- Methods -----------------------------------------------
	public:
		FastGuiDecorWidget(String const & id, String const & spriteName, Ogre::Vector2 const & position, Ogre::Vector2 const & size, String const & atlas, uint z);
		virtual ~FastGuiDecorWidget();

		virtual void setPosition(Ogre::Real left, Ogre::Real top);
		virtual void setSize(Ogre::Real width, Ogre::Real height);
		virtual Ogre::Vector2 getSize();
		virtual Ogre::Vector2 getPosition();

		void setSprite(String const & spriteName);
		inline Gorilla::Rectangle* getRectangle();
	protected:
	private:
	};
	//---------------------------------------------------------------
	inline Gorilla::Rectangle* FastGuiDecorWidget::getRectangle()
	{
		return this->rect;
	}
	//---------------------------------------------------------------
}

#endif //__FastGuiDecorWidget_h__27_10_2010__13_18_38__