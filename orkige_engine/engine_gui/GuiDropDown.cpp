/********************************************************************
	created:	Saturday 2026/07/11 at 19:15
	filename: 	GuiDropDown.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_gui/GuiDropDown.h"
#include "engine_gui/GuiManager.h"
#include "engine_gui/GuiScrollView.h"

#include <OgreStringConverter.h>
#include <algorithm>

namespace Orkige
{
	//---------------------------------------------------------
	GuiDropDown::GuiDropDown(String const & id, String const & spriteName,
		uint defaultGlyphIndex, String const & text,
		Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment,
		Ogre::Vector2 const & size, String const & atlas, uint z)
		: GuiButton(id, spriteName, defaultGlyphIndex, text, position,
			textAlignment, size, atlas, z, false),
		selectedIndex(0), fontIndex(defaultGlyphIndex), menuOpen(false),
		wantOpen(false)
	{
	}
	//---------------------------------------------------------
	GuiDropDown::~GuiDropDown()
	{
	}
	//---------------------------------------------------------
	void GuiDropDown::setItems(Ogre::StringVector const & _items)
	{
		this->items = _items;
		this->selectedIndex = 0;
		if(!this->items.empty())
		{
			this->setCaption(this->items[0]);
		}
	}
	//---------------------------------------------------------
	String GuiDropDown::getSelectedItem() const
	{
		if(this->selectedIndex < this->items.size())
		{
			return this->items[this->selectedIndex];
		}
		return StringUtil::BLANK;
	}
	//---------------------------------------------------------
	void GuiDropDown::selectIndex(std::size_t index)
	{
		if(index >= this->items.size())
		{
			return;
		}
		this->selectedIndex = index;
		this->setCaption(this->items[index]);
	}
	//---------------------------------------------------------
	void GuiDropDown::onCursorReleased(Ogre::Vector2 const & cursorPos)
	{
		GuiButton::onCursorReleased(cursorPos);
		// a completed click toggles the list; the actual open/close touches the
		// widget list, so defer it out of this dispatch loop
		if(this->wasClicked())
		{
			if(this->menuOpen)
			{
				this->closeMenu();
			}
			else if(!this->wantOpen)
			{
				this->wantOpen = true;
				GuiManager::getSingleton().runDeferred([this]()
				{
					this->openMenu();
				});
			}
		}
	}
	//---------------------------------------------------------
	bool GuiDropDown::onFrameStarted(FrameEventData const & data)
	{
		GuiButton::onFrameStarted(data);
		if(this->menuOpen)
		{
			// an outside tap light-dismissed the list: its modal record (and its
			// widgets) are gone, so reflect the closed state; otherwise poll the
			// options for a pick
			if(GuiManager::getSingleton().getModalContentZ(this->menuModalId) == 0)
			{
				this->menuOpen = false;
				this->optionIds.clear();
			}
			else
			{
				this->pollOptions();
			}
		}
		return false;
	}
	//---------------------------------------------------------
	void GuiDropDown::openMenu()
	{
		this->wantOpen = false;
		if(this->menuOpen || this->items.empty())
		{
			return;
		}
		GuiManager & manager = GuiManager::getSingleton();
		this->menuModalId = manager.showModal(this->getObjectID() + ".menu", true);
		const uint z = manager.getModalContentZ(this->menuModalId);
		optr<GuiFactory> factory = manager.getFactory().lock();
		if(!factory)
		{
			return;
		}
		const Ogre::Vector2 pos = this->getPosition();
		const Ogre::Vector2 size = this->getSize();
		const float rowH = size.y;
		// cap the visible list to a handful of rows; the rest scrolls
		const std::size_t visibleRows =
			std::min<std::size_t>(this->items.size(), 6);
		const Ogre::Vector2 listPos(pos.x, pos.y + size.y);
		const Ogre::Vector2 listSize(size.x, rowH * float(visibleRows));

		optr<GuiScrollView> scroll = factory->createScrollView(
			this->menuModalId + ".scroll", listPos, listSize,
			manager.getDefaultAtlas(), z).lock();
		manager.registerModalWidget(this->menuModalId,
			this->menuModalId + ".scroll");

		// a transparent vertical group holds the option buttons and overflows the
		// viewport (so a long list scrolls) - the proven .oui scroll pattern
		optr<GuiDecorWidget> content = factory->createDecorWidget(
			this->menuModalId + ".content", "none", listPos,
			Ogre::Vector2(size.x, rowH * float(this->items.size())),
			manager.getDefaultAtlas(), z).lock();
		if(content && scroll)
		{
			content->setColour(0.0f, 0.0f, 0.0f, 0.0f);
			content->setParent(scroll);
			content->setAnchorPreset("stretchtop");
			content->setOffsets(0.0f, 0.0f, 0.0f, 0.0f);
			content->setLayoutGroup("vertical");
			content->setGroupSpacing(0.0f);
			content->setChildForceExpand(true);
			content->setContentSizeFit("none", "preferred");
		}
		manager.registerModalWidget(this->menuModalId,
			this->menuModalId + ".content");

		this->optionIds.clear();
		for(std::size_t i = 0; i < this->items.size(); ++i)
		{
			const String optionId = this->menuModalId + ".opt" +
				Ogre::StringConverter::toString(static_cast<unsigned int>(i));
			optr<GuiButton> option = factory->createButton(optionId, "button",
				this->fontIndex, this->items[i], listPos, GuiLabel::LA_CENTER,
				Ogre::Vector2(size.x, rowH), manager.getDefaultAtlas(), z, false,
				GuiButtonBlink::BBLINK_NONE).lock();
			if(option && content)
			{
				option->setParent(content);
			}
			manager.registerModalWidget(this->menuModalId, optionId);
			this->optionIds.push_back(optionId);
		}
		manager.reorderViews();
		this->menuOpen = true;
	}
	//---------------------------------------------------------
	void GuiDropDown::closeMenu()
	{
		if(!this->menuOpen)
		{
			return;
		}
		GuiManager::getSingleton().dismissModal(this->menuModalId);
		this->menuOpen = false;
		this->optionIds.clear();
	}
	//---------------------------------------------------------
	void GuiDropDown::pollOptions()
	{
		GuiManager & manager = GuiManager::getSingleton();
		for(std::size_t i = 0; i < this->optionIds.size(); ++i)
		{
			if(!manager.widgetExists(this->optionIds[i]))
			{
				continue;
			}
			optr<GuiButton> option =
				manager.getWidgetAs<GuiButton>(this->optionIds[i]).lock();
			if(option && option->wasClicked())
			{
				this->selectIndex(i);
				manager.dismissModal(this->menuModalId);
				this->menuOpen = false;
				this->optionIds.clear();
				return;
			}
		}
	}
	//---------------------------------------------------------
	OABSTRACT_IMPL(GuiDropDown)
		OFUNC(setItems)
		OFUNC(getSelectedIndex)
		OFUNC(getSelectedItem)
		OFUNC(selectIndex)
		OFUNC(isMenuOpen)
	OOBJECT_END
}
