/********************************************************************
	created:	Saturday 2026/07/26 at 12:30
	filename: 	GuiListView.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_gui/GuiListView.h"
#include "engine_gui/GuiDecorWidget.h"
#include "engine_gui/GuiLabel.h"
#include "engine_gui/GuiManager.h"

#include <algorithm>
#include <sstream>

namespace Orkige
{
	//---------------------------------------------------------
	GuiListView::GuiListView(String const & id, Ogre::Vector2 const & position,
		Ogre::Vector2 const & size, String const & atlas, uint z)
		: GuiScrollView(id, position, size, atlas, z), viewAtlas(atlas),
		itemFont(9), contentZ(z), itemSerial(0)
	{
	}
	//---------------------------------------------------------
	GuiListView::~GuiListView()
	{
	}
	//---------------------------------------------------------
	void GuiListView::initContent(uint fontIndex)
	{
		this->itemFont = fontIndex;
		if(!this->contentId.empty())
		{
			return;		// already built
		}
		GuiManager & manager = GuiManager::getSingleton();
		this->contentId = this->getObjectID() + ".content";
		// the inner content container: a spriteless (transparent) vertical layout
		// group anchored to stretch across the top of the viewport and grow down
		// with its items (content-size-fit vertical=preferred), exactly the scroll
		// content shape the settings screen authors by hand
		optr<GuiDecorWidget> content = onew(new GuiDecorWidget(this->contentId,
			"none", Ogre::Vector2(0, 0), Ogre::Vector2(100, 100), this->viewAtlas,
			this->contentZ));
		manager.addWidget(content);
		content->setColour(0.0f, 0.0f, 0.0f, 0.0f);	// invisible - it only lays out
		content->setParent(this->sharedSelf());
		content->setAnchorPreset("stretchtop");
		content->setOffsets(0.0f, 0.0f, 0.0f, 0.0f);
		content->setLayoutGroup("vertical");
		content->setGroupPadding(8.0f, 8.0f, 8.0f, 8.0f);
		content->setGroupSpacing(8.0f);
		content->setChildForceExpand(true);
		content->setContentSizeFit("none", "preferred");
	}
	//---------------------------------------------------------
	String GuiListView::addItem(String const & text)
	{
		if(this->contentId.empty())
		{
			return String();
		}
		GuiManager & manager = GuiManager::getSingleton();
		optr<GuiWidget> content = manager.getWidget(this->contentId).lock();
		if(!content)
		{
			return String();
		}
		std::ostringstream idStream;
		idStream << this->getObjectID() << ".item." << this->itemSerial++;
		const String itemId = idStream.str();
		optr<GuiLabel> item = onew(new GuiLabel(itemId, this->itemFont, text,
			Ogre::Vector2(0, 0), this->viewAtlas, this->contentZ, true));
		manager.addWidget(item);
		// a layout child of the vertical group: the group arranges it and the list
		// re-flows (the resolver picks up the new preferred content extent)
		item->setParent(content);
		this->itemIds.push_back(itemId);
		manager.markLayoutDirty();
		return itemId;
	}
	//---------------------------------------------------------
	bool GuiListView::removeItem(String const & itemId)
	{
		std::vector<String>::iterator it =
			std::find(this->itemIds.begin(), this->itemIds.end(), itemId);
		if(it == this->itemIds.end())
		{
			return false;
		}
		GuiManager & manager = GuiManager::getSingleton();
		manager.destroyWidget(itemId);
		this->itemIds.erase(it);
		manager.markLayoutDirty();
		return true;
	}
	//---------------------------------------------------------
	void GuiListView::clear()
	{
		GuiManager & manager = GuiManager::getSingleton();
		for(String const & itemId : this->itemIds)
		{
			manager.destroyWidget(itemId);
		}
		this->itemIds.clear();
		manager.markLayoutDirty();
	}
	//---------------------------------------------------------
	String GuiListView::getItemId(int index) const
	{
		if(index < 0 || index >= static_cast<int>(this->itemIds.size()))
		{
			return String();
		}
		return this->itemIds[static_cast<std::size_t>(index)];
	}
	//---------------------------------------------------------
	OABSTRACT_IMPL(GuiListView)
		OFUNC(addItem)
		OFUNC(removeItem)
		OFUNC(clear)
		OFUNC(getItemCount)
		OFUNC(getItemId)
	OOBJECT_END
}
