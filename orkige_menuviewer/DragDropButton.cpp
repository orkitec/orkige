/********************************************************************
	created:	Monday 2010/10/11
	filename:	DragDropButton.cpp
	author:		philipp.engelhard
	notice:		
	copyright:	(c) 2010 kunst-stoff
*********************************************************************/

#include <core_event/GlobalEventManager.h>
#include <core_game/GameObjectManager.h>
#include <engine_graphic/Engine.h>
//#include "cc_components/JunctionPickerComponent.h"
#include "cc_game/PickingMasks.h"
#include "cc_gui/DragDropButton.h"
#include "engine_gui/GuiManager.h"
#include "engine_physic/CollisionTools.h"

using namespace Orkige;

namespace CC
{
	IMPL_OWNED_EVENTTYPE(DragDropButton, ButtonHitEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	DragDropButton::DragDropButton(
		const Orkige::String& name,
		const Orkige::String& materialGroup,
		const Orkige::String& templateName,
		const Ogre::DisplayString& caption,
		Ogre::Real width)
	:
	Widget(name, materialGroup),
	isDragging(false),
	pickedObject(0),
	dragee(0),
	imageOverlayContainer(0),
	imageToCursorOffset(),
	pickEvent(NULL),
	//pickEvent(JunctionPickerComponent::JunctionPickEvent),
	pickEventData()
	{
		this->overlayElement = Ogre::OverlayManager::getSingleton().createOverlayElementFromTemplate(materialGroup + "/" + templateName, "", name);
		this->borderPanel = (Ogre::BorderPanelOverlayElement*)this->overlayElement;
		this->textArea = (Ogre::TextAreaOverlayElement*)this->borderPanel->getChild(this->borderPanel->getName() + "/DragDropButtonCaption");
		this->imageOverlay = (Ogre::OverlayElement*)this->borderPanel->getChild(this->borderPanel->getName() + "/DragDropButtonImage");

#if OGRE_PLATFORM == OGRE_PLATFORM_IPHONE
		this->textArea->setCharHeight(this->textArea->getCharHeight() - 3);
#endif

		this->textArea->setTop(-(this->textArea->getCharHeight() / 2));

		//if (width > 0)
		//{
		//	this->overlayElement->setWidth(width);
		//	this->fitToContents = false;
		//}
		//else this->fitToContents = true;

		this->fitToContents = false;

		this->setCaption(caption);
		this->state = BS_UP;

		this->imageOverlayContainer = this->imageOverlay->getParent();
		this->imageOverlay->setMetricsMode(Ogre::GMM_PIXELS);

//		this->pickEventData = onew(new CardPickEventData());
	}
	//---------------------------------------------------------
	DragDropButton::~DragDropButton()
	{
	}
	//---------------------------------------------------------
	const Ogre::DisplayString& DragDropButton::getCaption()
	{
		return this->textArea->getCaption();
	}
	//---------------------------------------------------------
	void DragDropButton::setCaption(const Ogre::DisplayString& caption)
	{
		this->textArea->setCaption(caption);
		if (this->fitToContents) 
		{
			this->overlayElement->setWidth(OverlayUtil::getCaptionWidth(caption, this->textArea) + this->overlayElement->getHeight() - 12);
		}
	}
	//---------------------------------------------------------
	const DragDropButton::ButtonState& DragDropButton::getState() 
	{ 
		return this->state; 
	}
	//---------------------------------------------------------
	void DragDropButton::onCursorPressed(const Ogre::Vector2& cursorPos)
	{
		if (OverlayUtil::isCursorOver(this->overlayElement, cursorPos, 4)) 
		{
			this->setState(BS_DOWN);
		}
	}
	//---------------------------------------------------------
	void DragDropButton::onCursorReleased(const Ogre::Vector2& cursorPos)
	{
		if (this->state == BS_DOWN)
		{
			this->setState(BS_OVER);
			GuiManager::getSingleton().getEventManager()->trigger(Event(::Orkige::Button::ButtonHitEvent, oBadPointer(this)));
		}

		if (this->isDragging == true)
		{
			oDebugMsg("philipp", 0, "Stopped Dragging at: " << cursorPos.x << "@" << cursorPos.y);
			this->StopDragging();
			this->PickAt(cursorPos);
		}
	}
	//---------------------------------------------------------
	void DragDropButton::onCursorMoved(const Ogre::Vector2& cursorPos)
	{
		if (OverlayUtil::isCursorOver(this->overlayElement, cursorPos, 4))
		{
			if (this->state == BS_UP) this->setState(BS_OVER);
		}
		else
		{
			if (this->state != BS_UP) this->setState(BS_UP);
		}

		if (this->state == BS_DOWN)
		{
			if (this->isDragging == false)
			{
				this->StartDragging( cursorPos );
			}
			else
			{
				oDebugMsg("philipp", 0, "Dragging");
			}
		}

		if (this->isDragging)
		{
			this->imageOverlay->setPosition( cursorPos.x - this->imageToCursorOffset.x,
											 cursorPos.y - this->imageToCursorOffset.y);
		}

	}
	//---------------------------------------------------------
	void DragDropButton::onFocusLost()
	{
		this->setState(BS_UP);   // reset button if cursor was lost
		oDebugMsg("philipp", 0, "onFocusLost");
		this->StopDragging();
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void DragDropButton::setState(const DragDropButton::ButtonState& bs)
	{
		if (bs == DragDropButton::BS_OVER)
		{
			this->borderPanel->setBorderMaterialName(this->materialGroup + "/Button/Over");
			this->borderPanel->setMaterialName(this->materialGroup + "/Button/Over");
		}
		else if (bs == DragDropButton::BS_UP)
		{
			this->borderPanel->setBorderMaterialName(this->materialGroup + "/Button/Up");
			this->borderPanel->setMaterialName(this->materialGroup + "/Button/Up");
		}
		else
		{
			this->borderPanel->setBorderMaterialName(this->materialGroup + "/Button/Down");
			this->borderPanel->setMaterialName(this->materialGroup + "/Button/Down");
		}

		this->state = bs;
	}
	//---------------------------------------------------------
	void DragDropButton::PickAt (const Ogre::Vector2& cursorPos)
	{
		oDebugMsg("philipp", 0, "PickAt: " << cursorPos.x << "@" << cursorPos.y);

		Ogre::Vector3 result;
		Ogre::Real closestDistance = 100000.0f;		// is that far enough?

		bool hit = Orkige::CollisionTools::getSingleton().raycastFromCamera(
			Engine::getSingleton().getRenderWindow(),
			Engine::getSingleton().getCamera(),
			cursorPos,
			result,
			this->pickedObject,
			closestDistance,
			PICK_JUNCTION);

		if (hit)
		{

			oDebugMsg("philipp", 0, "picked object: " << this->pickedObject->getName());
			//this->dragee = Engine::getSingleton().getSceneManager()->getEntity("TestSelector");
			//if (this->dragee)
			//{
			//	this->dragee->getParentSceneNode()->setVisible(true);
			//	Ogre::Vector3 oldPosition = this->dragee->getParentSceneNode()->getPosition();
			//	this->dragee->getParentSceneNode()->translate(result - oldPosition);
			//}
			//else
			//{
			//	oDebugMsg("philipp", 0, "no dragee found");
			//}

			//this->pickEventData->action = CardPickEventData::PEA_TURN_RIGHT;
			this->pickEventData->object = this->pickedObject;
			this->pickEvent.setData(this->pickEventData);
			Orkige::GameObjectManager::getSingleton().triggerEvent(Orkige::Event(this->pickEvent));
		}
		else
		{
			oDebugMsg("philipp", 0, "nothing picked");
		}

	}
	//---------------------------------------------------------
	void	DragDropButton::StartDragging( const Ogre::Vector2& cursorPos )
	{
		Ogre::OverlayManager& om = Ogre::OverlayManager::getSingleton();
		this->imageToCursorOffset = Ogre::Vector2(
			cursorPos.x - (this->imageOverlay->_getDerivedLeft() * om.getViewportWidth()),
			cursorPos.y - (this->imageOverlay->_getDerivedTop() * om.getViewportHeight())
			);

		this->imageOverlay->_setParent(NULL);
		this->imageOverlay->setHorizontalAlignment(Ogre::GHA_LEFT);
		this->imageOverlay->setVerticalAlignment(Ogre::GVA_TOP);
		this->isDragging = true;
		oDebugMsg("philipp", 0, "Start Dragging");
	}
	//---------------------------------------------------------
	void	DragDropButton::StopDragging()
	{
		this->imageOverlay->_setParent(this->imageOverlayContainer);
		this->imageOverlay->setPosition(0.f, 0.f);
		this->isDragging = false;
		oDebugMsg("philipp", 0, "Stopped Dragging");
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OABSTRACT_IMPL(DragDropButton)
	OOBJECT_END
}