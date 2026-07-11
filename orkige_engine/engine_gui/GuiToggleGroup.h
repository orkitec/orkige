/********************************************************************
	created:	Saturday 2026/07/11 at 18:30
	filename: 	GuiToggleGroup.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __GuiToggleGroup_h__11_7_2026__18_30_00__
#define __GuiToggleGroup_h__11_7_2026__18_30_00__

#include "engine_module/EnginePrerequisites.h"
#include "core_util/ToggleGroupState.h"

#include <vector>

namespace Orkige
{
	class GuiCheckBox;

	//! @brief single-selection (radio) semantics over a set of checkboxes:
	//! checking one member clears the others. The pure state machine lives in
	//! core_util/ToggleGroupState (headless unit-tested); this wraps it with the
	//! member checkboxes and their sprites. A tapped member routes through the
	//! group (GuiCheckBox holds a back-reference), so single-selection is
	//! enforced in ONE place. Scripts poll getSelected()/pollChanged(); the
	//! GuiManager owns the group so it outlives a script frame.
	class ORKIGE_ENGINE_DLL GuiToggleGroup
	{
	public:
		explicit GuiToggleGroup(String const & id);
		~GuiToggleGroup();

		//! @brief add a checkbox as the next member; wires its back-reference so
		//! a tap routes here. The first member added at index 0.
		void addMember(optr<GuiCheckBox> const & checkbox);

		//! the selected member index, or -1 when none is selected
		int getSelected() const { return this->state.selected; }
		//! @brief force the selection (updates every member's checked sprite);
		//! -1 clears it when the group allows the empty state
		void setSelected(int index);
		//! @brief allow deselecting the current member by tapping it again
		//! (default false = a plain radio group keeps one selected)
		void setAllowNone(bool allow) { this->state.allowNone = allow; }
		//! number of members
		int getMemberCount() const { return this->state.count; }

		//! @brief poll-and-consume: true once after the selection changed (the
		//! polled idiom, like GuiButton::wasClicked)
		bool pollChanged();

		//! @brief a member checkbox reports it was tapped (called from
		//! GuiCheckBox::onCursorPressed). Enforces single-selection and syncs the
		//! member sprites.
		void handleMemberTapped(GuiCheckBox* checkbox);

		String const & getId() const { return this->id; }
	private:
		//! push each member's checked sprite to match the state (notify off so
		//! the sync does not re-enter the group)
		void applySelection();

		String								id;
		ToggleGroupState					state;
		std::vector<woptr<GuiCheckBox> >	members;
		bool								changed;	//!< poll latch
	};
}

#endif //__GuiToggleGroup_h__11_7_2026__18_30_00__
