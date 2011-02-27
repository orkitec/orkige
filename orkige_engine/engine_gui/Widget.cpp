/********************************************************************
	created:	Wednesday 2010/08/04 at 15:09
	filename: 	Widget.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_gui/Widget.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	Widget::~Widget()
	{

	}
	//---------------------------------------------------------
	void Widget::cleanup()
	{
		if (this->overlayElement) 
		{
			OverlayUtil::nukeOverlayElement(this->overlayElement);
		}
		this->overlayElement = 0;
	}
	//---------------------------------------------------------
	Ogre::OverlayElement* Widget::getOverlayElement()
	{
		return this->overlayElement;
	}
	//---------------------------------------------------------
	String const & Widget::getName()
	{
		return this->overlayElement->getName();
	}
	//---------------------------------------------------------
	Widget::TrayLocation Widget::getTrayLocation()
	{
		return this->trayLocation;
	}
	//---------------------------------------------------------
	void Widget::hide()
	{
		this->overlayElement->hide();
	}
	//---------------------------------------------------------
	void Widget::show()
	{
		this->overlayElement->show();
	}
	//---------------------------------------------------------
	bool Widget::isVisible()
	{
		return this->overlayElement->isVisible();
	}
	//---------------------------------------------------------
	void Widget::onFocusLost() 
	{

	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	Widget::Widget(String const & name, String const & _materialGroup) : IGuiObject(name), materialGroup(_materialGroup)
	{
		this->trayLocation = TL_NONE;
		this->overlayElement = 0;
	}
	//---------------------------------------------------------
	void Widget::_assignToTray(TrayLocation trayLoc) 
	{ 
		this->trayLocation = trayLoc; 
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(Widget)
	OOBJECT_END
}