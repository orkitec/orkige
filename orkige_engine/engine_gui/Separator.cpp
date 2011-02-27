/********************************************************************
	created:	Wednesday 2010/08/04 at 15:09
	filename: 	Separator.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/

#include "engine_gui/Separator.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	Separator::Separator(String const & name, String const & materialGroup, Ogre::Real width) : Widget(name, materialGroup)
	{
		this->overlayElement = Ogre::OverlayManager::getSingleton().createOverlayElementFromTemplate(this->materialGroup + "/Separator", "Panel", name);
		if (width <= 0) 
		{
			this->fitToTray = true;
		}
		else
		{
			this->fitToTray = false;
			this->overlayElement->setWidth(width);
		}
	}
	//---------------------------------------------------------
	bool Separator::_isFitToTray()
	{
		return this->fitToTray;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(Separator)
	OOBJECT_END
}