/********************************************************************
	created:    Wednesday 2010/11/03 at 19:56
	filename:   FastGuiSlider.cpp
	author:     hicham.allaoui  
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_fastgui/FastGuiSlider.h"
#include "engine_fastgui/FastGuiManager.h"
#include <core_event/GlobalEventManager.h>


namespace Orkige 
{
	IMPL_OWNED_EVENTTYPE(FastGuiSlider, SelectMenuEvent);
    //----------------------------------------------------
    //- public: ------------------------------------------
    //----------------------------------------------------
	FastGuiSlider::FastGuiSlider(String const & id, String const & buttonId, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, FastGuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z) 
		//: FastGuiWidget(id, atlas, z)
		: FastGuiSelectMenu(id, buttonId, spriteName, defaultGlyphIndex, text, position, textAlignment, size, atlas, z),
		pinActive(false)
	{
		/*
		this->decor = onew(new FastGuiDecorWidget(id + ".decor", spriteName, position, size, atlas, z));
		this->label = onew(new FastGuiLabel(id + ".label", defaultGlyphIndex, text, position, atlas, z, true));
		this->label->setSize(this->decor->getSize().x, this->decor->getSize().y);
		this->label->setAlignment(FastGuiLabel::LA_TOP);

		this->leftArrow = onew(new FastGuiDecorWidget("leftArrow.decor", "select_menu_field_left", position, Ogre::Vector2::ZERO, atlas, z));
		this->rightArrow = onew(new FastGuiDecorWidget("rightArrow.decor", "select_menu_field_right", position, Ogre::Vector2::ZERO, atlas, z));
		
		this->buttonMainSelection = onew(new FastGuiButton(buttonId, "select_menu_field", defaultGlyphIndex, "dunnoooooo!", position, FastGuiLabel::LA_LEFT, Ogre::Vector2::ZERO, atlas, z));
		this->buttonMainSelection->getLabel().lock()->getCaption()->colour(Orkige::Colours::webcolour(Orkige::Colours::Black));

		if (size != Ogre::Vector2::ZERO)
		{
			this->updateSize();
		}
		this->updatePosition();

		this->selectedIndex = 0;
		this->showItem();
		*/
		this->pin = onew(new FastGuiDecorWidget("slider.decor", "select_menu_pin", position, Ogre::Vector2::ZERO, atlas, z+1));
	}
	//----------------------------------------------------
    FastGuiSlider::~FastGuiSlider()
    {
    }

	//----------------------------------------------------
	void FastGuiSlider::onCursorPressed( Ogre::Vector2 const & cursorPos )
	{
		if (this->buttonMainSelection->getDecor().lock()->getRectangle()->intersects(cursorPos)) 
		{
			this->buttonMainSelection->onCursorPressed(cursorPos);
		}
		if (this->pin->getRectangle()->intersects(cursorPos))
		{
			if (itemsPinSnap.size() >= 2)
			{
				pinActive = true;
			}
		}
		else
		{
			if (this->leftArrow->getRectangle()->intersects(cursorPos))
			{
				this->selectItemIndex(this->selectedIndex - 1);
			}
			if (this->rightArrow->getRectangle()->intersects(cursorPos))
			{
				this->selectItemIndex(this->selectedIndex + 1);

			}
		}
	}
	//----------------------------------------------------
	void FastGuiSlider::onCursorReleased( Ogre::Vector2 const & cursorPos )
	{
		this->buttonMainSelection->onCursorReleased(cursorPos);

		pinActive = false;
	}
	//----------------------------------------------------
	void FastGuiSlider::onCursorMoved( Ogre::Vector2 const & cursorPos )
	{
		this->buttonMainSelection->onCursorMoved(cursorPos);

		if (pinActive)
		{
			// calculate closest snap point
			int closestIndex = -1;
			float minDist = Ogre::Math::POS_INFINITY;
			float dist;
			for (int i = 0; i < itemsPinSnap.size(); ++i)
			{
				dist = cursorPos.distance(itemsPinSnap.at(i));
				if (dist < minDist)
				{
					minDist = dist;
					closestIndex = i;
				}
			}
			oAssert(closestIndex != -1);

			this->selectItemIndex(closestIndex);
		}
	}
	//----------------------------------------------------
	void FastGuiSlider::showItem()
	{
		FastGuiSelectMenu::showItem();

		Ogre::Vector2 & pos = this->itemsPinSnap.at(this->selectedIndex);
		this->pin->setPosition(pos.x, pos.y);
	}
	//----------------------------------------------------
	void FastGuiSlider::setItems( const Ogre::StringVector& items )
	{
		this->items = items;
	
		// test
		Ogre::Vector2 p0 = buttonMainSelection->getPosition();
		Ogre::Vector2 p1 = buttonMainSelection->getPosition() + buttonMainSelection->getSize();
		p1 -= p0;

		// calculate slider positions
		float t;
		this->itemsPinSnap.resize(this->items.size());
		if (this->itemsPinSnap.size() >= 2)
		for (int i = 0; i < itemsPinSnap.size(); ++i)
		{
			t = static_cast<float>(i) / static_cast<float>(items.size() - 1);
			this->itemsPinSnap.at(i) = Ogre::Vector2(p0 + t * p1);
		}

		this->selectItemIndex(0, false);
	}
    //----------------------------------------------------
    //- protected: ---------------------------------------
    //----------------------------------------------------

    //----------------------------------------------------
    //- private: -----------------------------------------
    //----------------------------------------------------

	OABSTRACT_IMPL(FastGuiSlider)
		OOBJECT_END

} 