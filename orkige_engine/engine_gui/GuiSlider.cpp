/********************************************************************
	created:    Wednesday 2010/11/03 at 19:56
	filename:   GuiSlider.cpp
	author:     hicham.allaoui  
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "engine_gui/GuiSlider.h"
#include "engine_gui/GuiManager.h"
#include <core_event/GlobalEventManager.h>


namespace Orkige 
{
	IMPL_OWNED_EVENTTYPE(GuiSlider, SelectMenuEvent);
    //----------------------------------------------------
    //- public: ------------------------------------------
    //----------------------------------------------------
	GuiSlider::GuiSlider(String const & id, String const & buttonId, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z) :
		GuiSelectMenu(id, buttonId, spriteName, defaultGlyphIndex, text, position, textAlignment, size, atlas, z),
		pinActive(false)
	{
		// info: the gui elements in .menu files are read linear but interpreted in alphabetical order of their class name descriptor
		// e.g. "Button", "Label". Luckily "Slider" comes last and all other gui element are alredy known and can be used here.

		{
			Orkige::String idBack = id + "_decor_back";
			if (GuiManager::getSingleton().widgetExists(idBack))
			{
				this->decor = std::static_pointer_cast<Orkige::GuiDecorWidget>( GuiManager::getSingleton().getWidget(idBack).lock() );
			}
			else
			{
				this->decor = onew(new GuiDecorWidget(idBack, spriteName, position, size, atlas, z));
			}
		}
		{
			Orkige::String idLabel = id + "_text";
			if (GuiManager::getSingleton().widgetExists(idLabel))
			{
				this->label = std::static_pointer_cast<Orkige::GuiLabel>( GuiManager::getSingleton().getWidget(idLabel).lock() );
			}
			else
			{
				this->label = onew(new GuiLabel(idLabel, defaultGlyphIndex, text, position, atlas, z+1, true));
				this->label->setSize(this->decor->getSize().x, this->decor->getSize().y);
				this->label->setAlignment(GuiLabel::LA_TOP);
			}
		}
		{
			Orkige::String idArrowLeft = id + "_previous";
			Orkige::String idArrowRight = id + "_next";
			if (GuiManager::getSingleton().widgetExists(idArrowLeft) &&
				GuiManager::getSingleton().widgetExists(idArrowRight))
			{
				this->leftArrow = std::static_pointer_cast<Orkige::GuiDecorWidget>( GuiManager::getSingleton().getWidget(idArrowLeft).lock() );
				this->rightArrow = std::static_pointer_cast<Orkige::GuiDecorWidget>( GuiManager::getSingleton().getWidget(idArrowRight).lock() );
			}
			else
			{
				this->leftArrow = onew(new GuiDecorWidget(idArrowLeft, "select_menu_field_left", position, Ogre::Vector2::ZERO, atlas, z));
				this->rightArrow = onew(new GuiDecorWidget(idArrowRight, "select_menu_field_right", position, Ogre::Vector2::ZERO, atlas, z));

				this->leftArrow->setPosition(
					this->decor->getPosition().x - this->leftArrow->getSize().x,
					this->leftArrow->getPosition().y + (this->decor->getSize().y/2.0f) - (this->leftArrow->getSize().y/2.0f));
				this->rightArrow->setPosition(
					this->decor->getPosition().x + this->decor->getSize().x,
					this->rightArrow->getPosition().y + (this->decor->getSize().y/2.0f) - (this->rightArrow->getSize().y/2.0f));
				this->leftArrow->setSize(
					this->decor->getSize().x * 0.2f, 
					this->decor->getSize().y * 0.9f);
				this->rightArrow->setSize(
					this->decor->getSize().x * 0.2f, 
					this->decor->getSize().y * 0.9f);
			}
		}
		{
			Orkige::String idMain = id + "_button";
			if (GuiManager::getSingleton().widgetExists(idMain))
			{
				this->buttonMainSelection = std::static_pointer_cast<Orkige::GuiButton>( GuiManager::getSingleton().getWidget(idMain).lock() );
			}
			else
			{
				this->buttonMainSelection = onew(new GuiButton(buttonId, "select_menu_field", defaultGlyphIndex, "dunnoooooo!", position, GuiLabel::LA_LEFT, Ogre::Vector2::ZERO, atlas, z));
				this->buttonMainSelection->getLabel().lock()->getCaption()->colour(Orkige::Colours::webcolour(Orkige::Colours::Black));

				this->buttonMainSelection->setSize(this->decor->getSize().x * 0.8f, this->decor->getSize().y * 0.3f);

				float positionX = this->buttonMainSelection->getPosition().x + (this->decor->getSize().x / 2.0f) - (this->buttonMainSelection->getSize().x / 2.0f);
				float positionY = this->decor->getPosition().y + (this->decor->getSize().y/2.0f);
				this->buttonMainSelection->setPosition(floor(positionX), floor(positionY));
			}
		}
		{
			Orkige::String idPinArea = id + "_pin_area";
			if (GuiManager::getSingleton().widgetExists(idPinArea))
			{
				this->pin_area = std::static_pointer_cast<Orkige::GuiDecorWidget>( GuiManager::getSingleton().getWidget(idPinArea).lock() );
			}
			else
			{
				this->pin_area = onew(new GuiDecorWidget(idPinArea, "select_menu_pin", this->buttonMainSelection->getPosition(), this->buttonMainSelection->getSize(), atlas, z-1));
			}
		}		
		{
			Orkige::String idPin = id + "_pin";
			if (GuiManager::getSingleton().widgetExists(idPin))
			{
				this->pin = std::static_pointer_cast<Orkige::GuiDecorWidget>( GuiManager::getSingleton().getWidget(idPin).lock() );
			}
			else
			{
				this->pin = onew(new GuiDecorWidget(idPin, "select_menu_pin", position, Ogre::Vector2::ZERO, atlas, z+1));
			}
		}

		this->selectedIndex = -1;
		this->showItem();
	}
	//----------------------------------------------------
    GuiSlider::~GuiSlider()
    {
    }
	//----------------------------------------------------
	void GuiSlider::setPosition( Ogre::Real left, Ogre::Real top )
	{
		Ogre::Vector2 d(
			left - this->decor->getPosition().x,
			top  - this->decor->getPosition().y);
		
		// rounding label the diff vector for nicer font
		this->decor->setPosition( 
			this->decor->getPosition().x + d.x, 
			this->decor->getPosition().y + d.y );
		this->label->setPosition( 
			static_cast<int>(this->label->getPosition().x + d.x), 
			static_cast<int>(this->label->getPosition().y + d.y) );
		this->leftArrow->setPosition( 
			this->leftArrow->getPosition().x + d.x, 
			this->leftArrow->getPosition().y + d.y );
		this->rightArrow->setPosition( 
			this->rightArrow->getPosition().x + d.x, 
			this->leftArrow->getPosition().y + d.y );
 		this->buttonMainSelection->getDecor().lock()->setPosition( 
			this->buttonMainSelection->getDecor().lock()->getPosition().x + d.x, 
			this->buttonMainSelection->getDecor().lock()->getPosition().y + d.y );
		this->pin->setPosition( 
			this->pin->getPosition().x + d.x, 
			this->pin->getPosition().y + d.y );
		this->pin_area->setPosition( 
			this->pin_area->getPosition().x + d.x, 
			this->pin_area->getPosition().y + d.y );
	}
	//----------------------------------------------------
	void GuiSlider::setSize( Ogre::Real width, Ogre::Real height )
	{
		// re-lay-out the compound slider around the new field size, mirroring the
		// constructor's proportional geometry: the decor is the field, the arrows
		// hug its sides, the main button + pin track sit centred inside it.
		const Ogre::Vector2 origin = this->decor->getPosition();
		this->decor->setSize( width, height );
		this->label->setSize( width, height );

		this->leftArrow->setSize( width * 0.2f, height * 0.9f );
		this->rightArrow->setSize( width * 0.2f, height * 0.9f );
		this->leftArrow->setPosition(
			origin.x - this->leftArrow->getSize().x,
			origin.y + (height / 2.0f) - (this->leftArrow->getSize().y / 2.0f) );
		this->rightArrow->setPosition(
			origin.x + width,
			origin.y + (height / 2.0f) - (this->rightArrow->getSize().y / 2.0f) );

		this->buttonMainSelection->setSize( width * 0.8f, height * 0.3f );
		float positionX = origin.x + (width / 2.0f) -
			(this->buttonMainSelection->getSize().x / 2.0f);
		float positionY = origin.y + (height / 2.0f);
		this->buttonMainSelection->setPosition( floor(positionX), floor(positionY) );

		this->pin_area->setSize(
			this->buttonMainSelection->getSize().x,
			this->buttonMainSelection->getSize().y );
		this->pin_area->setPosition(
			this->buttonMainSelection->getPosition().x,
			this->buttonMainSelection->getPosition().y );

		// rebuild the pin snap track for the new geometry, keeping the selection
		const int keep = this->selectedIndex;
		if (!this->items.empty())
		{
			this->setItems( this->items );	// recomputes itemsPinSnap (selects 0)
			if (keep >= 0 && keep < static_cast<int>(this->items.size()))
			{
				this->selectItemIndex( keep, false );
			}
		}
		else
		{
			this->showItem();				// just reposition/hide the pin
		}
	}
	//----------------------------------------------------
	void GuiSlider::onCursorPressed( Ogre::Vector2 const & cursorPos )
	{
		if (this->buttonMainSelection->getDecor().lock()->getRectangle()->intersects(cursorPos)) 
		{
			this->buttonMainSelection->onCursorPressed(cursorPos);
		}
		if (this->pin->getRectangle()->intersects(cursorPos))
		{
			if (itemsPinSnap.size() >= 2)
			{
				this->pinActive = true;
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
	void GuiSlider::onCursorReleased( Ogre::Vector2 const & cursorPos )
	{
		this->buttonMainSelection->onCursorReleased(cursorPos);

		this->pinActive = false;
	}
	//----------------------------------------------------
	void GuiSlider::onCursorMoved( Ogre::Vector2 const & cursorPos )
	{
		this->buttonMainSelection->onCursorMoved(cursorPos);

		if (this->pinActive)
		{
			// calculate closest snap point
			int closestIndex = -1;
			float minDist = Ogre::Math::POS_INFINITY;
			float dist;
			for (std::size_t i = 0; i < itemsPinSnap.size(); ++i)
			{
				dist = cursorPos.distance(itemsPinSnap.at(i));
				if (dist < minDist)
				{
					minDist = dist;
					closestIndex = (int)i;
				}
			}
			oAssert(closestIndex != -1);

			this->selectItemIndex(closestIndex);
		}
	}
	//----------------------------------------------------
	void GuiSlider::showItem()
	{
		GuiSelectMenu::showItem();

		if (this->itemsPinSnap.empty())
		{
			// hide 
			this->pin->setPosition(-2000, -2000);
		}
		else
		{
			Ogre::Vector2 & pos = this->itemsPinSnap.at(this->selectedIndex);
			this->pin->setPosition(pos.x, pos.y);
		}
	}
	//----------------------------------------------------
	void GuiSlider::setItems( const Ogre::StringVector& items )
	{
		this->items = items;
	
		// slider geometric range
		Ogre::Vector2 p0 = this->pin_area->getPosition() - this->pin->getSize() * 0.5;
		Ogre::Vector2 p1 = this->pin_area->getSize();
		
		// calculate slider positions
		float t;
		this->itemsPinSnap.resize(this->items.size());
		if (this->itemsPinSnap.size() >= 2)
		for (std::size_t i = 0; i < itemsPinSnap.size(); ++i)
		{
			t = static_cast<float>(i) / static_cast<float>(items.size() - 1);
			this->itemsPinSnap.at(i) = Ogre::Vector2(p0 + t * p1);
		}

		this->selectItemIndex(0, false);
	}
    //----------------------------------------------------
    //- protected: ---------------------------------------
    //----------------------------------------------------
	void GuiSlider::onEnabledChanged(bool enable)
	{
		GuiSelectMenu::onEnabledChanged(enable);
		const float alpha = enable ? 1.0f : GuiWidget::DISABLED_ALPHA;
		if(this->pin)		this->pin->setAlpha(alpha);
		if(this->pin_area)	this->pin_area->setAlpha(alpha);
	}
    //----------------------------------------------------
    //- private: -----------------------------------------
    //----------------------------------------------------

	void GuiSlider::applyRenderTransform(Ui2DTransform const & transform)
	{
		GuiSelectMenu::applyRenderTransform(transform);
		if(this->pin)		this->pin->applyRenderTransform(transform);
		if(this->pin_area)	this->pin_area->applyRenderTransform(transform);
	}
	//----------------------------------------------------
	void GuiSlider::applyRenderAlpha(float alphaMultiplier)
	{
		GuiSelectMenu::applyRenderAlpha(alphaMultiplier);
		if(this->pin)		this->pin->applyRenderAlpha(alphaMultiplier);
		if(this->pin_area)	this->pin_area->applyRenderAlpha(alphaMultiplier);
	}
	//----------------------------------------------------
	OABSTRACT_IMPL(GuiSlider)
		OOBJECT_END

} 