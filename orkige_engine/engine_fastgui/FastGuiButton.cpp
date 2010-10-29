/********************************************************************
	created:	Friday 2010/10/29 at 18:16
	filename: 	FastGuiButton.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_fastgui/FastGuiButton.h"
#include "engine_fastgui/FastGuiManager.h"

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(FastGuiButton, ButtonHitEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FastGuiButton::FastGuiButton(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, Ogre::Vector2 const & _textOffset, Ogre::Vector2 const & size, String const & atlas, uint z) : FastGuiWidget(id, atlas, z), textOffset(_textOffset)
	{
		this->label = onew(new FastGuiLabel(id + ".label", defaultGlyphIndex, text, position + textOffset, atlas, z));
		this->decor = onew(new FastGuiDecorWidget(id + ".decor", spriteName, position, size, atlas, z));
	}
	//---------------------------------------------------------
	FastGuiButton::~FastGuiButton()
	{
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(FastGuiButton)
	OOBJECT_END
}