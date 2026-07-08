/********************************************************************
	created:	Wednesday 2010/10/27 at 13:08
	filename: 	FastGuiWidget.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
#ifndef __FastGuiWidget_h__27_10_2010__13_08_39__
#define __FastGuiWidget_h__27_10_2010__13_08_39__

#include "engine_fastgui/IGuiObject.h"
#include "engine_fastgui/FastGuiView.h"
#include "engine_fastgui/Gorilla.h"

namespace Orkige
{
	namespace Colours
	{
		typedef Gorilla::Colours::Colour Colour;
		using namespace Gorilla::Colours;
		using Gorilla::webcolour;
		using Gorilla::rgb;
	};

	class ORKIGE_ENGINE_DLL FastGuiWidget : public IGuiObject
	{
		OOBJECT(FastGuiWidget, IGuiObject);
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		Gorilla::Layer* layer;
		woptr<FastGuiView> view;
	private:
		bool visible;
		//--- Methods -----------------------------------------------
	public:
		FastGuiWidget(String const & id, String const & atlas, uint z);
		virtual ~FastGuiWidget();

		//! set position of this widget
		virtual void setPosition(Ogre::Real left, Ogre::Real top) = 0;
		//! set size of this widget
		virtual void setSize(Ogre::Real width, Ogre::Real height) = 0;
		//! get size of this widget
		virtual Ogre::Vector2 getSize() = 0;
		//! get position of this widget
		virtual Ogre::Vector2 getPosition() = 0;
		//! get layer this widget is in
		inline Gorilla::Layer* getLayer() const;
		//! get the view of this layer
		inline woptr<FastGuiView> getView();
		//! center widget horizontally on the screen
		void centerHorizontal();
		
		//! show or hide 
		//void setVisibility(bool enable);
		//bool getVisibility();
		//! for priority sorting
		inline bool operator < (FastGuiWidget const & other) const;
	protected:
	private:
	};
	//---------------------------------------------------------------
	inline Gorilla::Layer* FastGuiWidget::getLayer() const
	{
		return this->layer;
	}
	//---------------------------------------------------------------
	inline woptr<FastGuiView> FastGuiWidget::getView()
	{
		return this->view;
	}
	//---------------------------------------------------------------
	inline bool FastGuiWidget::operator < (FastGuiWidget  const & other) const 
	{
		return this->getLayer()->getIndex() > other.getLayer()->getIndex();
	}
	//---------------------------------------------------------------
	struct FastGuiWidgetOptrCmp
	{
		inline bool operator()( optr<FastGuiWidget> const & lhs, optr<FastGuiWidget> const & rhs)
		{
			return *lhs < *rhs;
		}
	};
}

#endif //__FastGuiWidget_h__27_10_2010__13_08_39__