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
	namespace
	{
		//! inner padding of the content container (design units)
		const float LIST_VIEW_PADDING = 8.0f;
		//! gap between the flowed list's rows (design units)
		const float LIST_VIEW_SPACING = 8.0f;
		//! the row height a virtualized list falls back to when none was set
		const float LIST_VIEW_DEFAULT_ITEM_HEIGHT = 32.0f;
	}
	const int GuiListView::VIRTUAL_OVERSCAN = 1;
	//---------------------------------------------------------
	GuiListView::GuiListView(String const & id, Ogre::Vector2 const & position,
		Ogre::Vector2 const & size, String const & atlas, uint z)
		: GuiScrollView(id, position, size, atlas, z), viewAtlas(atlas),
		itemFont(9), contentZ(z), itemSerial(0), virtualized(false),
		itemHeight(LIST_VIEW_DEFAULT_ITEM_HEIGHT)
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
		// the inner content container: a spriteless (transparent) container
		// anchored to stretch across the top of the viewport and grow down with
		// its rows, exactly the scroll content shape the settings screen authors
		// by hand
		optr<GuiDecorWidget> content = onew(new GuiDecorWidget(this->contentId,
			"none", Ogre::Vector2(0, 0), Ogre::Vector2(100, 100), this->viewAtlas,
			this->contentZ));
		manager.addWidget(content);
		content->setColour(0.0f, 0.0f, 0.0f, 0.0f);	// invisible - it only lays out
		content->setParent(this->sharedSelf());
		content->setAnchorPreset("stretchtop");
		content->setOffsets(0.0f, 0.0f, 0.0f, 0.0f);
		this->applyContentMode();
	}
	//---------------------------------------------------------
	void GuiListView::applyContentMode()
	{
		if(this->contentId.empty())
		{
			return;
		}
		GuiManager & manager = GuiManager::getSingleton();
		optr<GuiWidget> content = manager.getWidget(this->contentId).lock();
		if(!content)
		{
			return;
		}
		if(this->virtualized)
		{
			// a PLAIN container: the rows anchor themselves at their virtual
			// offsets, so the arrangement never depends on the child order (a
			// materialised row may join the container at any time)
			content->setLayoutGroup("none");
			content->setContentSizeFit("none", "none");
			this->applyContentExtent();
		}
		else
		{
			// a vertical layout group with content-size-fit: the group arranges
			// every row and the list re-flows as rows come and go
			content->setLayoutGroup("vertical");
			content->setGroupPadding(LIST_VIEW_PADDING, LIST_VIEW_PADDING,
				LIST_VIEW_PADDING, LIST_VIEW_PADDING);
			content->setGroupSpacing(LIST_VIEW_SPACING);
			content->setChildForceExpand(true);
			content->setContentSizeFit("none", "preferred");
			content->setOffsets(0.0f, 0.0f, 0.0f, 0.0f);
		}
		manager.markLayoutDirty();
	}
	//---------------------------------------------------------
	void GuiListView::applyContentExtent()
	{
		if(!this->virtualized || this->contentId.empty())
		{
			return;
		}
		GuiManager & manager = GuiManager::getSingleton();
		optr<GuiWidget> content = manager.getWidget(this->contentId).lock();
		if(!content)
		{
			return;
		}
		// the container spans EVERY row, materialised or not, so the scroll
		// extent (and the clamp that rides on it) covers the whole model
		const float total = 2.0f * LIST_VIEW_PADDING +
			float(this->itemIds.size()) * this->itemHeight;
		content->setOffsets(0.0f, 0.0f, 0.0f, total);
		// mirror it into the live rect too, so the scroll view is handed the new
		// extent in the SAME resolve rather than one frame later
		const float scale = manager.getLayoutScale();
		content->setSize(content->getSize().x, total * scale);
		manager.markLayoutDirty();
	}
	//---------------------------------------------------------
	void GuiListView::setVirtualized(bool enable)
	{
		if(this->virtualized == enable)
		{
			return;
		}
		this->releaseAll();
		this->virtualized = enable;
		this->applyContentMode();
		if(this->virtualized)
		{
			this->updateWindow();
		}
		else
		{
			// the flowed list materialises every row again
			for(int each = 0; each < this->getItemCount(); ++each)
			{
				this->materializeItem(each);
			}
			this->materialized.first = 0;
			this->materialized.count = this->getItemCount();
		}
	}
	//---------------------------------------------------------
	void GuiListView::setItemHeight(float height)
	{
		if(height <= 0.0f || this->itemHeight == height)
		{
			return;
		}
		this->itemHeight = height;
		if(!this->virtualized)
		{
			return;
		}
		// every row's virtual offset moved: drop the window and rebuild it
		this->releaseAll();
		this->applyContentExtent();
		this->updateWindow();
	}
	//---------------------------------------------------------
	String GuiListView::addItem(String const & text)
	{
		if(this->contentId.empty())
		{
			return String();
		}
		std::ostringstream idStream;
		idStream << this->getObjectID() << ".item." << this->itemSerial++;
		const String itemId = idStream.str();
		this->itemIds.push_back(itemId);
		this->itemTexts.push_back(text);
		if(this->virtualized)
		{
			this->applyContentExtent();
			this->updateWindow();
			return itemId;
		}
		this->materializeItem(this->getItemCount() - 1);
		this->materialized.count = this->getItemCount();
		GuiManager::getSingleton().markLayoutDirty();
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
		const std::size_t index =
			std::size_t(std::distance(this->itemIds.begin(), it));
		// every row after this one shifts up, so the whole window is stale
		this->releaseAll();
		this->itemIds.erase(it);
		this->itemTexts.erase(this->itemTexts.begin() + std::ptrdiff_t(index));
		if(this->virtualized)
		{
			this->applyContentExtent();
			this->updateWindow();
		}
		else
		{
			for(int each = 0; each < this->getItemCount(); ++each)
			{
				this->materializeItem(each);
			}
			this->materialized.first = 0;
			this->materialized.count = this->getItemCount();
		}
		GuiManager::getSingleton().markLayoutDirty();
		return true;
	}
	//---------------------------------------------------------
	void GuiListView::clear()
	{
		this->releaseAll();
		this->itemIds.clear();
		this->itemTexts.clear();
		this->applyContentExtent();
		GuiManager::getSingleton().markLayoutDirty();
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
	String GuiListView::getItemText(int index) const
	{
		if(index < 0 || index >= static_cast<int>(this->itemTexts.size()))
		{
			return String();
		}
		return this->itemTexts[static_cast<std::size_t>(index)];
	}
	//---------------------------------------------------------
	int GuiListView::getMaterializedCount() const
	{
		if(GuiManager::getSingletonPtr() == NULL)
		{
			return 0;
		}
		GuiManager & manager = GuiManager::getSingleton();
		int live = 0;
		for(String const & id : this->itemIds)
		{
			if(manager.widgetExists(id))
			{
				++live;
			}
		}
		return live;
	}
	//---------------------------------------------------------
	void GuiListView::onLayoutResolved(Ogre::Vector2 const & viewportSize,
		Ogre::Vector2 const & contentExtent)
	{
		GuiScrollView::onLayoutResolved(viewportSize, contentExtent);
		// the viewport height just settled: it decides how many rows fit
		this->updateWindow();
	}
	//---------------------------------------------------------
	bool GuiListView::onFrameStarted(FrameEventData const & data)
	{
		const bool consumed = GuiScrollView::onFrameStarted(data);
		// a drag / flick moved the offset - follow it with the row window (a
		// no-op on every frame the window does not actually move)
		this->updateWindow();
		return consumed;
	}
	//---------------------------------------------------------
	void GuiListView::updateWindow()
	{
		if(!this->virtualized || this->contentId.empty() ||
			GuiManager::getSingletonPtr() == NULL)
		{
			return;
		}
		const float scale = GuiManager::getSingleton().getLayoutScale();
		const float rowPixels = this->itemHeight * scale;
		const ListWindow next = virtualWindow(this->getScroll(),
			float(this->viewportExtent), rowPixels, this->getItemCount(),
			VIRTUAL_OVERSCAN);
		if(next.first == this->materialized.first &&
			next.count == this->materialized.count)
		{
			return;		// the window did not move
		}
		// release the rows that left, materialise the ones that entered
		for(int each = this->materialized.first;
			each < this->materialized.end(); ++each)
		{
			if(!next.contains(each))
			{
				this->releaseItem(each);
			}
		}
		for(int each = next.first; each < next.end(); ++each)
		{
			if(!this->materialized.contains(each))
			{
				this->materializeItem(each);
			}
		}
		this->materialized = next;
		GuiManager::getSingleton().markLayoutDirty();
	}
	//---------------------------------------------------------
	void GuiListView::materializeItem(int index)
	{
		if(index < 0 || index >= this->getItemCount())
		{
			return;
		}
		GuiManager & manager = GuiManager::getSingleton();
		optr<GuiWidget> content = manager.getWidget(this->contentId).lock();
		if(!content)
		{
			return;
		}
		const std::size_t slot = std::size_t(index);
		if(manager.widgetExists(this->itemIds[slot]))
		{
			return;		// already live
		}
		optr<GuiLabel> item = onew(new GuiLabel(this->itemIds[slot],
			this->itemFont, this->itemTexts[slot], Ogre::Vector2(0, 0),
			this->viewAtlas, this->contentZ, true));
		manager.addWidget(item);
		item->setParent(content);
		if(this->virtualized)
		{
			// the row anchors itself at its VIRTUAL offset inside the container,
			// so it lands in the right place no matter when it was created
			const float top = LIST_VIEW_PADDING + float(index) * this->itemHeight;
			item->setAnchorPreset("stretchtop");
			item->setOffsets(LIST_VIEW_PADDING, top, -LIST_VIEW_PADDING,
				top + this->itemHeight);
		}
		manager.markLayoutDirty();
	}
	//---------------------------------------------------------
	void GuiListView::releaseItem(int index)
	{
		if(index < 0 || index >= this->getItemCount() ||
			GuiManager::getSingletonPtr() == NULL)
		{
			return;
		}
		GuiManager::getSingleton().destroyWidget(
			this->itemIds[std::size_t(index)]);
	}
	//---------------------------------------------------------
	void GuiListView::releaseAll()
	{
		if(GuiManager::getSingletonPtr() == NULL)
		{
			this->materialized = ListWindow();
			return;
		}
		GuiManager & manager = GuiManager::getSingleton();
		for(String const & id : this->itemIds)
		{
			manager.destroyWidget(id);
		}
		this->materialized = ListWindow();
		manager.markLayoutDirty();
	}
	//---------------------------------------------------------
	OABSTRACT_IMPL(GuiListView)
		OFUNC(addItem)
		OFUNC(removeItem)
		OFUNC(clear)
		OFUNC(getItemCount)
		OFUNC(getItemId)
		OFUNC(getItemText)
		OFUNC(setVirtualized)
		OFUNC(isVirtualized)
		OFUNC(setItemHeight)
		OFUNC(getItemHeight)
		OFUNC(getMaterializedCount)
		OFUNC(getFirstMaterializedIndex)
	OOBJECT_END
}
