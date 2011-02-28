/********************************************************************
	created:	Friday 2010/10/29 at 18:16
	filename: 	FastGuiView.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
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
		uint z;
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		FastGuiView(Gorilla::Screen* _screen, uint _z = 0);
		FastGuiView(FastGuiView const & other);
		//! destructor
		virtual ~FastGuiView();
		//! get or create layer at given index
		inline Gorilla::Layer* getLayer(uint z);
		//! get the gorilla screen for this view
		inline Gorilla::Screen* getScreen();
		//! get coordimate for given alignment
		Ogre::Vector2 getPosition(FastGuiView::Alignment alignment);
		//! set view z value (this doesn't reorder the view rendering immediately you have to call fastGuiManager::getSingleton().reorderViews();)
		inline void setZ(uint z);
		//! for priority sorting
		inline bool operator < (FastGuiView const & other) const;
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
	inline void FastGuiView::setZ(uint z)
	{
		this->z = z;
	}
	//---------------------------------------------------------------
	inline bool FastGuiView::operator < (FastGuiView  const & other) const 
	{
		return this->z < other.z;
	}
}

#endif //__FastGuiView_h__29_10_2010__18_16_35__