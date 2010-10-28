/********************************************************************
	created:	Wednesday 2010/10/27 at 13:19
	filename: 	FastGuiDecorWidget.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_fastgui/FastGuiDecorWidget.h"
#include "engine_fastgui/FastGuiManager.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FastGuiDecorWidget::FastGuiDecorWidget(String const & id, String const & spriteName, Ogre::Vector2 const & position, Ogre::Vector2 const & size, String const & atlas, uint z) : FastGuiWidget(id)
	{
		optr<FastGuiView> view = FastGuiManager::getSingleton().getCreateView(atlas).lock();
		oAssert(view);
		Gorilla::Layer* layer = view->getLayer(z);
		this->rect = layer->createRectangle(position, size);
		this->rect->background_image(spriteName);
	}
	//---------------------------------------------------------
	FastGuiDecorWidget::~FastGuiDecorWidget()
	{
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(FastGuiDecorWidget)
	OOBJECT_END
}