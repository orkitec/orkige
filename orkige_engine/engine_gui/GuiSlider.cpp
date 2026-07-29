/********************************************************************
	created:    Wednesday 2010/11/03 at 19:56
	filename:   GuiSlider.cpp
	author:     hicham.allaoui  
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_gui/GuiSlider.h"
#include "engine_gui/GuiManager.h"
#include <core_event/GlobalEventManager.h>
#include <algorithm>
#include <cmath>


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
				this->label->setAlignment(GuiLabel::LA_LEFT);
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
				this->buttonMainSelection = onew(new GuiButton(buttonId, "select_menu_field", defaultGlyphIndex, EMPTY_VALUE_CAPTION, position, GuiLabel::LA_CENTER, Ogre::Vector2::ZERO, atlas, z));
				if(optr<GuiLabel> valueLabel =
					this->buttonMainSelection->getLabel().lock())
				{
					valueLabel->getCaption()->colour(
						Orkige::Colours::webcolour(Orkige::Colours::Black));
				}
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
		this->arrangeSlider();
	}
	//----------------------------------------------------
    GuiSlider::~GuiSlider()
    {
    }
	//----------------------------------------------------
	void GuiSlider::setPosition( Ogre::Real left, Ogre::Real top )
	{
		this->decor->setPosition(left, top);
		this->arrangeSlider();
	}
	//----------------------------------------------------
	void GuiSlider::setSize( Ogre::Real width, Ogre::Real height )
	{
		this->decor->setSize(width, height);
		this->arrangeSlider();
	}
	//----------------------------------------------------
	void GuiSlider::arrangeSlider()
	{
		// the shared row geometry settles the title / arrows / value field; the
		// grip track IS that value rect and the grip rides it
		const Ogre::Vector4 track = this->arrangeParts();
		this->pin_area->setPosition(track.x, track.y);
		this->pin_area->setSize(track.z, track.w);
		this->pin->setSize(std::floor(std::max(4.0f, track.w * 0.5f)), track.w);
		// rebuild the snap points for the new track, keeping the selection
		const std::size_t keep = this->selectedIndex;
		if (!this->items.empty())
		{
			this->setItems(this->items);		// recomputes itemsPinSnap (selects 0)
			if (keep < this->items.size())
			{
				this->selectedIndex = keep;		// silent: the value never changed
			}
		}
		// ALWAYS re-place the grip: a relayout that happens to keep the selected
		// index must still move the grip onto the new track (it would otherwise
		// stay at its old snap point - outside the widget after a resize)
		this->showItem();
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
	
		// the grip walks the track HORIZONTALLY, centred on it: the snap points
		// are grip TOP-LEFT positions, so each is offset by half the grip
		const Ogre::Vector2 area = this->pin_area->getPosition();
		const Ogre::Vector2 areaSize = this->pin_area->getSize();
		const Ogre::Vector2 grip = this->pin->getSize();
		const Ogre::Real snapTop = std::floor(area.y
			+ (areaSize.y - grip.y) * 0.5f);
		this->itemsPinSnap.resize(this->items.size());
		if (this->itemsPinSnap.size() >= 2)
		for (std::size_t i = 0; i < itemsPinSnap.size(); ++i)
		{
			const float t = static_cast<float>(i)
				/ static_cast<float>(items.size() - 1);
			this->itemsPinSnap.at(i) = Ogre::Vector2(std::floor(area.x
				+ t * (areaSize.x - grip.x)), snapTop);
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