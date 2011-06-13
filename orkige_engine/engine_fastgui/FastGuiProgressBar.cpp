/********************************************************************
	created:    Wednesday 2010/11/17 at 16:04
	filename:   FastGuiProgressBar.cpp
	author:     hicham.allaoui  
	notice:		This source file is part of orkige (orkitec Game engine)
	For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/

#include "engine_fastgui/FastGuiProgressBar.h"
#include "engine_fastgui/FastGuiManager.h"
#include <core_event/GlobalEventManager.h>

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(FastGuiProgressBar, ProgressBarChanged);
	//----------------------------------------------------
	//- public: ------------------------------------------
	//----------------------------------------------------
    FastGuiProgressBar::FastGuiProgressBar(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, FastGuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z)
		: FastGuiWidget(id, atlas, z)
    {
		this->decor = onew(new FastGuiDecorWidget(id + ".decor", spriteName, position, size, atlas, z));
		Ogre::Vector2 barPos = Ogre::Vector2(this->decor->getPosition().x + this->decor->getSize().x * 0.03f, this->decor->getPosition().y + this->decor->getSize().y * 0.1f);
		this->barMaxSize = Ogre::Vector2(this->decor->getSize().x - this->decor->getSize().x * 0.06f, this->decor->getSize().y - this->decor->getSize().y * 0.2f);
		this->barDecore = onew(new FastGuiDecorWidget(id + ".bar", "progressbar_bar", barPos, barMaxSize, atlas, z));
		
		this->label = onew(new FastGuiLabel(id + ".label", defaultGlyphIndex, text, position, atlas, z, false));
	//	this->label->setSize(this->decor->getSize().x, this->decor->getSize().y);
		this->label->setSize(barMaxSize.x, barMaxSize.y);
		this->label->setAlignment(textAlignment);

		this->progress = 0.0f;
    }
	//----------------------------------------------------
    FastGuiProgressBar::~FastGuiProgressBar()
    {
    }   
	//----------------------------------------------------
	Ogre::Vector2 FastGuiProgressBar::getSize()
	{
		return this->decor->getSize();
	}
	//----------------------------------------------------
	Ogre::Vector2 FastGuiProgressBar::getPosition()
	{
		return this->decor->getPosition();
	}
	//----------------------------------------------------
	Orkige::String FastGuiProgressBar::getCaption()
	{
		return this->label->getCaption()->text();
	}
	//----------------------------------------------------
	void FastGuiProgressBar::setCaption(String const & text)
	{
		this->label->setText(text);
	}
	//----------------------------------------------------
	void FastGuiProgressBar::setPosition(Ogre::Real left, Ogre::Real top)
	{
		this->decor->setPosition(left, top);
		this->label->setPosition(left, top);
		this->barDecore->setPosition(this->decor->getPosition().x + this->decor->getSize().x * 0.03f, this->decor->getPosition().y + this->decor->getSize().y * 0.1f);
	}
	//----------------------------------------------------
	void FastGuiProgressBar::setSize(Ogre::Real width, Ogre::Real height)
	{
		this->decor->setSize(width, height);
		this->barMaxSize = Ogre::Vector2(this->decor->getSize().x - this->decor->getSize().x * 0.06f, this->decor->getSize().y - this->decor->getSize().y * 0.2f);
		this->label->setSize(barMaxSize.x,barMaxSize.y);
		this->addProgress(0.0f);
	}
	//----------------------------------------------------
	void FastGuiProgressBar::setProgress(float _progress)
	{
		this->progress = Ogre::Math::Ceil(_progress);
		this->progress = Ogre::Math::Clamp(progress, 0.0f, 100.0f);
		this->barDecore->setSize(this->barMaxSize.x * progress/100.0f, this->barDecore->getSize().y);

		GlobalEventManager::getSingleton().trigger(Event(FastGuiProgressBar::ProgressBarChanged, oBadPointer(this)));
	}
	//----------------------------------------------------
	void FastGuiProgressBar::addProgress(float _progress)
	{
		this->progress += _progress;
		setProgress(this->progress);
	}

    //----------------------------------------------------
    //- protected: ---------------------------------------
    //----------------------------------------------------

    //----------------------------------------------------
    //- private: -----------------------------------------
    //----------------------------------------------------
	OABSTRACT_IMPL(FastGuiProgressBar)
		OOBJECT_END


} 