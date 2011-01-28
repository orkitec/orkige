/********************************************************************
	created:	Monday 2010/11/01
	filename:	FastGuiDragDropButton.cpp
	author:		philipp.engelhard
	notice:		based on Orkige::FastGuiButton
	copyright:	(c) 2010 kunst-stoff
*********************************************************************/

#include "engine_fastgui/FastGuiDragDropButton.h"
#include "engine_fastgui/DragEventData.h"
#include "engine_fastgui/FastGuiManager.h"
#include "core_event/GlobalEventManager.h"
#include <engine_sound/SoundManager.h>

namespace Orkige
{
	
	IMPL_OWNED_EVENTTYPE(FastGuiDragDropButton, DragEndEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	FastGuiDragDropButton::FastGuiDragDropButton(String const & id,
												 String const & spriteName,
												 uint defaultGlyphIndex, 
												 String const & text, 
												 Ogre::Vector2 const & position, 
												 FastGuiLabel::LabelAlignment textAlignment,
												 Ogre::Vector2 const & size,
												 String const & atlas,
												 uint z)
		:
		FastGuiWidget(id, atlas, z),
		dragEvent(FastGuiDragDropButton::DragEndEvent),
		dragEventData(),
		imageToCursorOffset(),
		initialDecorPosition(position),
		initialWidgetPosition(position)
	{
		this->background = onew(new FastGuiDecorWidget(id + ".background", spriteName, position, size, atlas, z));
		this->decor = onew(new FastGuiDecorWidget(id + ".decor", spriteName, position, size, atlas, z));
		
		this->label = onew(new FastGuiLabel(id + ".label", defaultGlyphIndex, text, position, atlas, z));
		this->label->setSize(this->decor->getSize().x, this->decor->getSize().y);
		this->label->setAlignment(textAlignment);
	
		this->state = DDBS_UP;
		this->baseSpriteName = spriteName;
	
		this->dragEventData = onew(new DragEventData());
		this->dragEventData->button = oBadPointer(this);
		this->dragEvent.setData(this->dragEventData);

		this->isEnabled = true;
	}
	//---------------------------------------------------------
	FastGuiDragDropButton::~FastGuiDragDropButton()
	{
	}
	//---------------------------------------------------------
	const FastGuiDragDropButton::DragDropButtonState& FastGuiDragDropButton::getState() 
	{ 
		return this->state; 
	}
	//---------------------------------------------------------
	void FastGuiDragDropButton::setPosition(Ogre::Real left, Ogre::Real top)
	{
		this->decor->setPosition(left, top);
		this->label->setPosition(left, top);
	}
	//---------------------------------------------------------
	void FastGuiDragDropButton::setSize(Ogre::Real width, Ogre::Real height)
	{
		this->decor->setSize(width, height);
		this->label->setSize(width, height);
	}
	//---------------------------------------------------------
	Ogre::Vector2 FastGuiDragDropButton::getSize()
	{
		return this->decor->getSize();
	}
	//---------------------------------------------------------
	Ogre::Vector2 FastGuiDragDropButton::getPosition()
	{
		return this->decor->getPosition();
	}
	//---------------------------------------------------------
	void FastGuiDragDropButton::onCursorPressed(Ogre::Vector2 const & cursorPos)
	{
		

		if ( this->state == FastGuiDragDropButton::DDBS_DRAGGING )
		{
			// this can only be another mouse button than the one
			// we started dragging with, so --> abort dragging
			
			this->setState(FastGuiDragDropButton::DDBS_OVER);
	
			if (this->isEnabled)
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
			//this->setState(FastGuiDragDropButton::DDBS_DOWN);
			this->startDragging(cursorPos);
			SoundManager::getSingleton().playSound("pickcard");
		}
	}
	//---------------------------------------------------------
	void FastGuiDragDropButton::onCursorReleased(Ogre::Vector2 const & cursorPos)
	{
		if (this->state == FastGuiDragDropButton::DDBS_DRAGGING)
		{
			this->setState(FastGuiDragDropButton::DDBS_OVER);
	
			this->dragEventData->position = cursorPos;
			if (this->isEnabled)
			{
				//float distan = initialDecorPosition.distance(this->decor->getPosition()) ;
				float distan = Ogre::Math::Abs(initialDecorPosition.x) - Ogre::Math::Abs(this->decor->getPosition().x) ;

				if (Ogre::Math::Abs(distan) > 45.0f)
				{
					this->dragEventData->state = DragEventData::DS_DRAG_END;
				}
				else
				{
					this->dragEventData->state = DragEventData::DS_DRAG_ABORT;
				}
				
			}
			else
			{
				this->dragEventData->state = DragEventData::DS_DRAG_ABORT;
			}
			this->decor->setPosition(this->initialDecorPosition.x, this->initialDecorPosition.y);
			GlobalEventManager::getSingleton().trigger(Event(this->dragEvent));
		}
		else if (this->state == FastGuiDragDropButton::DDBS_DOWN)
		{
			this->setState(FastGuiDragDropButton::DDBS_OVER);
			GlobalEventManager::getSingleton().trigger(Event(FastGuiButton::ButtonHitEvent, oBadPointer(this)));
		}
	}
	//---------------------------------------------------------
	void FastGuiDragDropButton::onCursorMoved(Ogre::Vector2 const & cursorPos)
	{
		if (this->state == FastGuiDragDropButton::DDBS_DOWN)
		{
			if(this->background->getRectangle()->intersects(cursorPos))
			{
				this->startDragging(cursorPos);
			}
		}
		else if (this->state == FastGuiDragDropButton::DDBS_DRAGGING)
		{
			this->dragging(cursorPos);
		}
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void FastGuiDragDropButton::setState(const FastGuiDragDropButton::DragDropButtonState& bs)
	{
		if (bs == FastGuiDragDropButton::DDBS_OVER)
		{
			this->decor->setSprite(this->baseSpriteName + "_Over");
		}
		else if (bs == FastGuiDragDropButton::DDBS_UP)
		{
			this->decor->setSprite(this->baseSpriteName);
		}
		else
		{
			this->decor->setSprite(this->baseSpriteName + "_Down");
		}
	
		this->state = bs;
	}
	//---------------------------------------------------------
	void	FastGuiDragDropButton::dragging	(const Ogre::Vector2& cursorPos)
	{
		if (this->isEnabled)
		{
			this->decor->setPosition(cursorPos.x - this->imageToCursorOffset.x,
				cursorPos.y - this->imageToCursorOffset.y);
		
		
	
		this->dragEventData->state = DragEventData::DS_DRAGGING;
		this->dragEventData->position = cursorPos;
		GlobalEventManager::getSingleton().trigger(Event(this->dragEvent));}
		
	}
	//---------------------------------------------------------
	void	FastGuiDragDropButton::startDragging	(const Ogre::Vector2& cursorPos)
	
	{

		this->setState(FastGuiDragDropButton::DDBS_DRAGGING);
	
		this->initialDecorPosition = this->decor->getPosition();
	
// 		this->imageToCursorOffset = Ogre::Vector2(
// 			cursorPos.x - this->decor->getPosition().x,
// 			cursorPos.y - this->decor->getPosition().y );

		this->imageToCursorOffset =  Ogre::Vector2(
			 this->decor->getSize().x/2.0f,
			this->decor->getSize().x*0.5f + this->decor->getSize().y/2.0f );



	
		this->dragEventData->state = DragEventData::DS_DRAG_START;
		this->dragEventData->position = cursorPos;
		GlobalEventManager::getSingleton().trigger(Event(this->dragEvent));
		
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(FastGuiDragDropButton)
	OOBJECT_END
}