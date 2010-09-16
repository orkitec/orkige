/********************************************************************
	created:	Wednesday 2010/08/04 at 15:09
	filename: 	Slider.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_gui/Slider.h"
#include "engine_gui/GuiManager.h"

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(Slider, SliderMovedEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	Slider::Slider(String const & name, String const & materialGroup, const Ogre::DisplayString& caption, Ogre::Real width, Ogre::Real trackWidth,
		Ogre::Real valueBoxWidth, Ogre::Real minValue, Ogre::Real maxValue, unsigned int snaps)
		: Widget(name, materialGroup)
		, dragOffset(0.0f)
		, value(0.0f)
		, minValue(0.0f)
		, maxValue(0.0f)
		, interval(0.0f)
	{
		this->dragging = false;
		this->fitToContents = false;
		this->overlayElement = Ogre::OverlayManager::getSingleton().createOverlayElementFromTemplate(this->materialGroup + "/Slider", "BorderPanel", name);
		this->overlayElement->setWidth(width);
		Ogre::OverlayContainer* c = (Ogre::OverlayContainer*)this->overlayElement;
		this->textArea = (Ogre::TextAreaOverlayElement*)c->getChild(this->getName() + "/SliderCaption");
		Ogre::OverlayContainer* valueBox = (Ogre::OverlayContainer*)c->getChild(this->getName() + "/SliderValueBox");
		valueBox->setWidth(valueBoxWidth);
		valueBox->setLeft(-(valueBoxWidth + 5));
		this->valueTextArea = (Ogre::TextAreaOverlayElement*)valueBox->getChild(valueBox->getName() + "/SliderValueText");
		this->track = (Ogre::BorderPanelOverlayElement*)c->getChild(this->getName() + "/SliderTrack");
		this->handle = (Ogre::PanelOverlayElement*)this->track->getChild(this->track->getName() + "/SliderHandle");
#if OGRE_PLATFORM == OGRE_PLATFORM_IPHONE
		this->textArea->setCharHeight(this->textArea->getCharHeight() - 3);
		this->valueTextArea->setCharHeight(this->valueTextArea->getCharHeight() - 3);
#endif

		if (trackWidth <= 0)  // tall style
		{
			this->track->setWidth(width - 16);
		}
		else  // long style
		{
			if (width <= 0) this->fitToContents = true;
			this->overlayElement->setHeight(34);
			this->textArea->setTop(10);
			valueBox->setTop(2);
			this->track->setTop(-23);
			this->track->setWidth(trackWidth);
			this->track->setHorizontalAlignment(Ogre::GHA_RIGHT);
			this->track->setLeft(-(trackWidth + valueBoxWidth + 5));
		}

		this->setCaption(caption);
		this->setRange(minValue, maxValue, snaps, false);
	}
	//---------------------------------------------------------
	void Slider::setRange(Ogre::Real minValue, Ogre::Real maxValue, unsigned int snaps, bool notifyListener)
	{
		this->minValue = minValue;
		this->maxValue = maxValue;

		if (snaps <= 1 || this->minValue >= this->maxValue)
		{
			this->interval = 0;
			this->handle->hide();
			this->value = minValue;
			if (snaps == 1) 
			{
				this->valueTextArea->setCaption(Ogre::StringConverter::toString(this->minValue));
			}
			else 
			{
				this->valueTextArea->setCaption("");
			}
		}
		else
		{
			this->handle->show();
			this->interval = (maxValue - minValue) / (snaps - 1);
			this->setValue(minValue, notifyListener);
		}
	}
	//---------------------------------------------------------
	const Ogre::DisplayString& Slider::getValueCaption()
	{
		return this->valueTextArea->getCaption();
	}
	//---------------------------------------------------------
	void Slider::setValueCaption(const Ogre::DisplayString& caption)
	{
		this->valueTextArea->setCaption(caption);
	}
	//---------------------------------------------------------
	void Slider::setValue(Ogre::Real value, bool notifyListener)
	{
		if (this->interval == 0) 
		{
			return;
		}

		this->value = Ogre::Math::Clamp<Ogre::Real>(value, this->minValue, this->maxValue);

		this->setValueCaption(Ogre::StringConverter::toString(this->value));

		if (notifyListener) 
		{
			GuiManager::getSingleton().getEventManager()->trigger(Event(Slider::SliderMovedEvent, oBadPointer(this)));
		}

		if (!this->dragging) 
		{
			this->handle->setLeft((Ogre::Real)(int)((this->value - this->minValue) / (this->maxValue - this->minValue) * (this->track->getWidth() - this->handle->getWidth())));
		}
	}
	//---------------------------------------------------------
	Ogre::Real Slider::getValue()
	{
		return this->value;
	}
	//---------------------------------------------------------
	const Ogre::DisplayString& Slider::getCaption()
	{
		return this->textArea->getCaption();
	}
	//---------------------------------------------------------
	void Slider::setCaption(const Ogre::DisplayString& caption)
	{
		this->textArea->setCaption(caption);

		if (this->fitToContents) 
		{
			this->overlayElement->setWidth(OverlayUtil::getCaptionWidth(caption, this->textArea) + this->valueTextArea->getParent()->getWidth() + this->track->getWidth() + 26);
		}
	}
	//---------------------------------------------------------
	void Slider::onCursorPressed(const Ogre::Vector2& cursorPos)
	{
		if (!this->handle->isVisible()) 
		{
			return;
		}

		Ogre::Vector2 co = OverlayUtil::cursorOffset(this->handle, cursorPos);

		if (co.squaredLength() <= 81)
		{
			this->dragging = true;
			this->dragOffset = co.x;
		}
		else if (OverlayUtil::isCursorOver(this->track, cursorPos))
		{
			Ogre::Real newLeft = this->handle->getLeft() + co.x;
			Ogre::Real rightBoundary = this->track->getWidth() - this->handle->getWidth();

			this->handle->setLeft((Ogre::Real)Ogre::Math::Clamp<int>((int)newLeft, 0, (int)rightBoundary));
			this->setValue(getSnappedValue(newLeft / rightBoundary));
		}
	}
	//---------------------------------------------------------
	void Slider::onCursorReleased(const Ogre::Vector2& cursorPos)
	{
		if (this->dragging)
		{
			this->dragging = false;
			this->handle->setLeft((Ogre::Real)(int)((this->value - this->minValue) / (this->maxValue - this->minValue) * (this->track->getWidth() - this->handle->getWidth())));
		}
	}
	//---------------------------------------------------------
	void Slider::onCursorMoved(const Ogre::Vector2& cursorPos)
	{
		if (this->dragging)
		{
			Ogre::Vector2 co = OverlayUtil::cursorOffset(this->handle, cursorPos);
			Ogre::Real newLeft = this->handle->getLeft() + co.x - this->dragOffset;
			Ogre::Real rightBoundary = this->track->getWidth() - this->handle->getWidth();

			this->handle->setLeft((Ogre::Real)Ogre::Math::Clamp<int>((int)newLeft, 0, (int)rightBoundary));
			this->setValue(this->getSnappedValue(newLeft / rightBoundary));
		}
	}
	//---------------------------------------------------------
	void Slider::onFocusLost()
	{
		this->dragging = false;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	Ogre::Real Slider::getSnappedValue(Ogre::Real percentage)
	{
		percentage = Ogre::Math::Clamp<Ogre::Real>(percentage, 0, 1);
		unsigned int whichMarker = (unsigned int) (percentage * (this->maxValue - this->minValue) / this->interval + 0.5);
		return whichMarker * this->interval + this->minValue;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(Slider)
	OOBJECT_END
}