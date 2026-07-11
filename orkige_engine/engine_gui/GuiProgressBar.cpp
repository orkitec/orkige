/********************************************************************
	created:    Wednesday 2010/11/17 at 16:04
	filename:   GuiProgressBar.cpp
	author:     hicham.allaoui  
	notice:		This source file is part of orkige (orkitec Game engine)
	For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/

#include "engine_gui/GuiProgressBar.h"
#include "engine_gui/GuiManager.h"
#include <core_event/GlobalEventManager.h>

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(GuiProgressBar, ProgressBarChanged);
	//----------------------------------------------------
	//- public: ------------------------------------------
	//----------------------------------------------------
    GuiProgressBar::GuiProgressBar(String const & id, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z)
		: GuiWidget(id, atlas, z)
    {
		this->decor = onew(new GuiDecorWidget(id + ".decor", spriteName, position, size, atlas, z));
		Ogre::Vector2 barPos = Ogre::Vector2(this->decor->getPosition().x + this->decor->getSize().x * 0.03f, this->decor->getPosition().y + this->decor->getSize().y * 0.1f);
		this->barMaxSize = Ogre::Vector2(this->decor->getSize().x - this->decor->getSize().x * 0.06f, this->decor->getSize().y - this->decor->getSize().y * 0.2f);
		this->barDecore = onew(new GuiDecorWidget(id + ".bar", "progressbar_bar", barPos, barMaxSize, atlas, z));
		
		this->label = onew(new GuiLabel(id + ".label", defaultGlyphIndex, text, position, atlas, z, false));
	//	this->label->setSize(this->decor->getSize().x, this->decor->getSize().y);
		this->label->setSize(barMaxSize.x, barMaxSize.y);
		this->label->setAlignment(textAlignment);

		this->progress = 0.0f;
    }
	//----------------------------------------------------
    GuiProgressBar::~GuiProgressBar()
    {
    }   
	//----------------------------------------------------
	Ogre::Vector2 GuiProgressBar::getSize()
	{
		return this->decor->getSize();
	}
	//----------------------------------------------------
	Ogre::Vector2 GuiProgressBar::getPosition()
	{
		return this->decor->getPosition();
	}
	//----------------------------------------------------
	Orkige::String GuiProgressBar::getCaption()
	{
		return this->label->getCaption()->text();
	}
	//----------------------------------------------------
	void GuiProgressBar::setCaption(String const & text)
	{
		this->label->setText(text);
	}
	//----------------------------------------------------
	void GuiProgressBar::setPosition(Ogre::Real left, Ogre::Real top)
	{
		this->decor->setPosition(left, top);
		this->label->setPosition(left, top);
		this->barDecore->setPosition(this->decor->getPosition().x + this->decor->getSize().x * 0.03f, this->decor->getPosition().y + this->decor->getSize().y * 0.1f);
	}
	//----------------------------------------------------
	void GuiProgressBar::setSize(Ogre::Real width, Ogre::Real height)
	{
		this->decor->setSize(width, height);
		this->barMaxSize = Ogre::Vector2(this->decor->getSize().x - this->decor->getSize().x * 0.06f, this->decor->getSize().y - this->decor->getSize().y * 0.2f);
		this->label->setSize(barMaxSize.x,barMaxSize.y);
		this->addProgress(0.0f);
	}
	//----------------------------------------------------
	void GuiProgressBar::setProgress(float _progress)
	{
		this->progress = Ogre::Math::Ceil(_progress);
		this->progress = Ogre::Math::Clamp(progress, 0.0f, 100.0f);
		this->barDecore->setSize(this->barMaxSize.x * progress/100.0f, this->barDecore->getSize().y);

		GlobalEventManager::getSingleton().trigger(Event(GuiProgressBar::ProgressBarChanged, oBadPointer(this)));
	}
	//----------------------------------------------------
	void GuiProgressBar::addProgress(float _progress)
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
	void GuiProgressBar::applyRenderTransform(Ui2DTransform const & transform)
	{
		if(this->decor)		this->decor->applyRenderTransform(transform);
		if(this->barDecore)	this->barDecore->applyRenderTransform(transform);
		if(this->label)		this->label->applyRenderTransform(transform);
	}
	//----------------------------------------------------
	void GuiProgressBar::applyRenderAlpha(float alphaMultiplier)
	{
		if(this->decor)		this->decor->applyRenderAlpha(alphaMultiplier);
		if(this->barDecore)	this->barDecore->applyRenderAlpha(alphaMultiplier);
		if(this->label)		this->label->applyRenderAlpha(alphaMultiplier);
	}
	//----------------------------------------------------
	OABSTRACT_IMPL(GuiProgressBar)
		OFUNC(setProgress)
		OFUNC(addProgress)
		OFUNC(getProgress)
		OFUNC(getCaption)
		OFUNC(setCaption)
		OOBJECT_END


} 