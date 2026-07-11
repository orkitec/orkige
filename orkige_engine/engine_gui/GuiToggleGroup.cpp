/********************************************************************
	created:	Saturday 2026/07/11 at 18:30
	filename: 	GuiToggleGroup.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "engine_gui/GuiToggleGroup.h"
#include "engine_gui/GuiCheckBox.h"

namespace Orkige
{
	//---------------------------------------------------------
	GuiToggleGroup::GuiToggleGroup(String const & _id) : id(_id), changed(false)
	{
	}
	//---------------------------------------------------------
	GuiToggleGroup::~GuiToggleGroup()
	{
		// clear the members' back-references so a checkbox outliving the group
		// never dereferences it
		for(woptr<GuiCheckBox> const & weak : this->members)
		{
			if(optr<GuiCheckBox> member = weak.lock())
			{
				member->setToggleGroup(NULL);
			}
		}
	}
	//---------------------------------------------------------
	void GuiToggleGroup::addMember(optr<GuiCheckBox> const & checkbox)
	{
		if(!checkbox)
		{
			return;
		}
		checkbox->setToggleGroup(this);
		this->members.push_back(checkbox);
		this->state.count = static_cast<int>(this->members.size());
		// a member that comes in already checked seeds the selection
		if(checkbox->isChecked())
		{
			this->state.selected = this->state.count - 1;
			this->applySelection();
		}
	}
	//---------------------------------------------------------
	void GuiToggleGroup::setSelected(int index)
	{
		if(this->state.select(index))
		{
			this->applySelection();
			this->changed = true;
		}
	}
	//---------------------------------------------------------
	bool GuiToggleGroup::pollChanged()
	{
		bool was = this->changed;
		this->changed = false;
		return was;
	}
	//---------------------------------------------------------
	void GuiToggleGroup::handleMemberTapped(GuiCheckBox* checkbox)
	{
		// find the tapped member's index
		int index = -1;
		for(std::size_t i = 0; i < this->members.size(); ++i)
		{
			if(this->members[i].lock().get() == checkbox)
			{
				index = static_cast<int>(i);
				break;
			}
		}
		if(index < 0)
		{
			return;
		}
		const int before = this->state.selected;
		const int after = this->state.onMemberTapped(index);
		this->applySelection();
		if(after != before)
		{
			this->changed = true;
		}
	}
	//---------------------------------------------------------
	void GuiToggleGroup::applySelection()
	{
		for(std::size_t i = 0; i < this->members.size(); ++i)
		{
			optr<GuiCheckBox> member = this->members[i].lock();
			if(!member)
			{
				continue;
			}
			// notify off: this sync is the single source of truth, it must not
			// re-enter the group through the checkbox's toggle path
			member->setChecked(this->state.isSelected(static_cast<int>(i)), false);
		}
	}
}
