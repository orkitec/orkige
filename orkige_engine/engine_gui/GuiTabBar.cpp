/********************************************************************
	created:	Saturday 2026/07/26 at 12:00
	filename: 	GuiTabBar.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_gui/GuiTabBar.h"
#include "engine_gui/GuiToggleGroup.h"
#include "engine_gui/GuiCheckBox.h"
#include "engine_gui/GuiManager.h"

namespace Orkige
{
	//---------------------------------------------------------
	GuiTabBar::GuiTabBar(String const & _id, woptr<GuiToggleGroup> const & _group)
		: id(_id), group(_group), applied(-2), changed(false)
	{
	}
	//---------------------------------------------------------
	GuiTabBar::~GuiTabBar()
	{
	}
	//---------------------------------------------------------
	void GuiTabBar::addTab(String const & checkboxId, String const & panelId)
	{
		GuiManager & manager = GuiManager::getSingleton();
		// the tab checkbox joins the single-selection group (a tap there clears the
		// others); the panel is recorded index-aligned so sync() can pair them
		if(optr<GuiToggleGroup> g = this->group.lock())
		{
			if(manager.widgetExists(checkboxId))
			{
				g->addMember(manager.getWidgetAs<GuiCheckBox>(checkboxId).lock());
			}
		}
		this->panels.push_back(manager.widgetExists(panelId)
			? manager.getWidget(panelId)
			: woptr<GuiWidget>());
	}
	//---------------------------------------------------------
	void GuiTabBar::setSelected(int index)
	{
		if(optr<GuiToggleGroup> g = this->group.lock())
		{
			g->setSelected(index);
		}
	}
	//---------------------------------------------------------
	int GuiTabBar::getSelected() const
	{
		if(optr<GuiToggleGroup> g = this->group.lock())
		{
			return g->getSelected();
		}
		return -1;
	}
	//---------------------------------------------------------
	bool GuiTabBar::pollChanged()
	{
		const bool was = this->changed;
		this->changed = false;
		return was;
	}
	//---------------------------------------------------------
	void GuiTabBar::sync()
	{
		const int selected = this->getSelected();
		if(selected == this->applied)
		{
			return;		// steady - nothing to do
		}
		this->applyVisibility(selected);
		if(this->applied != -2)
		{
			this->changed = true;	// a real change (not the first apply)
		}
		this->applied = selected;
	}
	//---------------------------------------------------------
	void GuiTabBar::applyVisibility(int selected)
	{
		for(std::size_t i = 0; i < this->panels.size(); ++i)
		{
			optr<GuiWidget> panel = this->panels[i].lock();
			if(!panel)
			{
				continue;
			}
			// group-alpha 0 hides the panel and makes its whole subtree input-inert
			// (the cascade the modal show/hide relies on); 1 shows the selected one
			panel->setGroupAlpha(static_cast<int>(i) == selected ? 1.0f : 0.0f);
		}
	}
}
