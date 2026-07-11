/********************************************************************
	created:	Wednesday 2010/10/27 at 13:08
	filename: 	FastGuiWidget.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
#ifndef __FastGuiWidget_h__27_10_2010__13_08_39__
#define __FastGuiWidget_h__27_10_2010__13_08_39__

#include "engine_fastgui/IGuiObject.h"
#include "engine_fastgui/FastGuiView.h"
#include "engine_fastgui/UiRenderer.h"
#include <core_util/UiLayout.h>

namespace Orkige
{
	class ORKIGE_ENGINE_DLL FastGuiWidget : public IGuiObject
	{
		OOBJECT(FastGuiWidget, IGuiObject);
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		UiLayer* layer;
		woptr<FastGuiView> view;
	private:
		bool visible;
		//! @brief rect-anchor layout (opt-in). A widget with layoutEnabled is
		//! placed by FastGuiManager's per-frame resolve pass against its parent
		//! (or the screen root) instead of the absolute pixels it was authored
		//! with; a widget that never touches a layout setter keeps behaving
		//! exactly as before (absolute pixels) - zero migration.
		bool layoutEnabled;
		LayoutNode layout;
		woptr<FastGuiWidget> layoutParent;
		//! resolve against the safe-area root rather than the full window (only
		//! consulted for a parentless widget)
		bool layoutUseSafeArea;
		//! group arrangement (LGT_None = children keep their own anchors) and
		//! content-size-fit; both feed the manager's two-pass resolve. Opt-in
		//! like the anchor node - untouched leaves a plain absolute widget.
		LayoutGroup layoutGroup;
		LayoutContentFit layoutFit;
		//--- Methods -----------------------------------------------
	public:
		FastGuiWidget(String const & id, String const & atlas, uint z);
		virtual ~FastGuiWidget();

		//! set position of this widget
		virtual void setPosition(Ogre::Real left, Ogre::Real top) = 0;
		//! set size of this widget
		virtual void setSize(Ogre::Real width, Ogre::Real height) = 0;
		//! get size of this widget
		virtual Ogre::Vector2 getSize() = 0;
		//! get position of this widget
		virtual Ogre::Vector2 getPosition() = 0;
		//! get layer this widget is in
		inline UiLayer* getLayer() const;
		//! get the view of this layer
		inline woptr<FastGuiView> getView();
		//! center widget horizontally on the screen
		void centerHorizontal();

		//--- rect-anchor layout (opt-in; @see FastGuiManager resolve pass) ---
		//! @brief parent this widget under another: it then resolves inside the
		//! parent's rect. A parentless widget resolves against the screen root.
		void setParent(optr<FastGuiWidget> const & parent);
		//! @brief anchor min/max as parent-rect fractions 0..1 (min==max on an
		//! axis = a point anchor; min<max = stretch with the parent)
		void setAnchors(float minX, float minY, float maxX, float maxY);
		//! @brief anchor min/max from a named preset (TopLeft, Center,
		//! StretchAll, StretchTop, ... - the 9-way alignment vocabulary plus the
		//! stretch bands); case-insensitive
		void setAnchorPreset(String const & preset);
		//! @brief the 0..1 pivot the anchoredPosition refers to inside the rect
		void setPivot(float x, float y);
		//! @brief offsets from the anchor rect corners to the widget corners,
		//! design units (left/top = offsetMin, right/bottom = offsetMax)
		void setOffsets(float left, float top, float right, float bottom);
		//! @brief position the pivot point relative to the anchor (design units)
		void setAnchoredPosition(float x, float y);
		//! @brief size added beyond the anchor-rect span (design units)
		void setSizeDelta(float width, float height);
		//! @brief resolve a parentless widget against the safe rect (window minus
		//! the notch/home-bar insets) instead of the full window
		void setUseSafeArea(bool enable);

		//--- layout groups + content-size-fit (opt-in; @see the 2c core) ---
		//! @brief make this widget a group that auto-arranges its layout
		//! children, overriding their anchors ("none"/"horizontal"/"vertical"/
		//! "grid"; case-insensitive). "none" reverts to plain anchor children.
		void setLayoutGroup(String const & type);
		//! @brief a group's inner padding, in design units (left/top/right/bottom)
		void setGroupPadding(float left, float top, float right, float bottom);
		//! @brief a group's between-children gap (design units); the second value
		//! is the grid's row gap (<= 0 reuses the first for both axes)
		void setGroupSpacing(float spacing, float spacingY = 0.0f);
		//! @brief a horizontal/vertical group's cross-axis child alignment
		//! ("start"/"center"/"end")
		void setChildAlignment(String const & align);
		//! @brief stretch a horizontal/vertical group's children across the cross axis
		void setChildForceExpand(bool enable);
		//! @brief a grid group's cell size in design units
		void setGridCellSize(float width, float height);
		//! @brief a grid group's constraint ("flexible"/"columns"/"rows") + count
		void setGridConstraint(String const & constraint, int count);
		//! @brief content-size-fit per axis ("none"/"preferred"): a preferred axis
		//! sizes the widget to its measured content (a button to its label, a
		//! group to its children)
		void setContentSizeFit(String const & horizontal, String const & vertical);

		//! @brief is this widget placed by the layout resolver?
		inline bool isLayoutEnabled() const { return this->layoutEnabled; }
		//! @brief the layout descriptor (the resolver's input; @see FastGuiManager)
		inline LayoutNode const & getLayoutNode() const { return this->layout; }
		//! @brief the group arrangement descriptor (LGT_None = not a group)
		inline LayoutGroup const & getLayoutGroup() const { return this->layoutGroup; }
		//! @brief the content-size-fit descriptor
		inline LayoutContentFit const & getContentFit() const { return this->layoutFit; }
		//! @brief the layout parent (empty for a screen-root child)
		inline woptr<FastGuiWidget> getLayoutParent() const { return this->layoutParent; }
		//! @brief resolve against the safe-area root (parentless widgets only)
		inline bool getUseSafeArea() const { return this->layoutUseSafeArea; }
		//! @brief the intrinsic preferred content size fed to content-size-fit and
		//! group arrangement. The base is the widget's current size; text widgets
		//! override to measure their caption.
		virtual Ogre::Vector2 getPreferredSize();
		//! @brief a scroll viewport's current scroll offset in window pixels
		//! (0 for every other widget); the resolver shifts this widget's children
		//! by it. @see FastGuiScrollView
		virtual LayoutVec2 getScrollOffset() const { return LayoutVec2(); }
		//! @brief the manager hands a scroll viewport its resolved size + the
		//! content's preferred extent each relayout (a no-op on plain widgets)
		virtual void onLayoutResolved(Ogre::Vector2 const & viewportSize,
			Ogre::Vector2 const & contentExtent) { (void)viewportSize; (void)contentExtent; }
		//! @brief a mouse-wheel notch over @p cursorPos (@p wheelDelta = 120 per
		//! notch, sign = direction); a scroll viewport scrolls, others ignore it
		virtual void onMouseWheel(Ogre::Vector2 const & cursorPos, int wheelDelta)
		{ (void)cursorPos; (void)wheelDelta; }
		//! @brief push a resolved absolute rect into the widget (setPosition +
		//! setSize); the resolver calls this each relayout
		void applyResolvedRect(float x, float y, float width, float height);

		//! show or hide
		//void setVisibility(bool enable);
		//bool getVisibility();
		//! for priority sorting
		inline bool operator < (FastGuiWidget const & other) const;
	protected:
		//! opt into the layout system on the first layout setter, seeding the
		//! node from the widget's current on-screen rect so it stays put until
		//! anchors/offsets change
		void ensureLayout();
	private:
	};
	//---------------------------------------------------------------
	inline UiLayer* FastGuiWidget::getLayer() const
	{
		return this->layer;
	}
	//---------------------------------------------------------------
	inline woptr<FastGuiView> FastGuiWidget::getView()
	{
		return this->view;
	}
	//---------------------------------------------------------------
	inline bool FastGuiWidget::operator < (FastGuiWidget  const & other) const 
	{
		return this->getLayer()->getIndex() > other.getLayer()->getIndex();
	}
	//---------------------------------------------------------------
	struct FastGuiWidgetOptrCmp
	{
		inline bool operator()( optr<FastGuiWidget> const & lhs, optr<FastGuiWidget> const & rhs)
		{
			return *lhs < *rhs;
		}
	};
}

#endif //__FastGuiWidget_h__27_10_2010__13_08_39__