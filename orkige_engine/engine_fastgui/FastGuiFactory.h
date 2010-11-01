/********************************************************************
	created:	Wednesday 2010/10/27 at 13:09
	filename: 	FastGuiFactory.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec	
*********************************************************************/
#ifndef __FastGuiFactory_h__27_10_2010__13_09_02__
#define __FastGuiFactory_h__27_10_2010__13_09_02__

#include "engine_fastgui/FastGuiWidget.h"
#include "engine_fastgui/FastGuiDecorWidget.h"
#include "engine_fastgui/FastGuiButton.h"
#include "engine_fastgui/FastGuiTextbox.h"
#include <core_util/StringUtil.h>

namespace Orkige
{
	class FastGuiFactory : public Ogre::ConfigFile
	{
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		//--- Methods -----------------------------------------------
	public:
		FastGuiFactory();
		virtual ~FastGuiFactory();
		virtual woptr<FastGuiDecorWidget> createDecorWidget(String const & id, String const & spriteName, Ogre::Vector2 const & position, Ogre::Vector2 const & size, String const & atlas, uint z);
		virtual woptr<FastGuiLabel> createLabel(String const & id, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, String const & atlas, uint z);
		virtual woptr<FastGuiTextbox> createTextbox(String const & id, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, String const & atlas, uint z);
		virtual woptr<FastGuiButton> createButton(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, FastGuiLabel::LabelAlignment textAlignment = FastGuiLabel::LA_CENTER, Ogre::Vector2 const & size = Ogre::Vector2::ZERO, String const & atlas = StringUtil::BLANK, uint z = 0);
		virtual void load(String const filename);
	protected:
	private:
	};
	//---------------------------------------------------------------
}

#endif //__FastGuiFactory_h__27_10_2010__13_09_02__