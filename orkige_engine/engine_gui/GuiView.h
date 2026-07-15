/********************************************************************
	created:	Friday 2010/10/29 at 18:16
	filename: 	GuiView.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __GuiView_h__29_10_2010__18_16_35__
#define __GuiView_h__29_10_2010__18_16_35__

#include "engine_gui/UiRenderer.h"
#include <core_base/Interface.h>

namespace Orkige
{
	//! handles one UiScreen (= one atlas) and its layers, and OWNS the
	//! screen (used by GuiManager)
	class ORKIGE_ENGINE_DLL GuiView : public Interface
	{
		OOBJECT(GuiView, Interface);
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
		GuiView(UiScreen* _screen, uint _z = 0);
		//! destructor - destroys the layers and the screen
		virtual ~GuiView();
		//! get or create layer at given index
		inline UiLayer* getLayer(uint z);
		//! get the screen of this view
		inline UiScreen* getScreen();
		//! get coordimate for given alignment
		Ogre::Vector2 getPosition(GuiView::Alignment alignment);
		//! set view z value (this doesn't reorder the view rendering immediately you have to call GuiManager::getSingleton().reorderViews();)
		inline void setZ(uint z);
		//! for priority sorting
		inline bool operator < (GuiView const & other) const;
	protected:
	private:
		GuiView(GuiView const &);				// non-copyable (owns the screen)
		GuiView & operator=(GuiView const &);	// non-copyable
	};
	//---------------------------------------------------------------
	inline UiLayer* GuiView::getLayer(uint z)
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
	inline UiScreen* GuiView::getScreen()
	{
		return this->screen;
	}
	//---------------------------------------------------------------
	inline void GuiView::setZ(uint z)
	{
		this->z = z;
	}
	//---------------------------------------------------------------
	inline bool GuiView::operator < (GuiView  const & other) const
	{
		return this->z < other.z;
	}
	//---------------------------------------------------------------
	struct GuiViewOptrCmp
	{
		inline bool operator()( optr<GuiView> const & lhs, optr<GuiView> const & rhs)
		{
			return *lhs < *rhs;
		}
	};
	//---------------------------------------------------------------
}

#endif //__GuiView_h__29_10_2010__18_16_35__
