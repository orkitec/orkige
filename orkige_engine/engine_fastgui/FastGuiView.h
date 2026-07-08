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

#include "engine_fastgui/UiRenderer.h"
#include <core_base/Interface.h>

namespace Orkige
{
	//! handles one UiScreen (= one atlas) and its layers, and OWNS the
	//! screen (used by FastGuiManager)
	class ORKIGE_ENGINE_DLL FastGuiView : public Interface
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
		UiScreen* screen;					//!< actual screen (1 atlas), owned
		std::map<uint, UiLayer*> layers;	//!< all z layers of this screen
		uint z;
		//--- Methods -----------------------------------------------
	public:
		//! constructor - takes ownership of the screen
		FastGuiView(UiScreen* _screen, uint _z = 0);
		//! destructor - destroys the layers and the screen
		virtual ~FastGuiView();
		//! get or create layer at given index
		inline UiLayer* getLayer(uint z);
		//! get the screen of this view
		inline UiScreen* getScreen();
		//! get coordimate for given alignment
		Ogre::Vector2 getPosition(FastGuiView::Alignment alignment);
		//! set view z value (this doesn't reorder the view rendering immediately you have to call fastGuiManager::getSingleton().reorderViews();)
		inline void setZ(uint z);
		//! for priority sorting
		inline bool operator < (FastGuiView const & other) const;
	protected:
	private:
		FastGuiView(FastGuiView const &);				// non-copyable (owns the screen)
		FastGuiView & operator=(FastGuiView const &);	// non-copyable
	};
	//---------------------------------------------------------------
	inline UiLayer* FastGuiView::getLayer(uint z)
	{
		std::map<uint, UiLayer*>::iterator it  = this->layers.find(z);
		if(it != this->layers.end())
		{
			return it->second;
		}
		UiLayer* layer = screen->createLayer(z);
		this->layers[z] = layer;
		return layer;
	}
	//---------------------------------------------------------------
	inline UiScreen* FastGuiView::getScreen()
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
	//---------------------------------------------------------------
	struct FastGuiViewOptrCmp
	{
		inline bool operator()( optr<FastGuiView> const & lhs, optr<FastGuiView> const & rhs)
		{
			return *lhs < *rhs;
		}
	};
	//---------------------------------------------------------------
}

#endif //__FastGuiView_h__29_10_2010__18_16_35__
