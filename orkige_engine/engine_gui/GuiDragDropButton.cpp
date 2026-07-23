/********************************************************************
	created:	Monday 2010/11/01
	filename:	GuiDragDropButton.cpp
	author:		philipp.engelhard
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_gui/GuiDragDropButton.h"
#include "engine_gui/DragEventData.h"
#include "engine_gui/GuiManager.h"
#include "core_event/GlobalEventManager.h"
#include <engine_sound/SoundManager.h>

namespace Orkige
{
	
	IMPL_OWNED_EVENTTYPE(GuiDragDropButton, DragEndEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	GuiDragDropButton::GuiDragDropButton(String const & id,
												 String const & spriteName,
												 uint defaultGlyphIndex, 
												 String const & text, 
												 Ogre::Vector2 const & position, 
												 GuiLabel::LabelAlignment textAlignment,
												 Ogre::Vector2 const & size,
												 String const & atlas,
												 uint z)
		:
		GuiWidget(id, atlas, z),
		dragEvent(GuiDragDropButton::DragEndEvent),
		dragEventData(),
		imageToCursorOffset(),
		initialDecorPosition(position),
		initialWidgetPosition(position),
		isActionButton(false),
		isRightSide(true)
	{
		this->markInteractive();	// a drag-drop button consumes press/drag input
		//oAssertDesc(size.x > 0.0 && size.y > 0.0, "Warning: button has invalid size and won't create any events: " << id);

		this->background = onew(new GuiDecorWidget(id + ".background", spriteName, position, size, atlas, z));
		this->decor = onew(new GuiDecorWidget(id + ".decor", spriteName, position, size, atlas, z));

		this->background->getRectangle()->no_background();
		
		this->label = onew(new GuiLabel(id + ".label", defaultGlyphIndex, text, position, atlas, z, true));
		this->label->setSize(this->decor->getSize().x, this->decor->getSize().y);
		this->label->setAlignment(textAlignment);
	
		this->state = DDBS_UP;
		this->baseSpriteName = spriteName;
	
		this->dragEventData = onew(new DragEventData());
		this->dragEventData->button = oBadPointer(this);
		this->dragEvent.setData(this->dragEventData);

		this->isEnabled = true;
		this->isFreezed = false;
	}
	//---------------------------------------------------------
	GuiDragDropButton::~GuiDragDropButton()
	{
	}
	//---------------------------------------------------------
	const GuiDragDropButton::DragDropButtonState& GuiDragDropButton::getState() 
	{ 
		return this->state; 
	}
	//---------------------------------------------------------
	void GuiDragDropButton::setPosition(Ogre::Real left, Ogre::Real top)
	{
		this->decor->setPosition(left, top);
		this->label->setPosition(left, top);
	}
	//---------------------------------------------------------
	void GuiDragDropButton::setSize(Ogre::Real width, Ogre::Real height)
	{
		this->decor->setSize(width, height);
		this->label->setSize(width, height);
	}
	//---------------------------------------------------------
	Ogre::Vector2 GuiDragDropButton::getSize()
	{
		return this->decor->getSize();
	}
	//---------------------------------------------------------
	Ogre::Vector2 GuiDragDropButton::getPosition()
	{
		return this->decor->getPosition();
	}
	//---------------------------------------------------------
	void GuiDragDropButton::onCursorPressed(Ogre::Vector2 const & cursorPos)
	{

		if ((this->isActionButton) && (this->decor->getRectangle()->intersects(cursorPos)))
		{
			if (this->isEnabled && !this->isFreezed)
			{
				SoundManager::getSingleton().playSound("pickcard");
				this->setState(GuiDragDropButton::DDBS_OVER);
				GlobalEventManager::getSingleton().trigger(Event(GuiButton::ButtonHitEvent, oBadPointer(this)));
			}
			return;
		}
			
		if ( this->state == GuiDragDropButton::DDBS_DRAGGING )
		{
			// this can only be another mouse button than the one
			// we started dragging with, so --> abort dragging
			
			this->setState(GuiDragDropButton::DDBS_OVER);
	
			if (this->isEnabled && !this->isFreezed)
			{
				this->decor->setPosition(this->initialDecorPosition.x, this->initialDecorPosition.y);
				
			}
	
			this->dragEventData->state = DragEventData::DS_DRAG_ABORT;
			this->dragEventData->position = cursorPos;
		
			GlobalEventManager::getSingleton().trigger(Event(this->dragEvent));

		}
		if (this->decor->getRectangle()->intersects(cursorPos)) 
		{
			// I'm unsure about this, which way it should be done (pe)
			//this->setState(GuiDragDropButton::DDBS_DOWN);
			this->startDragging(cursorPos);
			this->dragging(cursorPos);
			SoundManager::getSingleton().playSound("pickcard");
		}
	}
	//---------------------------------------------------------
	void GuiDragDropButton::onCursorReleased(Ogre::Vector2 const & cursorPos)
	{
		
		if (this->isActionButton)
		{
			return;
		}
		else if (this->state == GuiDragDropButton::DDBS_DRAGGING)
		{
			this->setState(GuiDragDropButton::DDBS_OVER);
	
			this->dragEventData->position = cursorPos;
			if (this->isEnabled && !this->isFreezed)
			{
				if ( this->isRightSide && (cursorPos.x > this->background->getRectangle()->left()) )
				{
					this->dragEventData->state = DragEventData::DS_DRAG_ABORT;
					GlobalEventManager::getSingleton().trigger(Event(GuiButton::ButtonHitEvent, oBadPointer(this)));
				}
				else if (!this->isRightSide && (cursorPos.x < this->background->getRectangle()->left() + this->background->getRectangle()->width()) )
				{
					this->dragEventData->state = DragEventData::DS_DRAG_ABORT;
					GlobalEventManager::getSingleton().trigger(Event(GuiButton::ButtonHitEvent, oBadPointer(this)));
				}
				else
				{
					this->dragEventData->state = DragEventData::DS_DRAG_END;
				}
			}
			else
			{
				if (this->background->getRectangle()->intersects(cursorPos))
				{
					GlobalEventManager::getSingleton().trigger(Event(GuiButton::ButtonHitEvent, oBadPointer(this)));
				}
				this->dragEventData->state = DragEventData::DS_DRAG_ABORT;
			}
			this->decor->setPosition(this->initialDecorPosition.x, this->initialDecorPosition.y);
			GlobalEventManager::getSingleton().trigger(Event(this->dragEvent));
		}
		else if (this->state == GuiDragDropButton::DDBS_DOWN)
		{
			this->setState(GuiDragDropButton::DDBS_OVER);
			GlobalEventManager::getSingleton().trigger(Event(GuiButton::ButtonHitEvent, oBadPointer(this)));
		}
	}
	//---------------------------------------------------------
	void GuiDragDropButton::onCursorMoved(Ogre::Vector2 const & cursorPos)
	{
		if (this->isActionButton)
		{
			return;
		}
		else if (this->state == GuiDragDropButton::DDBS_DOWN)
		{
			if(this->background->getRectangle()->intersects(cursorPos))
			{
				this->startDragging(cursorPos);
				this->dragging(cursorPos);
			}
		}
		else if (this->state == GuiDragDropButton::DDBS_DRAGGING)
		{
			this->dragging(cursorPos);
		}
	}
	//---------------------------------------------------------
	String GuiDragDropButton::getCaption()
	{
		if(label)
			return this->label->getCaption()->text();	
		else
			return StringUtil::BLANK;
	}
	//---------------------------------------------------------
	void GuiDragDropButton::setCaption(String const & text)
	{
		if(label)
			this->label->setText(text);
	}
	//---------------------------------------------------------
	void GuiDragDropButton::setIsActionButton(bool _isActionButton)
	{
		this->isActionButton = _isActionButton;
	}
	//---------------------------------------------------------
	void GuiDragDropButton::setIsRightSide(bool _isRightSide)
	{
		this->isRightSide = _isRightSide;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void GuiDragDropButton::setState(const GuiDragDropButton::DragDropButtonState& bs)
	{
		if (bs == GuiDragDropButton::DDBS_OVER)
		{
			this->decor->setSprite(this->baseSpriteName + "_over");
		}
		else if (bs == GuiDragDropButton::DDBS_UP)
		{
			this->decor->setSprite(this->baseSpriteName);
		}
		else
		{
			this->decor->setSprite(this->baseSpriteName + "_down");
		}
	
		this->state = bs;
	}
	//---------------------------------------------------------
	void	GuiDragDropButton::dragging	(const Ogre::Vector2& cursorPos)
	{
		if (this->isEnabled&& !this->isFreezed)
		{
			this->decor->setPosition(cursorPos.x - this->imageToCursorOffset.x,
				cursorPos.y - this->imageToCursorOffset.y);
		
		
	
			if ((this->isRightSide && (cursorPos.x < this->background->getRectangle()->left()) )
				||  (!this->isRightSide && (cursorPos.x > this->background->getRectangle()->left() + this->background->getRectangle()->width())) )
			{
				this->dragEventData->state = DragEventData::DS_DRAGGING;
				this->dragEventData->position = cursorPos;
				GlobalEventManager::getSingleton().trigger(Event(this->dragEvent));
			}
			else
			{
				this->dragEventData->state = DragEventData::DS_DRAGGING;
				this->dragEventData->position = Ogre::Vector2(-54321, -54321);
				GlobalEventManager::getSingleton().trigger(Event(this->dragEvent));
			}
		}
		
	}
	//---------------------------------------------------------
	void	GuiDragDropButton::startDragging	(const Ogre::Vector2& cursorPos)
	
	{

		this->setState(GuiDragDropButton::DDBS_DRAGGING);
	
		this->initialDecorPosition = this->decor->getPosition();
	
// 		this->imageToCursorOffset = Ogre::Vector2(
// 			cursorPos.x - this->decor->getPosition().x,
// 			cursorPos.y - this->decor->getPosition().y );

// 		this->imageToCursorOffset =  Ogre::Vector2(
// 			 this->decor->getSize().x/2.0f,
// 			this->decor->getSize().x*0.5f + this->decor->getSize().y/2.0f );

		this->imageToCursorOffset =  Ogre::Vector2(
			this->decor->getSize().x/2.0f,
			this->decor->getSize().y);



	
		this->dragEventData->state = DragEventData::DS_DRAG_START;
		this->dragEventData->position = cursorPos;
		GlobalEventManager::getSingleton().trigger(Event(this->dragEvent));

		
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(GuiDragDropButton)
	OOBJECT_END
}