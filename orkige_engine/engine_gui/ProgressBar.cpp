/********************************************************************
	created:	Wednesday 2010/08/04 at 15:08
	filename: 	ProgressBar.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/

#include "engine_gui/ProgressBar.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	ProgressBar::ProgressBar(String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width, Ogre::Real commentBoxWidth) : Widget(name, materialGroup), progress(0.0f)
	{
		this->overlayElement = Ogre::OverlayManager::getSingleton().createOverlayElementFromTemplate(this->materialGroup + "/ProgressBar", "BorderPanel", name);
		this->overlayElement->setWidth(width);
		Ogre::OverlayContainer* c = (Ogre::OverlayContainer*)this->overlayElement;
		this->textArea = (Ogre::TextAreaOverlayElement*)c->getChild(this->getName() + "/ProgressCaption");
		Ogre::OverlayContainer* commentBox = (Ogre::OverlayContainer*)c->getChild(this->getName() + "/ProgressCommentBox");
		commentBox->setWidth(commentBoxWidth);
		commentBox->setLeft(-(commentBoxWidth + 5));
		this->commentTextArea = (Ogre::TextAreaOverlayElement*)commentBox->getChild(commentBox->getName() + "/ProgressCommentText");
		this->meter = c->getChild(getName() + "/ProgressMeter");
		this->meter->setWidth(width - 10);
		this->fill = ((Ogre::OverlayContainer*)this->meter)->getChild(this->meter->getName() + "/ProgressFill");
		this->setCaption(caption);
	}
	//---------------------------------------------------------
	void ProgressBar::setProgress(Ogre::Real progress)
	{
		this->progress = Ogre::Math::Clamp<Ogre::Real>(progress, 0, 1);
		this->fill->setWidth((Ogre::Real)std::max<int>((int)this->fill->getHeight(), (int)(this->progress * (this->meter->getWidth() - 2 * this->fill->getLeft()))));
	}
	//---------------------------------------------------------
	Ogre::Real ProgressBar::getProgress()
	{
		return this->progress;
	}
	//---------------------------------------------------------
	const Ogre::DisplayString& ProgressBar::getCaption()
	{
		return this->textArea->getCaption();
	}
	//---------------------------------------------------------
	void ProgressBar::setCaption(const Ogre::DisplayString& caption)
	{
		this->textArea->setCaption(caption);
	}
	//---------------------------------------------------------
	const Ogre::DisplayString& ProgressBar::getComment()
	{
		return this->commentTextArea->getCaption();
	}
	//---------------------------------------------------------
	void ProgressBar::setComment(const Ogre::DisplayString& comment)
	{
		this->commentTextArea->setCaption(comment);
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(ProgressBar)
	OOBJECT_END
}