/********************************************************************
	created:	Saturday 2026/07/26 at 12:00
	filename: 	GuiTabBar.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __GuiTabBar_h__26_7_2026__12_00_00__
#define __GuiTabBar_h__26_7_2026__12_00_00__

#include "engine_module/EnginePrerequisites.h"

#include <vector>

namespace Orkige
{
	class GuiToggleGroup;
	class GuiWidget;
	class GuiCheckBox;

	//! @brief a tab bar: a single-selection group of tab checkboxes, each paired
	//! with a sibling content panel whose visibility follows the selection. Pure
	//! composition over the existing pieces - the radio semantics are a
	//! GuiToggleGroup (a tapped tab clears the others), and "show one panel" is
	//! the widget group-alpha the modal show/hide already uses (0 = hidden AND
	//! input-inert, cascaded to the panel's whole subtree). This object only owns
	//! the tab<->panel pairing and syncs it once per frame (the GuiManager ticks
	//! it), so nothing here is backend-specific - it renders on both flavors by
	//! construction. Author it declaratively with the .oui `[TabBar]` section or
	//! from Lua (gui:createTabBar(id) then bar:addTab(checkboxId, panelId)). The
	//! GuiManager owns the tab bar so it outlives a script frame.
	class ORKIGE_ENGINE_DLL GuiTabBar
	{
	public:
		//! @param group the single-selection group the tab checkboxes join (the
		//! GuiManager creates it alongside the tab bar). May be empty; addTab is
		//! then a no-op that keeps the panel list length in step.
		GuiTabBar(String const & id, woptr<GuiToggleGroup> const & group);
		~GuiTabBar();

		//! @brief add a tab: the checkbox @p checkboxId joins the group as the next
		//! member, and the widget @p panelId becomes the panel shown while that tab
		//! is selected. Ids are resolved through the GuiManager, so a missing widget
		//! is skipped with the pair still recorded (the indices stay aligned).
		void addTab(String const & checkboxId, String const & panelId);

		//! @brief select tab @p index (updates the group's checked tab and shows the
		//! matching panel next sync). -1 hides every panel when the group allows it.
		void setSelected(int index);
		//! the selected tab index, or -1 when none is selected
		int getSelected() const;
		//! number of tabs
		int getTabCount() const { return static_cast<int>(this->panels.size()); }

		//! @brief poll-and-consume: true once after the selected tab changed (the
		//! polled idiom, like GuiButton::wasClicked / GuiToggleGroup::pollChanged)
		bool pollChanged();

		//! @brief reconcile panel visibility with the current selection. Called once
		//! per frame by the GuiManager (after the input dispatch, so a tab tap is
		//! already reflected in the group). Applies only on a change - a steady tab
		//! bar does nothing.
		void sync();

		String const & getId() const { return this->id; }

	private:
		//! @brief show panel @p selected, hide the rest (group-alpha 1/0, cascaded).
		//! @param animate let a panel that declares an `enter`/`exit` transition
		//! play it instead of snapping (false for the first apply, which must not
		//! animate a screen's unselected panels out as it opens)
		void applyVisibility(int selected, bool animate);

		String								id;
		woptr<GuiToggleGroup>				group;
		std::vector<woptr<GuiWidget> >		panels;		//!< index-aligned with the group members
		int									applied;	//!< last selection pushed to the panels (-2 = never)
		bool								changed;	//!< poll latch
	};
}

#endif //__GuiTabBar_h__26_7_2026__12_00_00__
