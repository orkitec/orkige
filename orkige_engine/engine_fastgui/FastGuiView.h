/********************************************************************
	created:	Friday 2010/10/29 at 18:16
	filename: 	FastGuiView.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
*********************************************************************/
#ifndef __FastGuiView_h__29_10_2010__18_16_35__
#define __FastGuiView_h__29_10_2010__18_16_35__

#include "engine_fastgui/Gorilla.h"
#include <core_base/Interface.h>

namespace Orkige
{
	//! handles one Gorilla::Screen and its layers (used by FastGuiManager)
	class FastGuiView : public Interface
	{
		OOBJECT(FastGuiView, Interface);
		//--- Types -------------------------------------------------
	public:
		enum Alignment
		{
			VA_TOPLEFT = 0,
			VA_TOP,
			VA_TOPRIGHT,
			VA_LEFT,
			VA_CENTER,
			VA_RIGHT,
			VA_BOTTOMLEFT,
			VA_BOTTOM,
			VA_BOTTOMRIGHT,
		};
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		Gorilla::Screen* screen;				//!< actual screen (1atlas)
		std::map<uint, Gorilla::Layer*> layers;	//!< all z layers of this screen
		//--- Methods -----------------------------------------------
	public:
		//! constrcutor
		FastGuiView(Gorilla::Screen* _screen);
		//! destructor
		virtual ~FastGuiView();
		//! get or create layer at given index
		inline Gorilla::Layer* getLayer(uint z);
		//! get the gorilla screen for this view
		inline Gorilla::Screen* getScreen();
		//! get coordimate for given alignment
		Ogre::Vector2 getPosition(FastGuiView::Alignment alignment);
	protected:
	private:
	};
	//---------------------------------------------------------------
	inline Gorilla::Layer* FastGuiView::getLayer(uint z)
	{
		std::map<Ogre::uint, Gorilla::Layer*>::iterator it  = this->layers.find(z);
		if(it != this->layers.end())
		{
			return it->second;
		}
		Gorilla::Layer* layer = screen->createLayer(z);
		this->layers[z] = layer;
		return layer;
	}
	//---------------------------------------------------------------
	inline Gorilla::Screen* FastGuiView::getScreen()
	{
		return this->screen;
	}
	//---------------------------------------------------------------
}

#endif //__FastGuiView_h__29_10_2010__18_16_35__